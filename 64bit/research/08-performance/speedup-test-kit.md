# The speed kit: define-off identity, the identity job, verify defines, the D178 pair and coverage saves (ticket 4803)

impl-4803 (account B), 2026-11-17. The test kit of epic 0504 (P0 item 7 of `speedup-epic-proposal.md`; rules 2-4 of
its section 2 and the Architect's review sections 1-2). The commands are in `perf/speedkit/README.md`; this file holds
what the kit is built on, what its demonstration measured and what is not verified. Marks: `[C]` read in code or a
file, `[L]` seen in a run, `[I]` inferred. Pins: x64 `4f4daac70` (layout `x64-4f4daac7-pf8e9f42`, DLL `2D737BD5...`),
i0-ship `39f17f754` (COMMON 18, batch 3).

## 0. The result in brief

- **Item 1, define-off**: a speed branch's define-off release DLL is the pin's DLL **byte for byte except the link's
  build identity** (20 bytes), measured on the demo branch. Whole-file SHA-256 equality is impossible for any edited
  source file (section 2). The MC set rule 3 to "byte-identical except the build identity fields" on 2026-11-17 (thread
  4803, answer to question #4684). Every speed block must end with `#line N` or every later `__LINE__` moves.
  A control rebuild of the pin by the same steps gives the pin's SHA-256 exactly.
- **Item 2, identity job**: the define-on demo build is IDENTICAL to the pin on M, H, E and three coverage windows; the
  negative test (every 1,000th kept value wrong) is caught (DIFFERENT, exit 1, first table named). D24 of the define-on
  build fails on one column, `IsReligionOwner`, which is bug 0508 (an uninitialised read that any code-layout change
  moves), not the package: until 0508's fix is in the pin a package's D24 may fail there alone (section 3).
- **Item 4, verify defines**: `VP64SpeedVerify.h`; verify runs report 60.7 million checks and 0 differences on M (31.6
  million on E), and the seeded negative control is caught (1 spoil, 1 difference), with the game IDENTICAL in every
  verify run. One spoilt value never changed the game, so the identity job's negative test has its own
  `SPOIL_EVERY` control. Check counts differ by a few dozen between identical games: compare differences, not counts.
- **Item 3, the D178 pair** on `oldpc` with the memory after 10 turns: section 5 (the null pair).
- **Item 5, coverage saves**: Congress (M 180, H 305), special session (H 310) and the first corporation of M
  (founded at 242; save at 240), each checked from the save itself (the event turns can move by one from a save).

## 1. The demonstration package `perf/speed-kitdemo`

Fork branch `perf/speed-kitdemo` = `4f4daac70` + `0a4da3b23` (the header and the demo) + `37a95ffa9` (the
`SPOIL_EVERY` control; spoils flush the totals each turn); made in the clone `C:/c5-64bit-work/4803/fork` and pushed to
the fork on GitHub as that branch only. Never merged into a pin.

- `CvGameCoreDLL_Expansion2/VP64SpeedVerify.h` (new): the verify-define pattern (section 4).
- `CvGameCoreUtils.cpp`: behind `VP64_SPEED_KITDEMO`, `plotDistance(int, int, int, int)` answers from a per-thread
  (`thread_local`) direct-mapped table of 1,024 entries keyed by everything the original reads: both plots, the grid
  width and height and the two wrap flags. So an entry is exact for as long as it is kept, without invalidation. The
  original body is kept as `plotDistanceOriginal` inside the block. `_VERIFY` compares at every call and returns the
  original; `_SPOIL=n` stores the n-th computed distance plus 1. Package name in the logs: `KITDEMO`.
- Statics it adds (the two-thread question, Architect's review section 2): the table is `thread_local`, so nothing is
  shared between the ui thread and the game-core thread; the verify logger's statics (one `Log` object with its SRW
  lock, the per-package table, two `FILE*`) are behind that lock and exist only in verify or spoil builds. It is not a
  speedup candidate (plotDistance is a few integer operations; no timing was taken).

## 2. Item 1: the define-off byte check

### What a define-off build of a branch can and cannot equal `[C, L 2026-11-17]`

Three things stand between "the define is off" and "the DLL is the pin's":

1. **The version string.** `tools/x64-dll/build.py` writes `CURRENT_GAMECORE_VERSION` from `git describe` (`... Clean`)
   for a git checkout, or from `deps/dll.lock`'s describe (`... Copy`) for a plain copy, which it accepts only when the
   source fingerprint is the pin's. A branch has another describe and another fingerprint; its string has another
   length and moves every later byte of `.rdata`. `perf/speedkit/build_as_pin.py` imports `build.py` unchanged and
   replaces only its source check for that run, so a plain copy of the branch is built with the pin's string
   (`Release-5.4.8-57-g4f4daac70 Copy`) [C].
2. **`__LINE__`.** `PRECONDITION` (`CvGameCoreDLLPCH.h` line 83) and the logging macros expand `__LINE__` in release
   builds; `CvGameCoreUtils.cpp` has one at line 1084, below the demo's insertion [C]. Inserted lines move every later
   value. **Every speed block is followed by `#line N`** (N = the next line's number in the pin's file), which restores
   the numbering; the demo has two (`#line 506`, `#line 508`).
3. **The build identity.** Under `/Brepro` lld-link writes, instead of times, a hash of the PDB into the PDB GUID (its
   first 8 bytes; the other 8 are the text `LLD PDB.`) and a hash of the output into the PE `TimeDateStamp` and the
   debug directory's two entry time stamps (CODEVIEW, REPRO). The PDB holds a checksum of every source file, so any
   edit of a compiled file, a comment included, moves those 20 bytes [C: the measured fields below; cause I, from
   ticket 0159's finding that a stubs path recorded in the PDB moved the same fields].

### The demo's result `[L, 2026-11-17 18:44-18:56 EST, workstation; release, epoch 1794921170]`

`python -I perf/speedkit/defineoff.py --fork C:/c5-64bit-work/4803/fork --branch perf/speed-kitdemo --work
C:/c5-64bit-work/4803/defineoff2 --deps C:/c5-64bit-work/4803/defineoff/build --jobs 6` (the stubs of the first take,
built from the pin's plain copy at 18:39-18:40). Compile 84 s, link 410 s, link peak private 13,329 MB.

| | |
|---|---|
| branch DLL | `F42A7423932C01C37F023E790AF5448113ECD15309016E9FB49F12A8B23D658C`, 23,669,760 B |
| pin DLL | `2D737BD58EF4FD31829793C5992497FC366CDFC6A522789DA29429C9A597438B`, 23,669,760 B |
| differing bytes | 20, all build identity: PE `TimeDateStamp` (0x80), CODEVIEW and REPRO entry time stamps (0x13920cc, 0x13920e8), PDB GUID bytes 0-7 (0x1392104); `CheckSum` 0 in both, age 1 in both |
| verdict | `IDENTICAL-EXCEPT-BUILD-ID`: every byte of code, data, imports, exports and relocations equal |

**The final head `37a95ffa9`** (with `SPOIL_EVERY`), the same command with the default pass rule, 2026-11-17
20:44-20:52 EST: DLL `1239F2D88C36E912...`, the same 20 build-identity bytes differ and nothing else:
`defineoff: IDENTICAL-EXCEPT-BUILD-ID`, exit 0 (`perf/evidence/0504/4803/defineoff/final-37a95ffa9/`).

The control (the pin's own plain copy through the same steps, to show that the rest of the build reproduces the pin
exactly): section 2.1.

### 2.1 The control `[L, 2026-11-17 18:57-19:12 EST]`

The pin's own plain copy (`git archive 4f4daac70`, fingerprint the lock's) through the same DLL step
(`build_as_pin.py --commit 4f4daac70... --describe pin -- --config release --out .../build/dll-pin --stubs
.../build/stubs --src .../src-pin`, `SOURCE_DATE_EPOCH=1794921170`, the same stubs at the same relative path, no
`--shorten`): **SHA-256 `2D737BD58EF4FD31...97438B`, the pin's DLL exactly** (`dllcmp.py ... --expect-sha256 2D737BD5...`:
`IDENTICAL`; compile 91 s, link 513 s, peak private 13,329 MB). So the kit's build (wrapper, epoch, stubs, version
string, no narrowing gate) reproduces the pin bit for bit, and the 20 bytes of the branch build come from the edited
source alone. The MC's ruling of 2026-11-17 names this control as the proof that the masked comparison is sound.
Evidence: `perf/evidence/0504/4803/defineoff/` (both reports, the control's comparison).

### Known gap: the narrowing gate and `#line` `[L 2026-11-17 18:42]` (closed by ticket 4810, 2026-11-17 23:15)

**Corrected 2026-11-17 (ticket 4810, lead-0504):** with `--shorten`, `build.py` now compiles with
`-Xclang -fno-diagnostics-use-presumed-location`, so clang names physical lines and the gate keys a speed branch's sites
as the pin's: the demo branch `perf/speed-kitdemo` 37a95ffa9 (define off) gives 794 sites, 0 new, 4 gone, exactly the
pin's result (`perf/evidence/0504/4810/`, `tools/x64-dll/README.md` "Narrowing gate"). The flag changes no code (`-flto`
objects byte-identical). `defineoff.py` still builds without `--shorten` (unchanged); a define-on build of a package can
now run the gate to check its new code. The text below is the gap as found (kept as the trace).

`build.py --shorten` keys every `-Wshorten-64-to-32` site by the text of the source line the diagnostic names
(`narrowing_keys`). After a `#line`, clang names the pin's line numbers, but the gate reads the physical (shifted) lines
of the branch's file, so it saw 4 "new" and 8 "gone" sites in `CvGameCoreUtils.cpp` and failed the first take
(`C:/c5-64bit-work/4803/defineoff/build/dll/narrowing-new.txt`). So `defineoff.py` builds without `--shorten`; the
build is otherwise release's (`--shorten` adds only the warning and the gate: no code-generation flag, and the
reproducible build keeps the command line out of the DLL; the result above is the proof). A narrowing check of a
package's new code is therefore missing until the gate reads presumed locations; the MC gives that fix to lead-0504 as a
ticket of its own (2026-11-17).

## 3. Item 2: the identity job

### Design `[C, perf/speedkit/idjob.py]`

- One command, parameterised by the candidate DLL (`--dll`) and the server layout (`--layout-bin`, default the pin's
  layout bin from `deps/dll.lock`); the reference is the layout's own DLL (the pin's) unless `--ref-dll`. Each arm plays
  in its own copy of the layout bin with its DLL and PDB put in (4669's way), through `server/run_server.py` (gated,
  `jobcap` 16 GB), one run at a time, AI logging off, 0438's statistics-on client database (`9335186b...`), its
  localization database and the toolkit game folder: the inputs of 4669 and 4800.
- Windows: M (`TS0438-M-T150`, to 161, compared 150..160), H (`TS0438-H-T300`, to 306, compared 300..305), E
  (`server/specs/continents-standard-fixed-seed.json`, Standard Continents, map seed 1001 / sync 2002, to 51, compared
  0..50; 4800's early-game window), D24 (`tools/d10/run_d10.py` against `deps/i0.lock`'s 32-bit build with
  `--order-exception none --d18-x86 yes --d18-x64 yes`), and `--window NAME=SAVE:LO:HI` for coverage saves. Every run
  plays one turn past the window, because a run logs only part of the turn it ends in (0438's identity trace).
- Compare: 0438's `idtrace.py` method (rows outside LO..HI deleted in every table with a Turn column), then
  `tools/d10/statscmp.py`'s `compare` with `--from-turn LO --expect-turns LO HI` (plausibility: every per-turn table has
  rows for every turn). A difference is reported as the window, the first differing table (earliest first differing
  turn), its first differing column, row and the two values, and the list of differing tables.
- `--ref-from` reuses the pin's runs of an earlier job when the reference DLL and libraries, the database, the
  localization database and the window's input all have the same SHA-256, so a package's later candidates play only
  their own arm.
- Exit 0 only on `idjob: IDENTICAL`; 1 on a difference or a run that did not reach its turn; 2 on bad inputs.

### The demo's runs `[L, 2026-11-17, workstation (shared; headless runs, no timing claim), fast builds of fork 0a4da3b23 unless named]`

Evidence `perf/evidence/0504/4803/identity/`.

| job | candidate | windows | result | exit |
|---|---|---|---|---|
| id-on (19:27-19:35) | on (`B2A50D58...`) | M 150-160 / H 300-305 / E 0-50 | **IDENTICAL**: 15 tables 32,838 rows / 14 tables 125,210 rows / 15 tables 20,626 rows | 0 |
| id-on-rel (20:53-21:03) | on, **release** (fork 37a95ffa9, `DD5351E6...`; D107's one release confirmation) | M / H / E | IDENTICAL: the same rows as the fast build | 0 |
| id-spoilnv (19:36) | spoilnv (`_SPOIL=1000`, no verify) | M | IDENTICAL: the one spoilt value never reached a decision (section 4) | 0 |
| id-spoilevery (20:34-20:35) | spoilevery (fork 37a95ffa9, `_SPOIL_EVERY=1000`, `BD0CCD23...`) | M | **DIFFERENT** (the negative test): first `InstantYields` at turn 153, column `YieldTimes100` row 199, reference 3200 / candidate 3600; 11 tables differ | **1** |
| id-on-cong (19:46-19:50) | on | coverage windows CONG-M 180-183, CONG-H 305-308 | IDENTICAL: 14 tables 16,607 rows / 14 tables 82,186 rows | 0 |
| id-on-corp (20:43-20:44) | on | coverage window CORP-M 240-245 | IDENTICAL: 14 tables, 42,826 rows | 0 |
| id-on-d24 (20:08-20:34, 1,560 s) | on | D24 against `i0ship-39f17f75` (`13CFB6D1...`) | **FAIL**: tiny part 2 only (`ReligionBeliefYields.IsReligionOwner`, 334 of 1,135 rows from turn 30: 1 on x86, 0 on x64); tiny parts 1 and 3, standard and huge all three parts pass | 1 |

**The D24 failure is bug 0508, not the kit and not the demo's code** [I, strong]: `IsReligionOwner` reads a pantheon's
holy-city coordinates, which the `CvReligion` constructor leaves uninitialised for a pantheon, so their value is whatever
the founding stack frame held; any change of code layout (here the fast configuration plus the demo's code in
`CvGameCoreUtils.cpp`) can move it. 4801 found the same column, the same direction (1 on x86, 0 on x64) and the same
tiny/standard turns for its PGO build, and traced the defect (`speedup-p1-pgo.md` section 6; bug 0508; its fix is in
ticket 4805's batch). The pin itself passed D24 on 2026-11-17 (`deps/dll.lock` `verified`). So until 0508's fix is in
the pin, a package's D24 may fail on this column alone; the identity job reports it by name, and the package's report
says so. idjob now prints D24's per-game table and the first line of every failing part.

H's 125,210 rows and E's 20,626 rows are the counts 4800 compared on the same windows (on batch 2), M's 32,838 rows
are this job's M window with both runs played to 161. Wall times (shared machine, indications): M about 25 s per run,
H about 2 min 20 s, E about 25 s.

## 4. Item 4: the verify-define pattern

### The header `VP64SpeedVerify.h` `[C, fork 0a4da3b23]`

- A package `P` sets `VP64_SPEED_P` (the speed path), `VP64_SPEED_P_VERIFY` (check mode) and, for the negative
  control, `VP64_SPEED_P_SPOIL=<n>` (seeded: the n-th kept value the package stores is made wrong; the seed picks which).
- `VP64_SPEED_CHECK_K(pkg, site, fast, orig, k1..k4)` (integers, enums, bools: exact) and `VP64_SPEED_CHECK_D`
  (doubles: bit-exact, so -0/+0 and NaN payloads count) count every check and log every difference with the turn, the
  check number and four keys; `VP64_SPEED_SPOIL_NOW(pkg, n)` counts stores and fires once.
- **A verify build returns the original value**, so its game is the pin's (its stats.db must be IDENTICAL as well) and
  one wrong kept value cannot hide later ones by changing the game around them. The spoil applies in both modes: with
  VERIFY it must be caught; without VERIFY it must change the game (the identity job's negative test).
- Files in `%VP64_SPEED_DIR%` (else the working folder): `VP64SpeedTotals.csv` (package, checks, differences, spoils,
  stores, first difference turn, last turn; rewritten at every new turn and at unload), `VP64SpeedDiffs.csv` (the first
  10,000 differences per package), `VP64SpeedInfo.txt` (the defines compiled in; a package adds its three lines).
- Thread safety: one SRW lock around the counters and files; `Log` is a function-local static (built at the first
  check, destroyed at unload).
- Builds: the defines go in through the `CL` environment variable (`perf/speedkit/kitbuild.py`; clang-cl reads it, as
  4800's counting build did); a define-on DLL carries its branch's describe plus `+speed` in its version string. That
  the define took effect is visible in the PDB (`plotDistanceKept` is in the `on` build's PDB and not in the define-off
  build's) and, for verify builds, in `VP64SpeedInfo.txt`.

### The demo's builds `[L, 2026-11-17 19:12-19:27 EST, about 4 min each, fast configuration (D107: functional work), workstation]`

| build | defines | DLL SHA-256 |
|---|---|---|
| on | `VP64_SPEED_KITDEMO` | `B2A50D58DE2AAD0D...` |
| verify | + `_VERIFY` | `27729790FB2D8022...` |
| spoil | + `_VERIFY` + `_SPOIL=1000` | `6037245A01339C58...` |
| spoilnv | + `_SPOIL=1000` (no verify) | `D61E993DF8F88A05...` |

### The demo's verify runs `[L, 2026-11-17 19:36-19:39 EST, workstation, headless, fork 0a4da3b23 builds]`

`perf/speedkit/verifyrun.py` (each window through `idjob.py`, the pin's reference runs reused from the `on` build's
identity job by `--ref-from`); evidence `perf/evidence/0504/4803/verify/`.

| run | build | windows | checks | differences | spoils | game | verdict |
|---|---|---|---|---|---|---|---|
| clean | verify | M 150-160 | 60,671,483 | 0 | 0 | IDENTICAL (32,838 rows) | PASS (expect clean) |
| | | E 0-50 | 31,648,995 | 0 | 0 | IDENTICAL (20,626 rows) | |
| caught | spoil (`_SPOIL=1000`) | M 150-160 | 60,671,451 | **1** (turn 150, check 2,724: plotDistance(60,48 -> 58,39) fast 10, original 9) | 1 (store 1,000 of 21,648,456) | IDENTICAL | PASS (expect caught) |
| negative of the check | spoil | M 150-160, expect clean | 60,671,515 | 1 | 1 | IDENTICAL | **FAIL** (as it must) |

Findings:

- **The check count is not deterministic between identical games** (60,671,483 / 60,671,451 / 60,671,515 on the same
  window, all three IDENTICAL games) `[L]`. The totals are written at each turn change, so they cover turns up to the
  start of the last turn; the server ended without the DLL's static destructor writing the final totals (no file from
  the destructor in the no-verify spoil run) `[L]`, and the number of calls made around the turn loop's update ticks
  seems to vary `[I]`. So a package compares differences and spoils, never check counts, between runs.
- **One spoilt value does not change the game**: the spoilt entry of the direct-mapped table was read once (at its own
  store) and evicted before any reuse; the spoil build without verify (`spoilnv`, `_SPOIL=1000`) played an IDENTICAL M
  game (32,838 rows). A single wrong kept value is the verify mode's control, not the identity job's. So the header has
  a second control, `VP64_SPEED_<P>_SPOIL_EVERY=<n>` (every n-th store wrong; fork `37a95ffa9`), for the identity job's
  negative test (section 3).

## 5. Item 3: the D178 pair and the kit's noise floor on oldpc

### Design `[C, perf/speedkit/pair.py and tbpair.py]`

- `pair.py` (workstation) stages two arms, each a copy of one server layout bin (default the pin's) with its DLL and
  no PDBs, under `F:/c5-64bit-work/<issue>/pair-<time>/`, and submits `tbpair.py` with `tools/testbed/tb.py submit
  --quiet --expect-min N` (N from about 1.5 min + 0.12 min per turn per run on M, 2.5 + 0.45 on H [I]), shipping the
  arms, 0438's save and its two databases. It refuses with uncommitted kit changes (the job runs HEAD) and imports the
  job's modules first (EFFECTIVENESS 11).
- `tbpair.py` (the host, `%C5H_PYTHON%`) runs `perf/tbjobs/series.py` unchanged (interleaved rounds, order rotated per
  round, one process at a time, round 1's stats.db compared by statscmp, `series.json` and `turnbench.json`), with two
  changes from outside, as ticket 4801's `tbseries.py` did for the first: the start turn is the save's turn + 1 (the
  post-load turn is not timed; M 151..150+T, H 301..300+T), and each run records the server's working set and private
  bytes when each turn line appears (`memPerTurnMB`), `memAfter10MB` (private bytes when the 10th timed turn ended,
  D151's measure) and `memGrowthMBPerTurn` (least squares over the timed turns). It reads the GPU and driver into
  `gpu.txt` (D175).
- The last turn's line is written just before the server ends the game: its sample can catch the teardown (the local
  smoke of 2026-11-17 19:35 read 0.1 and 151.8 MB there against about 258 MB the turn before) `[L]`. So it is kept in
  `memPerTurnMB` but used for nothing, and `pair.py` asks for at least 11 timed turns.
- The local smoke (`perf/evidence/0504/4803/tbpair_local.py`: the job with a testbed-like environment on the
  workstation, M, 1 run per arm, 10 turns) ran clean before the oldpc submit: both arms ok, statscmp identical [L].

### The null pair `[L, 2026-11-17 21:15:22-21:15:59 EST, oldpc, quiet: alone, undisturbed, foreign cores 0.092]`

`python -I perf/speedkit/pair.py --a pina=<pin DLL> --b pinb=<pin DLL> --scenario M --runs 1 --issue 4803` (both arms
the pin's DLL `2D737BD5...` in the pin layout, server `ED829BDA...`), job `20261117-193736-a845` (the first submit,
`20261117-184935-4279`, was cancelled before it started to ship the teardown fix above). GPU NVIDIA GeForce RTX 2080,
driver 32.0.16.1088 of 2026-07-21 (D175). Evidence `perf/evidence/0504/4803/pair/oldpc-20261117-193736-a845/`
(`series.json`, `turnbench.json`, `analyze.txt` = `perf/tbjobs/analyze.py <out> --ref pina`, the quiet proof, the
statscmp).

| arm | s per AI turn, 151-165 | boot + load | memory after 10 timed turns (private) | growth, least squares 151-164 | peak private |
|---|---|---|---|---|---|
| pina | 1.0019 | 3.37 s | 252.3 MB | 0.55 MB/turn | 259.1 MB |
| pinb | 0.9928 | 3.36 s | 257.7 MB | 0.49 MB/turn | 259.6 MB |

- The two arms played the same game (statscmp of round 1: 15 tables, 52,688 rows, IDENTICAL).
- **Noise floor of one trial per arm: 0.9 % on M** (pinb 0.91 % faster than pina with the same DLL), so a D178 pair of
  2-3 trials per arm shows differences of a few percent; under about 1 % it needs grade A (PERFORMANCE.md: >= 6 runs
  per arm, 12 under 1 %). One M run takes about 18 s on oldpc (15 turns), so a 3-per-arm M pair is about 2 minutes of
  machine time; the planned length in `pair.py` is calibrated on this run.
- **The memory after 10 turns is one sample of a quantity that moves by several MB from turn to turn** (248-259 MB over
  the window in both arms); the two identical arms differ by 5.4 MB in that sample and by 0.06 MB/turn in the slope.
  A package's memory claim reads the slope and the per-turn series over all its runs, not one 10-turn sample [L].
- First turn after the load (151) is the fastest (0.77-0.79 s), the window's last ones about 1.0 s: M's turns grow
  within the window [L].

## 6. Item 5: coverage saves

### What the windows lack (4800's P0.6, `speedup-p0-measurements.md`, on batch 2) `[L by 4800]`

No World Congress session in M 151-160, H 301-305 or the early game 1-50 (M has one at 181-182, H at 306-307 and a
special session from 311); no corporation founded in any window (H has 7 from before 300; M to 200 and the early game
have none). Era changes and city-state friendship changes happen in every window, so they need no save. Batch 3
changes no game result against batch 2 (COMMON 18), so P0.6's turns hold for the batch-3 pin [I, the M and H windows
of section 3 are the same games].

### The saves `[L, 2026-11-17 19:39-19:47 EST, workstation; the pin layout x64-4f4daac7-pf8e9f42, headless, AI logging off, 0438's statistics-on database]`

`python -I perf/speedkit/coverage.py --out F:/c5-64bit-work/4803/coverage` (each: `run_server.py --load <0438 save>
--turns T --save-out NAME`; records in `coverage.json`, copied to `perf/evidence/0504/4803/coverage/`). Kept on F:
under the kit's own names, never committed (game files, D83).

| save | from | turn | window | event | SHA-256 | bytes |
|---|---|---|---|---|---|---|
| `SK4803-M-T180` | TS0438-M-T150 | 180 | 180..183 | Congress session, started 181, finished 182 | `62c2ec6d25b70866...` | 1,052,564 |
| `SK4803-H-T305` | TS0438-H-T300 | 305 | 305..308 | Congress session, started 306, finished 307 | `407390a9bbdbc5e8...` | 2,796,949 |
| `SK4803-H-T310` | TS0438-H-T300 | 310 | 310..313 | Congress special session, starts 311 | `b90844d6efa0c00d...` | 2,810,076 |
| `SK4803-M-T240` | TS0438-M-T150 | 240 | 240..245 | the first corporation of M, founded at 242; first franchise (section 6.1) | `927add02b0088a31...` | 1,282,289 |

In use (section 3, `id-on-cong`): the `on` build against the pin on CONG-M 180..183 (14 tables, 16,607 rows) and
CONG-H 305..308 (14 tables, 82,186 rows): IDENTICAL, both. Whether the saves still meet their events when loaded:
section 6.1. The corporation founding: section 6.2.

### 6.1 The events from the saves `[L, 2026-11-17 20:38-20:43 EST]`

Each save played through its window with 4800's counting DLL (`games3.sh` `check`; `EventTurns.csv` copied to
`perf/evidence/0504/4803/coverage/check/`). **A game continued from a save is not the game played on from the earlier
save** (D24's controls show the same: a continuation differs from the direct play from its first turn), so the event
turns can move by a turn; every event still falls inside its window:

| save | window | events seen from the save | in the direct play from 150 / 300 (4800, this ticket's scout) |
|---|---|---|---|
| `SK4803-M-T180` | 180..183 | Congress started **182**, finished **183** (3 enact proposals) | 181 / 182 |
| `SK4803-H-T305` | 305..308 | Congress started 306, finished 307 (3 enact proposals) | 306 / 307 |
| `SK4803-H-T310` | 310..313 | special session started 311, finished 312 (1 enact proposal) | started 311 (finish not seen: 4800's run ended at 312) |
| `SK4803-M-T240` | 240..245 | first corporation founded at 242, first franchise at **245** | 242 / 244 |

### 6.2 Corporations in M `[L, 2026-11-17 19:50-20:0x EST, workstation]`

Scout: 4800's counting DLL (`x64-count-0504`, `d06fb61d...`, built on batch 2 with `VP64_COUNT_EVENTS`; copied to
`C:/c5-64bit-work/4803/count0504/`) in a copy of the batch-3 pin layout, from TS0438-M-T150 to 350
(`C:/c5-64bit-work/4803/scout_corp.sh`; `EventTurns.csv` counts the corporations at each turn's end). **The first
corporation of M is founded during turn 242** (`corps_founded_end` 0 at 241, 1 at 242), **its first franchise at
244**. So the save `SK4803-M-T240` (window 240..245) covers both. Its event check and its identity run: 6.1 and
section 3. The early game (0-50) has no corporation and cannot have one (the technology is far later) [I].

## 7. Not verified

- The narrowing gate on speed code: not run (section 2, known gap; the gate fix is lead-0504's).
- The identity job's results were taken on **fast** builds (D107: functional work), except `id-on-rel`, the release
  confirmation of the define-on build on M, H and E; D24, the coverage windows and the negative tests were not repeated on
  release. The define-off check is release.
- The D178 pair with real arms (a package against the pin) and on H: only the M null pair ran.
- `idjob.py --ref-from` across machines or after a pin move (it compares SHA-256 of the reference DLL, libraries,
  databases and inputs, not the tool commit).
- The two-thread hazard: the kit runs headless only; the windowed identity pair and the HI-sweeper soak before any
  default-on (the Architect's review section 2) are not part of the kit.
- The Architect's long verify game for P6/P13 (0 to 300+ with verify on): the kit runs it as windows; no such run was
  made here.
- The client (S4) path: `idjob.py --layout-bin` takes a server layout but plays headless only.
