# The Marathon stress game towards turn 2000, and where stock fails (one-off 9074, D161)

The owner, 2026-11-16 03:53 EST (D161): carry 9073's Marathon stress game on to about turn 2000, a save every 50 turns,
and load each save in the stock (32-bit) game to see whether any of them crashes it. Ticket
`tracker/issues/9074-marathon-stress-game-to-t2000.md`. Status: **final (2026-11-17 23:30 EST)**: the headless game
reached turn 2000 (2026-11-17 23:23, game state 0, no victory); the stock series was closed at T1650 by the owner's order
D180 (section 4.2).

**Result.** The x64 server played the game from turn 400 to turn 2000 with no game failure (one segment was cut by `run_server.py`'s own timeout and rerun, section 3); 32 saves, one every 50 turns,
`F:/c5-64bit-work/9074/saves/TS1.20-T0450..T2000.Civ5Save` (5.2 MB at T400, 16.5 MB at T2000). Stock (32-bit EXE,
43-civ guard DLL) loads and plays every save up to T1050 (21 of 21) and **first fails at T1100**; from T1100 to T1650
9 of 14 loads fail (6 crashes in the load, 3 hangs), in the EXE's own code, with about 1 GB of address space still
free: a hang in a record-pool list walk entered from Lua (VA 0x5B1F80-0x5B202A) and crashes under the Lua-visible
event manager. Neither the 4 MB queue overflow (PR #13397's subject) nor an allocation failure is the cause
(sections 4.1, 4.2).

## 1. The game `[C, save facts; L]`

- 9073's series `TS1.20` (`F:/c5-64bit-work/9071/stress/saves/TS1.20-TNNN.Civ5Save`, every 25 turns to T400):
  `Tectonic Stress 1.20.lua` (9073's `tools/stressmap/`), 180 x 112 = 20,160 plots, `WORLDSIZE_HUGE`, 42 majors +
  20 city states (63 slots with the observer), Marathon, Prince, ancient era, map seed 9071, sync seed 9072, spec
  `F:/c5-64bit-work/9071/stress/headless/spec-1.20.json`. The size is the largest the stock EXE loads (about
  20,000 plots load, 21,879 overflow the stack: `research/02-exe-internals`, 9073's section 6.1).
- From T400 the game is carried on by this ticket in the x64 server (`server/run_server.py --load`, the release-43
  layout `F:/c5-64bit-work/0334/rebuild/layout/x64-04b1211b-m43-p9aa30f1/bin`, game database
  `F:/c5-64bit-work/9071/lc-db/Civ5DebugDatabase.db` as 9073), in 50-turn segments, each segment loading the last
  save. Saves: `F:/c5-64bit-work/9074/saves/TS1.20-TNNNN.Civ5Save` (four digits), SHA-256 of each in
  `F:/c5-64bit-work/9074/headless/seg.log`, turn times and the server report of each beside it. Scripts:
  `F:/c5-64bit-work/9074/segments.sh` (9073's driver with the changes below) and `resume700.sh`.

## 2. How the game is kept from ending before turn 2000 `[C, DLL source; L]`

- **Max turns is not the limit.** The save's max-turn setting is 3000 (9073's spec `maxTurns: 3000`; read back from
  the game, `Game.GetMaxTurns()` = 3000 at T400). The DLL ends a game on max turns only when the elapsed turns reach
  it (`CvGame::testVictory`, `getElapsedGameTurns() >= getMaxTurns()`), and under AI autoplay it then sets
  `GAMESTATE_EXTENDED` instead of `GAMESTATE_OVER`, so the game would go on even past 3000.
- **Victories were on.** 9073's spec has `"victories": []`, and its server.cfg had `victories=` empty; this does
  **not** mean "no victory": the game reads every victory valid. Measured: at T400 all five (`VICTORY_TIME`,
  `_SPACE_RACE`, `_DOMINATION`, `_CULTURAL`, `_DIPLOMATIC`) were `true` (the probe below), and 9073's stock load of
  T250 lists the same five as `true` in its facts (`F:/c5-64bit-work/9071/stress/cal/stock-t250/acceptance-run.json`).
  (Corrects the 9071 session's note of 2026-11-16 that its headless runs had every victory off.) On Marathon a space
  race or diplomatic victory is plausible before turn 2000 [I].
- **What was done:** a Lua probe, `F:/c5-64bit-work/9074/novictory.lua`, run by `run_server.py --lua-probe` after
  each load and before autoplay: `Game.SetVictoryValid(id, false)` for every row of `Victories`. The flag is
  `CvPreGame::setVictory` (`CvGame::isVictoryValid` returns `CvPreGame::isVictory`), part of the pregame the DLL
  writes into the save, so it stays off in the saves: the T450 segment's probe read all five `false->false` [L].
  The saves remain ordinary saves; stock loads them (section 4). From T450 on the game therefore differs from a
  default game in one setup flag: no victory can be won [C]. The AI's victory strategies may differ from a game
  with victories on [I, not measured].

## 3. The headless run `[L, 2026-11-16..17, workstation, other teams' load; not for timing]`

| segment | mean s/turn | note |
|---|---|---|
| 400 -> 450 | 33.8 | 2026-11-16 04:03 start |
| 450 -> 500 | 48.1 | |
| 500 -> 550 | 39.4 | |
| 550 -> 600 | 62.5 | |
| 600 -> 650 | 69.8 | |
| 650 -> 700 | about 90-96 at turns 689-690 | the last segment with the DLL's AI logging switches on |
| 700 -> 750 | 68.7 | `--dll-log-flags 0` from here |
| 750 -> 800 | 66.4 | |
| 800 -> 850 | 82.7 | |
| 850 -> 900 | 89.0 | |
| 900 -> 950 | 67.8 | |
| 950 -> 1000 | 66.7 | |
| 1000 -> 1050 | 76.3 | |
| 1050 -> 1100 | about 97 (4,871 s for the segment with the load) | |
| 1100 -> 1150, first try | 64-709, mean about 170 | turns 1100-1140 while the stock T1100 hang held a core and a 3.3 GB dump was written; `run_server.py`'s default `--timeout 7200` killed the server at turn 1141, no save; restarted from T1100 at 19:45 with `--timeout 21600` |
| 1100 -> 1150 | 126.3 (max 464) | restart 19:40; the stock probes ran beside it on the same machine to T1650 |
| 1150 -> 1200 | 128.9 | |
| 1200 -> 1250 | 78.0 | |
| 1250 -> 1300 | 129.1 | |
| 1300 -> 1350 | 132.6 | |
| 1350 -> 1400 | 110.2 | |
| 1400 -> 1450 | 92.5 | |
| 1450 -> 1500 | 102.7 | |
| 1500 -> 1550 | 116.4 | |
| 1550 -> 1600 | 109.5 | |
| 1600 -> 1650 | 122.1 | |
| 1650 -> 1700 | 134.1 | the last stock probe (T1650) ran beside it; D180 at 14:30 |
| 1700 -> 1750 | 98.9 | |
| 1750 -> 1800 | 90.3 | |
| 1800 -> 1850 | 116.0 | |
| 1850 -> 1900 | 78.3 | |
| 1900 -> 1950 | 97.1 (max 676) | |
| 1950 -> 2000 | 88.0 | done 2026-11-17 23:23:36; `novictory` read all five victories `false` and game state 0 at every load |

From T700 the segments run with `--dll-log-flags 0` (the DLL's AI-logging paths off; they cost 49-60 % of a turn with
identical game behaviour, `turn-time-baselines.md` section 9) to bring the remaining 1,300 turns from days to less.
Save size grows about 0.35-0.45 MB per 50 turns (5.24 MB at T400, 7.80 MB at T650, 10.1 MB at T900, 11.8 MB at
T1100, 14.5 MB at T1500, 16.5 MB at T2000). Means from each segment's `TS1.20-TNNNN.turn-times.tsv` (51 rows with the
load turn); the machine was shared with other teams' work, so these are not timings to compare.

## 4. Stock probes `[L, 2026-11-16, workstation, c5h-i]`

Method: `F:/c5-64bit-work/9074/probe.sh` (9073's `probe.sh` with a save-path argument): the stock EXE with the
43-civ build of the pinned DLL `i0ship-26234371-m43-ia` (sha256 `204fbfcc...`) on pool instance c5h-i (claimed by
impl-9074; standard files backed up by SHA-256 by the 9071 session, restored by hash at the release),
`perf/profilekit/acceptance.py run ... --config stock-32 --turns 5 --no-ledger --issue 9074`: load the save, play 5
turns of AI autoplay, one memory sample of the EXE per turn (`tools/harness/memory.py`), the harness's safe stop, the
front end restored, the save copy removed. "Free" is the EXE's free user address space (of 4 GB, large-address-aware),
"largest free" its largest free block: the figure an allocation fails on. Run folders
`F:/c5-64bit-work/9074/stock/tNNNN/` (`acceptance-run.json`, `per-turn.csv`, `memory.csv`, `run.log`, pictures).
T250 is 9073's run (10 turns, `F:/c5-64bit-work/9071/stress/cal/stock-t250`): clean, free 785 MB at load, about
600 MB after the first turn.

| save turn | load s | end | free MB at load | largest free MB at load | private MB at load | min free MB (5 turns) | min largest free MB | private MB at the last turn | mean s/turn |
|---|---|---|---|---|---|---|---|---|---|
| 50 | 184 | autoplay handed back at turn 55 | 835 | 786 | 2394 | 723 | 669 | 2518 | 32 |
| 100 | 173 | autoplay handed back at turn 105 | 809 | 755 | 2466 | 725 | 667 | 2517 | 38 |
| 150 | 201 | autoplay handed back at turn 155 | 689 | 640 | 2521 | 601 | 549 | 2614 | 54 |
| 200 | 234 | autoplay handed back at turn 205 | 750 | 693 | 2541 | 582 | 445 | 2632 | 58 |
| 250 (9073) | 184 | autoplay handed back at turn 260 | 785 | 739 | 2497 | 574 | 466 | 2625 | 59 |
| 300 | 217 | autoplay handed back at turn 305 | 620 | 565 | 2623 | 478 | 320 | 2715 | 103 |
| 350 | 232 | autoplay handed back at turn 355 | 717 | 660 | 2550 | 572 | 510 | 2653 | 88 |
| 400 | 200 | autoplay handed back at turn 405 | 658 | 597 | 2637 | 467 | 249 | 2733 | 110 |
| 450 | 230 | autoplay handed back at turn 455 | 646 | 589 | 2614 | 486 | 416 | 2723 | 119 |
| 500 | 229 | autoplay handed back at turn 505 | 622 | 569 | 2658 | 378 | 269 | 2785 | 164 |
| 550 | 286 | autoplay handed back at turn 555 | 643 | 583 | 2648 | 412 | 264 | 2791 | 199 |
| 600 | 306 | autoplay handed back at turn 605 | 461 | 409 | 2725 | 282 | 156 | 2862 | 223 |
| 650 | 399 | autoplay handed back at turn 655 | 542 | 484 | 2730 | 368 | 123 | 2878 | 207 |
| 700 | 352 | autoplay handed back at turn 705 | 569 | 507 | 2733 | 365 | 298 | 2850 | 222 |
| 750 | 322 | autoplay handed back at turn 755 | 550 | 486 | 2742 | 313 | 144 | 2886 | 205 |
| 800 | 383 | autoplay handed back at turn 805 | 434 | 371 | 2803 | 270 | 192 | 2908 | 232 |
| 850 | 473 | autoplay handed back at turn 855 | 487 | 431 | 2795 | 252 | 154 | 2952 | 266 |
| 900 | 402 | autoplay handed back at turn 905 | 514 | 448 | 2783 | 251 | 82 | 2973 | 237 |
| 950 | 291 | autoplay handed back at turn 955 | 477 | 420 | 2810 | 276 | 103 | 2977 | 214 |
| 1000 | 302 | autoplay handed back at turn 1005 | 465 | 407 | 2830 | 260 | 189 | 2977 | 175 |
| 1050 | 333 | autoplay handed back at turn 1055 | 458 | 406 | 2844 | 158 | 45 | 3010 | 236 |
| 1100 (run 1) | 384 | **hang in the first turn** (no turn 1101 after 1,500 s; section 4.1) | 471 | 403 | 2819 | 470 | 403 | 2819 (flat) | - |
| 1100 (run 2) | 426 | autoplay handed back at turn 1105 | 338 | 260 | 2941 | 144 | 32 | 3053 | 470 |
| 1100 (run 3) | (181 s, still loading) | **crash in the load**: access violation (section 4.1) | 1,053 at the last sample | 942 | 2183 | - | - | - | - |
| 1150 | (303 s, still loading) | **crash in the load**: write access violation at VA 0x55D9E2 (section 4.1) | 1,034 at the last sample | 940 | 2211 | - | - | - | - |
| 1200 | (1,805 s, never in game) | **hang in the load**: main thread busy for 28 min at VA 0x77C235, no allocation after 200 s (section 4.1) | 976 (flat from 200 s) | 976 | 2216 (flat) | - | - | - | - |
| 1250 | 455 | autoplay handed back at turn 1255 | 459 | 400 | 2820 | 225 | 154 | 3001 | 481 |
| 1300 | (364 s, still loading) | **crash in the load**: write access violation at VA 0x55D947 (section 4.1) | 1,004 at the last sample | 962 | 2229 | - | - | - | - |
| 1350 | 584 | autoplay handed back at turn 1355 | 285 | 218 | 2935 | 220 | 155 | 3004 | 298 |
| 1400 | 456 | autoplay handed back at turn 1405 | 443 | 386 | 2852 | 252 | 181 | 3014 | 259 |
| 1450 | (333 s, still loading) | **crash in the load**: read access violation at VA 0x5B1FB7, in the T1100 run-1 loop (section 4.1) | 1,002 at the last sample | 946 | 2258 | - | - | - | - |
| 1500 | 533 | autoplay handed back at turn 1505 | 413 | 361 | 2886 | 205 | 136 | 3028 | 319 |
| 1550 | (379 s, still loading) | **crash in the load**: read access violation at VA 0x7A3F0A, the #13397 fix run-2 site (section 4.1) | 928 at the last sample | 830 | 2295 | - | - | - | - |
| 1600 | (425 s, still loading) | **crash in the load**: write access violation at VA 0x55D9AE, the T1150/T1300 site (section 4.1) | 913 at the last sample | 846 | 2284 | - | - | - | - |
| 1650 | (1,811 s, never in game) | **hang in the load**: main thread in the T1100 run-1 record-list loop (VA 0x5B1F8D-0x5B2026) (section 4.1) | 924 (flat from about 600 s) | 873 | 2306 (flat) | - | - | - | - |

- **No save up to T1050 crashes stock** (21 of 21 clean, no dialog). **T1100 fails stock in 2 of 3 loads, T1150 crashed its one load, T1200 hung in its one load, T1250 played clean, T1300 crashed in its load, T1350 and T1400 played clean, T1450 crashed in its load, T1500 played clean, T1550 and T1600 crashed in their loads and T1650 hung in its load** (section 4.1):
  run 1 hung in its first turn, run 2 played its 5 turns (largest free block down to 32 MB at turn 1102, turn 1104
  993 s), run 3 crashed with an access violation while still loading. Machine free commit was never the limit (40-47 GB).
- **The address space closes in**: the private bytes at load rise about 0.55 MB per game turn (2,394 MB at T050,
  2,730 MB at T650, 2,783-2,803 MB at T800-T900), and the smallest largest-free block seen while playing falls from
  669 MB (T050) to 123 MB (T650), 82 MB (T900, turn 903) and 45 MB (T1050, turn 1051, private 3,137 MB).
  The figures move with the machine and the load (T350 better than T300), so these are indications only [L, n = 1
  per save]. Stock's turn time on these saves grows from 32 s to over 200 s per turn.

### 4.1 T1100: stock hangs in the first turn `[L, 2026-11-16 17:39-18:20 EST, c5h-i; C, dump]`

- Run 1: the save loaded (384 s; free 471 MB, largest free 403 MB, private 2,819 MB), autoplay was armed for 5
  turns, and turn 1101 never came. The harness's 1,500 s turn timeout ended the run (`end_reason` "no new turn for
  1500 s after turn None", code 5) and left the game running, as it does. No dialog, no crash file.
- The game was **not slow but looping**: from 17:47 to 18:12 private bytes stayed at 2,819.2 MB and the free address
  space at 470.8 MB (no allocation at all), the window was "not responding", one core was fully busy, and
  `!runaway` showed the main thread (thread 0) with all of the time (33:06 then 34:06 of user time one minute apart,
  every other thread about 34 s in total).
- Where (non-invasive `cdb -pv`, read only): the main thread is in a short EXE loop at VA 0x5B1F80-0x5B202A (ImageBase
  0x400000; the EXE was loaded at 0x8F0000 in that run, offsets +0x1B1F80-+0x1B202A). Behaviour, from the registers
  sampled six times 5 s apart: it walks an index-linked list of 56-byte records (record = base + index * 56, the next
  index at +0x2C with bit 31 a flag, the previous at +0x28, 0x0FFFFFFF the end marker), unlinking each record and
  putting it back on a free list, and stops only when it comes back to the index it started from. The samples cycle
  through the same few indices (0xD8DE, 0xD904, 0xD96D, 0xD973), so the list it walks is a cycle that does not pass
  the start index: the loop never ends [C, registers; I, what the list is]. The indices (about 55,000) say the
  pool holds more than 55,000 records [I]. The stack below the loop cannot be walked (no frame pointers); the raw
  stack holds older return addresses into `lua51_Win32` and the DLL's `CvLuaMap::lGetPlot` and
  `CvLuaScopedInstance<CvLuaPlot>::Push`, which are probably left over from Lua UI code and not the caller [I].
- **Not every load fails**: run 2 of the same save (18:23-19:16) played 5 turns cleanly [L].
- **Run 3 crashed in the load** (19:40-19:44): after 181 s, still loading (private 2,183 MB), the VP crash handler's
  box "Game Crash": exception 0xC0000005, read of 0xC0BC6FF4, `Location (in file)` `CivilizationV_DX11.exe+0x0037BADA`
  = RVA 0x37C6DA = VA 0x77C6DA. Memory at the crash (the handler's markers): total free 1,087 MB, largest free block
  1,026 MB, but below 2 GB only 43 MB free (largest 8 MB). The handler appends its "address space nearly exhausted"
  advice, but the read address is in the upper 2 GB and a GB was free there; the failure is not an allocation failure
  [C, crashes.log; I, cause]. The box was closed as owner (`cli.py dismiss --fatal`); `crash/` holds the VP minidump
  `CvMiniDump_20261116_194400_...dmp` and `crashes.log` (whose first entry is an older 02:47 stack overflow from
  9073's work on this instance, not this run).
- **The hang and the crash are in the same EXE code** [C, addresses]: run 3's crash stack starts with the return
  addresses RVA 0x1AA0D8 and 0x1B8A27, the same two seen under the run-1 loop, and the crash location RVA 0x37C6DA lies
  in the function at RVA 0x37C2xx that run 1's loop returned to (0x37C254). Both stacks also hold `lua51_Win32`
  frames: the path is entered from Lua [I]. So one EXE structure (the index-linked record pool of the run-1 loop) is
  found broken at about T1100 in 2 of 3 loads, once as a cycle (hang) and once as a bad read (crash) [I].
- The x64 server played the same turn 1100 -> 1101 without trouble (its segment 1100 -> 1150 went on), so this is
  in the EXE's own code, which the server does not run [L].
- Evidence (not in the repository): `F:/c5-64bit-work/9074/stock/t1100-run1/` (`acceptance-run.json`, `memory.csv`
  at 5 s, `hang/stacks1-4.txt`, `hang/eips.txt`, `hang/regs.txt`, `hang/disasm.txt`) and a full dump of the hung EXE
  `hang/x86-59172-20261116-181917.dmp` (3.29 GB, sha256 `ee889f9f...`, `tools/harness/dump.py`, non-invasive; the
  game went on). The game was then stopped with `tools/harness/stop_nolua.py --settle 60` (the save-file-checked stop,
  no override: the in-game stop needs the Lua side to report a human turn, which a hung main thread cannot; all save
  folders had been still for 2,116 s).
- **Not the 4 MB queue overrun of PR #13397, as far as measured** (checked 2026-11-16 for the owner) [C, dump and crash
  report; I where marked]:
  - LoneGazebo/Community-Patch-DLL#13397 (vaderkos, "lay out map graphics in budgeted batches so the engine's event
    stream cannot overflow") is **open, not merged** (2026-11-16); it is in no VP release and not in the stock DLL used
    here (pin 26234371e + the `ia` patch: none of its code). Its stream is the EXE's 4 MB game-to-UI queue
    (`g_EventSystemLocalMachine`, VA 0x1A0E400, channel 0 at 0x1A0F080; `02-exe-internals/message-queue-and-fixed-buffers.md`):
    both of its DX11 code signatures match the EXE in the run-1 dump at the relocated addresses.
  - The stock DLL used here carries the EngineQueueGuard (the `ia` patch, the approach of the closed #13372), which
    refuses a record that does not fit that queue instead of letting it overrun. The **public** VP release has neither
    the guard nor #13397.
  - Run 1 (hang): the queue's high-water mark reads 4,194,304 bytes (exactly full) and the guard's counters in the dump
    read 22,551 records / 3,787,776 bytes diverted, largest 256 B, the last at about 17:48:08 (tick 183,428,750 against
    the dump's uptime), i.e. after the load (17:46:03) and around when the main thread went flat (17:47-17:49). So the
    queue did overflow in run 1 and the guard kept it from corrupting memory; whether the overflow came before the hang
    or because of it (a stuck main thread stops swapping while writers go on) is not settled [I].
  - Run 3 (crash): the crash report says "UI message queue guard: active, 0 records (0 KB) dropped": the queue never
    filled before that crash, so it was not this overrun.
  - Both failure sites differ from the known overrun signature (dispatcher VA 0x6A59A5 crash / 0x6A59AA spin): here a
    record-list walk at 0x5B1F80 and a read at 0x77C6DA.
  - **The queue overflows on every load from T650 on** (the guard's own log, `install/crashlogs/queueguard.log` on c5h-i,
    one line per burst after two quiet seconds; KB dropped per burst, 2026-11-16) [C]:

    | save | load-end burst | second burst (first autoplay turns) | third burst (end of run) |
    |---|---|---|---|
    | T050-T600 | none | none | none |
    | T650 | 794 | - | - |
    | T700 | 883 | - | - |
    | T750 | 1,114 | - | - |
    | T800 | 1,448 | 175 | - |
    | T850 | 1,676 | 155 | 854 |
    | T900 | 1,720 | 1,015 | 1,442 |
    | T950 | 1,962 | 1,378 | 1,672 |
    | T1000 | 2,338 | 1,362 | 1,967 |
    | T1050 | 2,612 | 2,218 | 2,161 |
    | T1100 run 1 (hang) | 3,182 at 17:46:03 | about 517 more by 17:48 (dump: 3,699 KB in all), then the hang | - |
    | T1100 run 2 (clean) | 3,174 | 1,863 | 1,867 |
    | T1100 run 3 (crash) | none: crashed 181 s in, before the load-end burst | - | - |

    The first burst comes as the load ends, the second about 2-4 min later, the third at the run's end; these match
    #13397's triggers (whole-map layout on load and when the active player changes under autoplay) [I]. Without the
    guard every one of these loads from T650 would have overrun the queue.
  - Outward symptoms match the 2026-09 queue-overflow class (late huge-map loads that crash, hang or survive at
    random), so a test with #13397 applied on top of the guard build would settle it; not done.
- **T1100 with #13397 (MC order for the owner, 2026-11-16; build from survey 9075)** [L, c5h-i, 21:12-22:13]: the 43-civ -ia
  DLL of fork branch `i0-fix-13397` 28ca892ee (the pin + #13397's batching, re-implemented in `CvMap.cpp`, + the `ia`
  patch with the guard; sha256 `b596610b...`, "Release-5.4.8-40-g28ca892ee+ia440878983+m43 Copy"), differing from the
  guard build `204fbfcc...` only by that commit; three loads of T1100 by `F:/c5-64bit-work/9074/test13397.sh`, as the
  guard runs. CustomMods.log: "Map layout batching: DX11 EXE, event stream found" in all three runs. **Fix build,
  rejected** (9075, 2026-11-16).
  - Run 1 (21:12-21:38): load 228 s (guard build 384-426 s); **guard dropped 0 records** in the whole run (guard
    build: about 3.2 MB at the end of each T1100 load); **crash in the first autoplay turn** at 21:27:29, 11 min
    after the load: access violation, write to 0xAB2B3000 (a page boundary), `Location (in file)` EXE+0x15CDE2 = VA
    0x55D9E2; stack through the 4 MB queue's dispatcher (RVA 0x2A59A5), its pump (RVA 0x59D8A) and the listener
    trampoline (RVA 0x466919): a handler of that queue writing past a buffer [C, crashes.log; I, which buffer]. Memory
    at the crash: total free 316 MB, largest 233 MB, below 2 GB 41 MB (largest 8 MB). Not one of the guard build's
    failure sites. Pictures: 8 s after the load the view showed no improvements; 8 min later the Rome area was drawn.
    9075 is checking whether the deferred layout completes in game (its own live check missed a railway loop 60 s
    after a load), so a "no drops" result here is provisional; a crash or hang counts.
  - Run 2 (21:38-21:56): load 246 s, guard 0 drops; **crash in the first autoplay turn** at 21:55:47, 12 min after the
    load: read of 0x0FB36020 at `Location (in file)` EXE+0x3A330A = VA 0x7A3F0A; stack through the dispatcher of the
    Lua-visible "B" event manager (RVA 0x23ABD4, pump RVA 0x385BA7) and the trampoline (RVA 0x466919). Free 292 MB,
    largest 196 MB. Evidence `stock/t1100-f13397-run2/`, `stock/side-crash/20261116_215549_pid41176/`.
  - Run 3 (21:57-22:13): load 229 s, guard 0 drops; **crash in the first autoplay turn** at 22:12:00, 10.5 min after
    the load: read of 0x2B8A64F8 at EXE+0x1B13B7 = **VA 0x5B1FB7, inside the record-list loop (0x5B1F80-0x5B202A) the
    guard build's run 1 hung in** (the read of the neighbour record's link at +0x2C, from an index that points outside
    the pool) [C, address; I, reading]; stack through the B manager's dispatcher (RVA 0x23ABD4) and handler RVAs
    0x3490D9, 0x21FD48, 0x1C8745, 0x3274A9. Free 314 MB, largest 241 MB. Evidence `stock/t1100-f13397-run3/`,
    `stock/side-crash/20261116_221208_pid31676/`.
  - **The 0x55D9E2 crash is not the batching's**: the next save, T1150, on the guard build `204fbfcc...` crashed in
    its load (22:18:21, 303 s in) at the same site, `Location (in file)` EXE+0x15CDE2 = VA 0x55D9E2, write to
    0x92C87000 (a page boundary again), with free memory ample (1,068 MB, largest 1,024 MB); the guard had diverted 9,210 records
    (1,585 KB) before the crash (crash report line; the guard's own log writes a line only after two quiet seconds); its stack runs through the B manager's dispatcher (RVA 0x23ABD4) and the handler RVA
    0x3490D9, as fix runs 2 and 3, to RVA 0x2E282E as fix run 1 [C, crashes.log]. Evidence
    `stock/t1150/crash/` (minidump `CvMiniDump_20261116_221821_...`).
  - **Low addresses (below 2 GB) are short in every run, failed or clean** (MC question, 2026-11-16 22:30) [C, crash
    reports; L, `memory.csv` at 5 s]: crash reports, Sub2G free / Sub2G largest block: guard run 3 43,088 / 8,064 KB;
    fix run 1 41,320 / 8,320; fix run 2 42,212 / 8,320; fix run 3 42,980 / 8,320; T1150 34,360 / 5,504 KB; guard run 1
    (hang, no report; its last samples) 36.3 MB / 4.0 MB. But the clean probes sit as low or lower: lowest sample of
    Sub2G free / Sub2G largest, T050 33.5 / 1.9 MB, T300 30.3 / 0.54, T600 33.1 / 0.62, T900 33.4 / 1.4, T1000 29.6 /
    0.56, T1050 31.4 / 0.88, T1100 guard run 2 (clean) 34.8 / 0.77 MB. So on this map the EXE's address space below
    2 GB is nearly full from T050 on, in every load: it is a constant of these runs, present at every failure but
    not what separates the failures from the clean runs [I, from n = 1 per save and 5-s sampling, which can miss a
    short low]. What does grow with the turn is the total private bytes and the falling largest block above 2 GB
    (section 4 table) and the queue's overflow (table above).
  - **T1200 hangs in the load** (2026-11-16 23:18-23:50, guard build `204fbfcc...`) [L; C, registers]: after 1,805 s
    `wait_game.py` still had no game (acceptance code 3, "game did not come up"). From about 200 s in, private bytes
    stayed at 2,215.6 MB and the free address space at 976 MB (largest block 976 MB; below 2 GB 35.5 MB free, largest
    3.1 MB): no allocation for 27 min. `!runaway`: main thread 28:22 of user time, every other thread under 16 s.
    The EXE was loaded at 0xA70000; five eip samples 5 s apart all read EXE+0x37C235 = **VA 0x77C235**, in the same
    function (RVA 0x37C2xx) that the T1100 guard run-1 loop returned to and that the T1100 guard run-3 crash
    (VA 0x77C6DA) is in, and the stack holds the same return addresses RVA 0x1AA0D8 and 0x1B8A27. The guard dropped
    nothing in this load (its log has only "active" at 23:19:29). So the same EXE path now fails at T1100 (2 of 3),
    T1150 (crash) and T1200 (hang), with or without queue overflow [I]. Stopped with `stop_nolua.py --settle 60`
    (no override); evidence `F:/c5-64bit-work/9074/stock/t1200/` (`memory.csv`, `hang/stacks.txt`, `hang/eips.txt`).
  - **T1250 clean, T1300 crashes in the load** (2026-11-17, guard build `204fbfcc...`) [L; C, crashes.log]: T1250 loaded
    in 455 s and played turns 1251-1255 (481 s per turn, largest free block down to 154 MB, below 2 GB at least 36.1 MB
    free, largest 5.4 MB; the guard diverted 28,384 records / 4,789 KB at the load and 10,305 / 1,483 KB later). T1300
    crashed at 02:20:43, 364 s into its load: write to 0x860E8000 (a page boundary again), `Location (in file)`
    EXE+0x15CD47 = **VA 0x55D947**, 0x9B bytes before the T1150 / fix-run-1 site 0x55D9E2, same stack (RVA 0x2E282E,
    handler 0x3490D9, the B manager's dispatcher 0x23ABD4, pump 0x385BA7) [C]. Markers: total free 1,027,588 KB,
    largest 985,040 KB; Sub2G free 31,732 KB, largest 3,008 KB; guard "16807 records (2879 KB) dropped". Box closed by
    probe.sh as owner; evidence `F:/c5-64bit-work/9074/stock/t1300/crash/` (minidump `CvMiniDump_20261117_022043_...`).
    T1350 (04:07-04:47) played clean (584 s load, 298 s per turn, largest free down to 155 MB; guard 32,610 records /
    5,550 KB at the load). T1400 (05:41-06:12) played clean (456 s load, 259 s per turn, largest free down to 181 MB).
    **T1450 crashed in its load** (07:05:50, 333 s in): read of 0xD9B29068 at EXE+0x1B13B7 = **VA 0x5B1FB7**, the read of
    the next record's link inside the T1100 run-1 hang loop (as #13397 fix run 3), with the T1100 run-1 / run-3 stack:
    RVA 0x37C254, 0x124973, 0x1AA0D8, 0x1B8A27, then `lua51_Win32` [C, crashes.log]. Markers: total free 1,025,764 KB,
    largest 968,732 KB; Sub2G free 35,464 KB, largest 2,368 KB; guard 18,087 records (3,164 KB) dropped. Evidence
    `F:/c5-64bit-work/9074/stock/t1450/crash/`. So two failure sites recur: the record-pool walk entered from Lua
    (0x5B1F80 loop, 0x77C2xx caller; T1100 runs 1 and 3, T1200, T1450) and the write at 0x55D9xx under the B manager's
    dispatcher (T1150, T1300, fix run 1) [C, addresses].
    T1500 (08:27-09:05) played clean (533 s load, 319 s per turn, largest free down to 136 MB).
    **T1550 crashed in its load** (10:13:54, 379 s in): read of 0xBAFA5020 at EXE+0x3A330A = **VA 0x7A3F0A**, the site of
    #13397 fix run 2, now on the guard build; stack RVA 0xA8A19, handler 0x3490D0, the listener trampoline 0x466919,
    the B manager's dispatcher 0x23ABD4 and pump 0x385BA7 [C, crashes.log]. Markers: total free 958,324 KB, largest
    911,744 KB; Sub2G free 38,868 KB, largest 8,448 KB; guard 19,481 records (3,426 KB) dropped. Evidence
    `F:/c5-64bit-work/9074/stock/t1550/crash/`. So the fix build's three crash sites all recur on the guard build:
    the batching did not create any of them [C, addresses].
    **T1600 crashed in its load** (11:47:59, 425 s in): write to 0x98729000 (a page boundary) at EXE+0x15CDAE = **VA
    0x55D9AE**, the T1150/T1300 site, same stack (RVA 0x2E282E, 0x3490D9, 0x466919, 0x23ABD4, 0x385BA7) [C]. Markers:
    total free 984,616 KB, largest 927,816 KB; Sub2G free 38,644 KB, largest 5,248 KB; guard 20,289 records (3,599 KB)
    dropped. Evidence `F:/c5-64bit-work/9074/stock/t1600/crash/`.
    **T1650 hung in its load** (13:24-13:58; no game after 1,811 s, acceptance code 3) [L; C, registers]: private bytes
    flat at 2,306 MB and free at 924 MB (largest 873 MB; below 2 GB 39.9 MB free, largest 4.75 MB) from about 600 s;
    `!runaway` main thread 25:28 of user time, the next thread 23 s. The EXE was loaded at 0xD20000; five eip samples
    5 s apart read EXE+0x1B1F8D, +0x1B1F91, +0x1B1FC2, +0x1B2026 = **VA 0x5B1F8D-0x5B2026, the T1100 run-1 record-list
    loop**, esi (the record index) 0xB6CF in every sample; the frame above returns to RVA 0x3274A9 (a handler on the
    #13397 fix run-3 crash stack) [C]. The guard dropped nothing in this load (its log has only "active" at 13:26:52).
    Stopped with `stop_nolua.py --settle 60` (save folders still 2,014 s, no override). Evidence
    `F:/c5-64bit-work/9074/stock/t1650/` (`memory.csv`, `hang/stacks.txt`, `hang/eips.txt`, `hang/stop.json`).
    Since T1100: 14 stock loads on the guard build, 5 clean (T1100 run 2, T1250, T1350, T1400, T1500), 9 failed (6 crashes in the load, 3
    hangs) [L, n = 1-3 per save].
  - **Verdict of the test** [L, n = 3 against n = 3]: #13397 build 3 of 3 loads crash in the first autoplay turn,
    at three different sites, with 0 records dropped by the guard; guard build 2 of 3 fail (hang in the first turn,
    crash in the load), with 3-7 MB dropped per load. So the T1100 failures are **not caused by the 4 MB queue
    overflow**: the record-list structure of the guard run-1 hang breaks with no overflow at all (fix run 3), and the
    batching makes the first turn worse, not better [I, small n]. With 9075's own finding (map art lost), 9075 rejected
    the fix build ("fix build, rejected"; `research/03-dll-engine-boundary/upstream-fix-batch-2.md` 2.3). Pictures of
    these runs (`stock/f13397-shots/`) are diagnostic only, never acceptance pictures (D112).
  - acceptance.py does not notice a crash box in its turn phase (it waits for its 1,500 s turn timeout); for runs 2
    and 3 `F:/c5-64bit-work/9074/side13397.sh` closes a fatal box as owner and takes pictures 60 s and 240 s after
    the load.
- `probe.sh` now does the same by itself for a game left running after the turn timeout without a dialog: thread
  state into `tNNNN/hang/`, then `stop_nolua`.

### 4.2 The stock series closed (the owner, 2026-11-17, decision D180) `[L, 2026-11-16..17, c5h-i]`

The owner, 2026-11-17 (relayed by the MC): no more loads of the 50-turn saves in the stock client; the point where stock stops
running this map is found. The headless run goes on to about T2000 with a save every 50 turns. The last stock load
was T1650 (2026-11-17 13:24-13:58); no probe was running when the order came (14:30), and the probe loop was stopped
before T1700 was written.

- **First failing turn: T1100.** Every save from T050 to T1050 loaded and played 5 turns cleanly (21 of 21; T250 is
  9073's run). The first failure was T1100 run 1 (2026-11-16): a hang in the first autoplay turn.
- **Since T1100: 9 of 14 stock loads fail** (guard build `204fbfcc...`): 6 crash in the load (T1100 run 3, T1150,
  T1300, T1450, T1550, T1600) and 3 hang (T1100 run 1 in the first turn; T1200 and T1650 in the load). The 5 clean
  loads were T1100 run 2, T1250, T1350, T1400 and T1500 (the last clean one). One load per save after T1100,
  so a single clean load does not make a save safe [I].
  The #13397 fix build failed 3 of 3 loads of T1100 (section 4.1).
- **Where the hang is** [C, registers, dump]: the main thread loops in the EXE's walk of an index-linked pool of
  56-byte records, VA 0x5B1F80-0x5B202A (ImageBase 0x400000; next index at +0x2C, previous at +0x28, end marker
  0x0FFFFFFF): the list has become a cycle that never returns to its start (T1100 run 1, T1650). Its caller is the
  function at RVA 0x37C2xx (VA 0x77C2xx; T1200 hung there at VA 0x77C235), entered from Lua through RVA 0x1AA0D8 /
  0x1B8A27. The same structure gives the read crashes at VA 0x5B1FB7 (T1450, fix run 3) and VA 0x77C6DA (T1100 run 3).
  The other crashes are under the Lua-visible "B" event manager's dispatcher (RVA 0x23ABD4): writes to a page
  boundary at VA 0x55D947-0x55D9E2 (T1150, T1300, T1600, fix run 1) and a read at VA 0x7A3F0A (T1550, fix run 2).
- **Not the cause** [L, C]: the 4 MB game-to-UI queue overflow (both hangs in the load had 0 records dropped; fix
  build runs had 0 dropped and still crashed) and an allocation failure (about 0.9-1.0 GB free above 2 GB at every
  crash; below 2 GB nearly full in clean loads too, section 4.1). What grows with the turn is private bytes
  (about 2.4 GB at T050 to 2.9-3.0 GB while playing at T1100-T1500) and the load time (184 s at T050, 450-590 s at
  T1250-T1500). The x64 server plays every one of these turns.

## 5. Notes for whoever repeats this

- `server/run_server.py` kills the server after `--timeout` seconds (default 7200) and keeps no save: past turn 1000 on
  this map a 50-turn segment can take more than 2 h when the machine is busy; pass a larger `--timeout`.

- A fresh worktree has no built harness helpers: `acceptance.py` then fails at the launch with exit 3 and an empty
  output; `run.log` names `tools/harness/native/bin/bg_helper32.exe` missing. Set `C5_HARNESS_BIN` to a built `bin`
  folder (this run used a copy of the 9073 worktree's, sources identical, at `C:/c5-64bit-work/9074s/harness-bin`)
  [L, 2026-11-16].
- Stopping a driver started by the agent's background shell: stopping the shell task does not stop a Git-bash script's
  loop on Windows; find it by its command line (`Win32_Process`) and stop that PID only.
