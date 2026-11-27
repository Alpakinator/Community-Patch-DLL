# Saves off the game thread: design (ticket 5032, phase 1)

Written 2026-11-24 15:44-16:00 EST by the 5032 implementer (account B), offline. Phase 2 (the measurement and the go,
section 6b, and the offline corpus check of section 6a) followed on 2026-11-24 from 16:05 to 17:43 EST. The Architect's draft T2
(`research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md` sections 3.1 EX5, 3.2 EX8, 4 T2).
Nothing was built and no game ran. The only new numbers are one offline indication (section 2, grade D). Marks:
`[C]` read in code or the knowledge base, `[L]` a recorded live measurement, `[I]` inferred.

## 0. Result

- **Recommended: way (b1), "stored first, compressed later". It needs no code write, so it can be in the base
  product.** While the EXE saves, the proxy redirects one import slot, `zlib1.dll!deflateInit_`, and passes compression
  level 0 (stored) where the EXE asks for level 6. The EXE then writes its own header and pregame and a valid save file
  that is not compressed, at about a tenth of the deflate cost. Right after, the server reads that file on its own
  thread and compresses it again with its zlib 1.2.3 writer, which matches the game's byte for byte (154 of 155 corpus
  saves, the other one a truncated file; `research/03-dll-engine-boundary/server-host.md` section 9). Then it replaces
  the file with an atomic rename. At every moment the file on disk is a save that loads.
- **Way (a), a writer thread in the EXE by a code write**, saves only the small rest that (b1) leaves (the header and
  the file write, about 0.05 to 0.1 s per M autosave `[I]`). It costs much more: the EXE's stream, allocator and file
  object would be used from a second thread, and sites from the build table are needed. Not recommended. If it is ever
  built, it goes behind `[Extras] FullExeOptimizations` (D228).
- **GO, measured** (phase 2, section 6b, oldpc, build 4390913: oldpc has no 26577625). One M autosave holds the game
  thread **0.367 s ± 0.005 s** (grade A), 12.0 % of an M turn with an autosave every turn. The game's default interval
  is **10 turns** (5 kept). That gives **1.2 % of an M turn** against the 0.3 % floor; (b1)'s own reach is 0.7-0.85 %
  `[I]`. `UI.SaveGame` on the huge save holds the main thread **0.671 s** (grade B). A human turn writes two autosaves.
  (Replaces the phase 1 line "expected go ... 0.5 to 0.65 % [I]" of 2026-11-24 15:5x.)
- **The player sees it in two places.** The autosave at the start of the human's turn (`AutoSave_<turn>`; engine
  contract, saves.md section 3) holds up the turn's start. A user or quick save of a huge game (`UI.SaveGame`) froze the
  main thread for 1.64 to 2.0 s on the stock route `[L, 2026-11-15]`. In the indication below, the deflate alone falls
  from 0.71 s to 0.06 s on the huge save.

## 1. What the save costs today on the product path

| Fact | Value | Source |
|---|---|---|
| One M autosave, the whole `AutoSave` call | 0.455 s (b2), 0.545 s (b1) | `[L]` workstation 2026-11-16, `speedup-p0-shares.md` P0.5 |
| of it, the DLL's serialization in the server | 0.112 to 0.154 s | same |
| of it, the transfer of the 21.6 MB body | about 0.01 to 0.015 s | same `[I]` |
| of it, the EXE's header, zlib and file write | **0.33 to 0.38 s (72 %)** | same |
| zlib's share of the EXE's game-core thread, S4 (product path) | 67 % (0.62 of 0.93 s per turn) | `[L]` laptop 2026-11-16, `turn-time-study.md` W4b |
| Per-turn autosave against every 10th turn, S4 stock pacing | -0.599 s (-3.9 %) [-0.766, -0.432] | `[L]` laptop grade B, `turn-time-study.md` W5 `autosave_10` |
| `UI.SaveGame` on the huge save, main thread | 1.64 to 2.0 s, stock route | `[L]` 2026-11-15, Architect's review EX8; no figure for the patch |

How the EXE saves `[C, knowledge base]`: the DLL's `Write` methods fill the EXE's in-memory stream. In the product,
the proxy writes the server's body into that stream in 4 KB pieces (`proxy-design.md` 11.6, 18.5). The file is a plain
header, the pregame, the marker u32 2, then the body as one zlib stream in 64 KiB pieces. Each piece is a u32 length and
its bytes. The zlib stream is level 6, windowBits 15, memLevel 8, all input with Z_NO_FLUSH, then one Z_SYNC_FLUSH, with
no final block and no Adler-32 (`research/sources/civ5-headless/saves.md` section 2; `server-host.md` section 9). The
EXE imports zlib from `zlib1.dll` (1.2.3): `deflateInit_`, `deflate` and `deflateEnd` by name (import list in
`research/sources/exe-world-subsystems/imports.txt`). For files it imports `CreateFileW`/`A`, `WriteFile`,
`CloseHandle`, `MoveFileW`, `DeleteFileA`/`W`, `fopen`, `_wfopen` and `fwrite`. Which of these the save writer uses is
not recorded (step M0 below). The autosave runs inside the DLL's `gDLL->AutoSave` call. In the product that is the EXE's
game-core thread serving the server's call, while the server's `Update` waits. `UI.SaveGame` runs on the thread that
calls it, the main thread in a human's game.

## 2. Offline indication: the cost by compression level `[L, grade D]`

`perf/evidence/5032/levels.py` (this ticket) inflates a save's body and times the game's call pattern at each level
with Python's zlib (1.2.13, not the game's 1.2.3 build). It ran on the workstation on 2026-11-24 at 15:51 EST, while the
machine was shared and loaded, so only the ratios count. The saves were read in place, read only.

| Save | body | level 6 (the game's) | level 1 | level 0 (stored) |
|---|---|---|---|---|
| `TS0438-M-T150` (M, the 0438 copy on F:) | 21,494,962 B | 466 ms median (340-801, n 7), 875,780 B | 150 ms, 1,200,054 B | 112 ms (96-151), 21,496,614 B |
| `VP8P-HUGE_0350` (huge) | 37,694,481 B | 707 ms (614-762, n 5), 2,213,112 B | 184 ms, 2,988,659 B | **63 ms** (55-74), 37,697,368 B |

Inflating a stored body takes 104 to 146 ms (one run). In Python, level 0 still copies the data a few times and builds
a 21 to 38 MB result object. In the EXE, zlib's stored path copies into its window, computes Adler-32 (1.2.3's
`read_buf` does that at every level) and copies into the 64 KiB output buffer `[C, zlib 1.2.3 source]`. Estimate for the
EXE: 0.03 to 0.08 s per M body instead of about 0.25 s `[I]`.

## 3. The options compared

| Way | What changes | Write outside `.data` | Kept on the game thread per M autosave `[I]` | Risk and cost |
|---|---|---|---|---|
| **(b1) stored first, the server compresses later** (recommended) | The proxy's `deflateInit_` slot passes level 0 for the save's stream only. The EXE writes a valid stored save. The server re-deflates it with its 1.2.3 writer and renames it over the file | **no**: one import slot (the kbhook and queue-guard pattern, D228's list of base tools), optionally file-open slots for the path | stored pass 0.03-0.08 s + file write of 21.6 MB to the cache about 0.01-0.02 s + header | Low. The file always loads. The disk briefly holds about 24 times the bytes (M: 21.5 MB instead of 0.9 MB). A crash before the re-deflate leaves a larger file that still loads |
| (b2) the deferred file: the proxy swallows the EXE's deflate and file writes, the server writes the whole file | `deflate`, `CreateFileW`/`WriteFile`/`CloseHandle` (or the CRT's) slots for the save's handle | no | about 0 | High. Until the server writes, no file or only a part of one is on disk. The EXE's own truncate of a user save's old file would need a temporary-name redirect. Readers and rotation must wait. Depends on the exact write pattern of the EXE's loop |
| (a) a writer thread in the EXE | The EXE's call of its writer is sent to a worker. The stream is copied first | **yes**, behind `FullExeOptimizations` (D228: own key, build table, bytes checked, off on mismatch, unknown build or CEG) | about 0 (the copy of the stream) | High. The EXE's stream, allocator, file-system object and Lua callers would be used from two threads. Every build needs its sites. CEG on 4390913 forbids it. Gains only about 0.05-0.1 s over (b1) |
| (c) a faster deflate with the same bytes | The `deflate` slot goes to zlib 1.2.3 rebuilt with a modern compiler, in the proxy | no | most of today's cost; the size of the gain is not known (perhaps 10-30 %) `[I]` | Low, but small and still synchronous. A fallback only if (b1) is refused |
| (d) a lower level for good (level 1) | `deflateInit_` level 1, no re-deflate | no | 0.15 s of Python's 0.47 s | The save files change (1.37 times larger, not byte-identical to stock, though they still load). Visible to players, so an `[Extras]` question for the owner at most. Not recommended |

A non-code-write way for the base product exists: (b1).

## 4. (b1) in detail, for the build ticket

1. **The save's stream only.** `deflateInit_` is redirected only while the product path is active. It passes level 0
   only on a thread that is "in a save". The autosave: the proxy serves the server's `ICvEngineUtility::AutoSave`
   crossing and marks the thread for the call's length. `UI.SaveGame` and quick saves: the EXE's Lua function calls the
   DLL's `Write` set itself (pregame, Game, Map, teams, players, supporting data). So the rule is "the first
   `deflateInit_` on a thread after it served a complete `Write` set, with level 6 and the default window". Every other
   deflate user in the EXE gets the real call unchanged. M0 confirms this order.
2. **The path.** The server must know which file to re-deflate. A file-open slot (whichever API M0 shows the writer
   uses), read while the thread is in a save, records the path. Another way: `GetFinalPathNameByHandleW` on the handle
   of the first `WriteFile` in the save. The proxy posts `(path, size, last-write time)` to the server after the save
   returns: a new one-way post, with no reply awaited.
3. **The server's job**, one worker thread in server.exe (64-bit, so no load on the EXE's 32-bit address space, bug
   0102). It waits until the file can be opened without write sharing, which means the EXE has closed it. It checks size
   and time against the post, reads the header, pregame and stored body (`srv_civ5save`), inflates the body, deflates it
   in the game's exact pattern, inflates the result again and compares, then writes `<name>.vp64tmp` in the same folder
   and renames it over the target with replace. If the target changed, was moved or deleted, or the check fails, the
   job is dropped with one log line. The stored file stays and loads.
4. **Order with the EXE.** The autosave rotation (`NumAutosavesKept`, the `prev` folder) renames and deletes older
   autosaves. Optional `MoveFileW`/`DeleteFile*` slots wait, with a bound, for a pending job on that path. Without them,
   step 3's "target changed" rule leaves a stored file in place. That is safe and only larger. Loading needs no wait,
   because the stored file loads. A rename blocked by an open reader retries until the reader is done.
5. **The end of a session.** At the proxy's normal shutdown the server finishes its queue, with a bound of a few seconds
   (about 0.5 s per M job `[I]`). On a kill or a crash the files stay stored and load. The next start removes orphaned
   `*.vp64tmp` files in the save folders.
6. **Disk.** If the save folder's free space is below 4 times the body plus 256 MB, the proxy leaves level 6 for that
   save (the base path), with one log line.
7. **The option.** `[Performance] SaveCompressLater` (name to be settled), 0 or 1. No code write, so it does not depend
   on `FullExeOptimizations`. At 0 no slot is touched. The default is the owner's call (D169). Recommendation: 1 once
   the build ticket's acceptance passes.

## 5. The measurement plan (the ticket's acceptance; oldpc quiet, grade A, by START-REQUEST; done, results in 6b)

Build 26577625, product path, options 2+3+4 with both quick modes on, a test-instance copy:

- **M1, the autosave's wait per M turn**, `TurnsBetweenAutosave = 1` on the copy, at least 20 autosaves. The `AutoSave`
  round trip and the `Write` handler time come from the server's `proxy-calls.tsv` and the proxy's `vpproxy-calls.tsv`
  (`perf/evidence/0504/4802/savesplit.py`), and the per-turn charge from `budget.save_s` (`turn-time-study.md`, instrument
  table). Report median and spread, and the EXE's part.
- **M2, `UI.SaveGame` on the huge save.** At least 10 saves with distinct names from a one-shot main-thread handler
  (metrics ledger G9's way), the main thread's block timed in Lua and from the file's settle time. Report median and
  spread. Delete the copies afterwards.
- **M3, the default autosave interval of a player's install.** What the game writes into a fresh `UserSettings.ini`
  (a test instance's profile with the file moved aside, backed up by hash and restored), and whether a human seat adds a
  second autosave per turn (`AutoSave_<turn>` against `AutoSave_Post_<turn>`). Known so far: the owner's Steam profile
  has 1 with 999 kept (read 2026-11-24; also in `research/sources/skills/vp-dll-dev-SKILL.md`). The headless reference
  instance had 10 with 5 kept (`research/sources/civ5-headless/saves.md` section 3). Nobody has recorded where either
  value came from, so neither is known to be the game's default.
- **M0, for the build ticket only:** a test-only import trace of one autosave and one `UI.SaveGame`: the
  `deflateInit_` arguments, the order of the `deflate` calls, the file API and the rotation calls, all described as
  behaviour (D83). No address of build 26577625 is written anywhere (D202).

Go or no-go from M1 to M3 by the ticket's rule: a go if the EXE's part per autosave, divided by the default interval,
is at least 0.3 % of an M or H turn.

## 6. Draft acceptance for the build ticket (b1), for the MC

- Option off: no import slot changed (checked at the proxy's start) and the game identical to main.
- Option on: every final save file is **byte-identical** to the one the same state gives with the option off. That
  means at least 10 M autosave pairs and 3 huge `UI.SaveGame` pairs. Offline, the corpus test (every `.Civ5Save` in the
  instances' folders, made stored and then re-deflated by the server's job) matches the original for every file that is
  not truncated.
- A stored file (the job held back by a test switch) loads in the EXE on the product path, on the 32-bit DLL with the
  patch off, and in the server's own loader, and plays 3 turns.
- The autosave's wait on M is **under 0.2 s**. `UI.SaveGame` on the huge save blocks the main thread **under 0.5 s**.
  Both on quiet oldpc, grade A, with spreads.
- Robustness: 20 turns with interval 1 and `NumAutosavesKept = 3`: no file lost, none corrupt, every remaining file
  re-deflated. Killing the server during a job keeps a stored file that loads, and the next start removes the
  `*.vp64tmp`. Below the free-space guard, level 6 is written inline.
- The game is identical, windowed against windowed (D164), option on against off.

## 6a. The MC's review of the design (MC #13103, 2026-11-24 16:0x EST)

Way (b1) was accepted for the measurement phase: it uses an import slot only (D228-clean), the EXE's own file can always
be loaded, and the server's re-deflate is checked by inflating before the rename. Way (a) is not pursued. **Any later
build of (b1) must meet these conditions:**

1. The rename is atomic. It is skipped when the file has changed since the EXE closed it (section 4 step 3).
2. A save made while the server is down or slow stays the EXE's stored file: valid, only larger.
3. Save compatibility (D239) is shown on the corpus: every corpus save, made stored and then re-deflated, equals the
   original, and a stored file loads in every client.
4. An option turns it off (section 4 step 7). At 0, no slot is touched.

Phase 2 is the measurement (M1 to M3, section 5) and the go or no-go. The build ticket is separate.

**Condition 3, offline part: met on the corpus** `[L, 2026-11-24 16:33-16:36 EST, workstation]`
(`perf/evidence/5032/corpus_stored.py`, `corpus-stored.txt`). The inputs were every `.Civ5Save` in the owner's
`ModdedSaves` and `Saves` (114, read in place) and the two 0438 scenario saves. For each, the stored form was made: the
file's own header and pregame bytes, then the body at level 0 in the game's pattern and 64 KiB pieces. The server's
`civ5save.exe repack` then re-deflated that stored form. Result: **115 of 116 byte-identical** to the original. The one
that differs is the truncated `AutoSave_Post_0026 BC-2960` of `server-host.md` section 9, which no writer can reproduce.
A stored form is about the body's size, a median 36 times the compressed file (3.85 GB against 153 MB for all 116), so
the free-space guard of section 4 step 6 (4 times the body plus 256 MB) covers it. The live part of condition 3, a
stored file loading in each client, is for the build ticket.

## 6b. Phase 2 results: the measurement and the go (2026-11-24, oldpc)

**Verdict: GO.** At the game's default interval the autosave costs **1.2 % of an M turn** against the value floor of
0.3 %. That counts the whole wait; (b1)'s reach alone is 0.7-0.9 % `[I]`. Every autosave also holds the game thread
0.37 s on M. A user save of the huge game holds the main thread 0.67 s.

**Deviation from the acceptance:** the acceptance names build 26577625. oldpc has only its own Steam EXE, build 4390913
(both jobs' `vpproxy.log`: "exe build: build 4390913 ... known and supported"), so every figure below is on 4390913.
The save path is the EXE's own code and the install's `zlib1.dll`. Whether 26577625 changes it was not measured
`[I: probably not]`. A confirmation on 26577625 needs a machine that has it: the workstation in an MC window, or the
laptop if its EXE is the new build.

| Measurement | Result | Setting and proof |
|---|---|---|
| **M1** the autosave's wait on the game thread per M turn (`budget.save_s@1`: the top thread inside the `AutoSave` call it made into the EXE, nested `Write` calls included, transport out) | **0.367 s ± 0.005 s** (95 % t over 6 run means), median 0.363 s, range 0.341-0.452 s over 60 turns. **12.0 % ± 0.24 % of the M turn** (3.056 s with an autosave every turn) | job `20261124-162709-f4e8`, 17:05-17:31 EST, arm `S4s5032:s5032_budget` (product as the installer lays it, options 2+3+4, Quick Combat and Movement on, observer seat, `TurnsBetweenAutosave` 1), M 151-160, 6 runs x 10 turns, every run exit 0. Quiet proof: alone, foreign 0.089 cores (limit 0.10). `undisturbed` is false only because the job's own six headed games run outside its process tree (pids and times are the six runs' launches, `quiet-testbed.md` 16 (c)). **Grade A** by the CPU criterion. `perf/evidence/5032/m1-f4e8.txt`, `m1-f4e8.json` (`m1.py --window 151-160`) |
| **M2** `UI.SaveGame` on the huge save, main thread | **0.671 s median** (mean 0.6715, sd 0.0078, 0.662-0.685 s, n 10); file 2.23 MB, settled 1.27-1.38 s after the arm | job `20261124-162823-8892`, 17:31-17:37 EST, `perf/evidence/5032/save_job.py` on TB-HUGE_0350 (turn 350), product path (set-proxy, ServerRenderer = 1, the composite). Quiet: alone, foreign 0.113 cores (just over the limit: svchost 18.5 s, Defender 10.2 s in 365 s). **Grade B.** The stock route's 1.64-2.0 s (2026-11-15) was another machine and route: not a pair. `perf/evidence/5032/m2m3-8892.txt` |
| **M3** the default autosave interval of a player's install | **`TurnsBetweenAutosave` 10, `NumAutosavesKept` 5**: what the EXE uses when `UserSettings.ini` lacks the keys (read in game after `OptionsManager.SyncGameOptionsCache`). The headless reference instance's 10 and 5 (`saves.md` 3) were these defaults. `ResetDefaultGameOptions` leaves the two values alone | same job, `[L]` |
| **M3** autosaves per human turn (interval 1) | **two**: `AutoSave_Post_<t>` when the human's turn ends (the DLL's `AutoSave(false, true)`, in the AI phase) and `AutoSave_<t>` when the AI phase is done (the EXE's own, before the next human turn). Each was 2.23 MB on the huge save | same job, 2 human turns (350, 351). Whether both follow the interval of 10 was not measured |

**Go or no-go by the ticket's rule.** Per M turn at the default interval of 10: 0.367 / 10 = 0.037 s, **1.2 % of the
3.06 s M turn**. The floor is 0.3 %, so this is a **GO**. (b1) removes the EXE's compression and keeps the DLL's
serialization in the server (0.11-0.15 s per M autosave on the workstation, P0.5). So its reach is 0.22-0.26 s per
autosave `[I]`, 0.7-0.85 % of the M turn at the default interval, still above the floor. With a human seat, the turn's
second autosave (`AutoSave_<t>`) is the EXE's own; the budget does not count it, so these figures understate a human's
cost if it also blocks a thread `[I]`. A player with an autosave every turn (the owner's setting) pays 12 % of an M turn.
On M2, (b1) leaves the stored write and the DLL's serialization: an estimated 0.15-0.25 s of the 0.67 s `[I]`. That is
within the build ticket's target of under 0.5 s.

## 7. Not verified

- The EXE's level-0 seconds are inferred from Python's zlib on a loaded machine. M0 and M1 measure them.
- That `UI.SaveGame` runs the DLL's `Write` set before `deflateInit_` on the same thread is inferred from the autosave
  split, not traced.
- Which file API the save writer uses, and whether a human-seat turn writes two autosaves, are open (M0, M3).
- Steam Cloud's handling of a save file renamed over within a second of its write was not looked at.
- Whether the EXE's loader limits the number of body pieces (a stored M body is 329 pieces instead of 14) was not read.
  The first acceptance load answers it.
