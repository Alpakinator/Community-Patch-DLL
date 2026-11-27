# Turn time of the x64 DLL with 0373's DSE scan limit (ticket 0391)

Ticket 0391, 2026-11-16. Ticket 0373 (merged 2026-11-15) added `/mllvm:-dse-memoryssa-scanlimit=1000` ("S1") to every
x64 release link (`tools/x64-dll/build.py` `LTO_DETERMINISM_LINK`) so that the whole-program link repeats. With the
larger limit LLVM's dead store elimination looks further, and the DLL's code differs from the release DLL `29F58E17...`
in many functions (11.5 MB of differing bytes, mostly moved addresses;
`../06-tooling-and-workflows/x64-lto-link-repeatability.md` 5.4). Its game speed was not measured. This file measures it
in the form of `docs/briefs/PERFORMANCE.md`.

## 1. Method

- **Arms.** `rel` = the release layout's bin `<pins_dir>/dlls/x64-04b1211b-pfdee656/bin` (`deps/dll.lock`
  `release`; DLL `29F58E17...`, server.exe `5B254430...`). `s1` = the same folder with only the DLL replaced by a DLL
  built with S1 from the same source (fork pin `04b1211b8`, plain copy by `git archive`), release, offline, at this
  branch's tools, with `SOURCE_DATE_EPOCH=1794741052` (the commit time of the lock's `tools` fdee65668, the epoch of the
  release DLL, so that `__DATE__`/`__TIME__` agree). The tools' changes since fdee65668 that touch the DLL build are
  0358's fixed link environment and 0373's S1; the 43-civilisation option of 4255 leaves the default (22) build as it
  was. PDBs not shipped.
- **Where.** The quiet testbed `oldpc` (i7-6700K), one quiet job through `tools/testbed/tb.py --host oldpc submit
  --quiet` (D106, COMMON 23); the owner's session logged on there (PERFORMANCE.md 4: 12 runs per arm for a claim under 1 %).
- **Series.** `perf/tbjobs/series.py`, one process at a time, arm order rotated per round, 12 runs per arm, AI
  logging off in both arms (the player's workload, D49), statistics logging as the pinned database sets it (D35):
  - huge: `TB-HUGE_0350.Civ5Save`, turns 350 -> 355 (the metric over turns 350-354);
  - Standard: new game Continents, map seed 1001, sync seed 2002, turns 0 -> 50.
- **Same game check.** `--compare rel,s1`: statscmp of round 1's stats.db of the two arms.
- **Statistics.** `perf/tbjobs/analyze.py --ref rel`: mean, sd, cv, 95 % CI per arm, the difference with Welch's
  interval and the smallest detectable difference.

Scripts: `../../perf/evidence/0391/` (`s01_build_b.sh`, `s02_prepare.sh`, `s03_submit.sh`; statistics `s04_analyze.sh`,
`sens.py`, `s05_facts.py`, `s07_check.py`; they ran from `C:/c5-64bit-work/0391/`, deleted at hand-back). The S1 DLL,
its PDB, `/MAP` file, `link.rsp` and build logs are kept in `F:/c5-64bit-work/0391/s1-dll/` (182 MB).

## 2. The builds and what S1 changes in the code [C, disassembly, 2026-11-16]

**Build B** (`perf/evidence/0391/s01_build_b.sh`, 02:14-02:24 EST, `build_all.py --config release --offline --jobs 8`,
every step of `build_all.json` rc 0; `link.rsp` carries `/mllvm:-dse-memoryssa-scanlimit=1000`): DLL
`CEFDFD4E2B526F3168049065238065E7548012AEA8B88A9903F3AA65BB7B250A`, 23,655,424 B (the size of 0373's `9C4C3C4D...`,
which was built at another epoch). Arm folders and every file's hash: `perf/evidence/0391/arms.sha256` (the five other
files of both arms are the release layout's: server.exe `5B254430...`, `CvGameDatabase_x64` `9C37B39E...`,
`CvLocalization_x64` `35863EDE...`, `lua51_x64` `30724D6D...`, `civ5save.exe` `409422EF...`).

**Per-function comparison** (`perf/evidence/0391/funcdiff.py`, report `funcdiff-rel-s1.txt`): `.text` split by the
`.pdata` entries (56,551 in both DLLs: functions, funclets, chained parts), both DLLs disassembled with the official
`llvm-objdump` (Intel syntax), addresses, RIP displacements and symbols replaced by placeholders, each entry's
instruction stream compared; names from B's `/MAP` file.

- `.text` is 19,477,366 B in A and 19,477,350 B in B (16 B less); every other section has the same size.
- **56,531 of 56,551 entries are identical** after the normalisation. Their code only moved: 33,344 entries by -16 B,
  1,030 by -32 B, 22,177 not at all. That is the "11.5 MB of differing bytes" of 0373 5.4: moved addresses and the RIP
  displacements that point across the moves, not other code.
- **20 entries differ: 6 functions and 14 of one function's funclets.** Each of the 6 functions has **one instruction
  fewer** in B; where the report shows it (4 of 6), it is **a store to a stack slot**, the kind of store 0373 5.1 says S1
  lets DSE prove dead. The frame layout and loop-alignment padding follow:

  | Function | A size / B size (B) | Removed store | Other differences |
  |---|---|---|---|
  | `CvGame::testVictory` | 7,397 / 7,381 | `mov dword ptr [rbp+0x49c], r14d` | one alignment `nop` form |
  | `CvPlot::changeBuildProgress` | 4,139 / 4,123 | `mov dword ptr [rbp+0x17c], 0` | 8 B frame slot moved (5 operands), `nop` form |
  | `CvTacticalAI::AssignBarbarianMoves` | 12,730 / 12,746 | one instruction fewer (2,934 -> 2,933 lines; not shown in the report's first 6 hunks) | 103 lines with other frame offsets; its 14 destructor funclets `dtor$583..600` differ only in the frame offset of their one `lea` |
  | `CvPlayerCorporations::GetCurrentOfficeBenefit` | 2,817 / 2,817 | `mov dword ptr [rbp+0x3c], 0` | `nop` form |
  | `CvGrandStrategyAI::DoTurn` | 8,300 / 8,300 | one instruction fewer (1,812 -> 1,811 lines; not in the first 6 hunks) | 8 B frame slot moved (22 lines) |
  | `CvUnit::initWithNameOffset` | 21,909 / 21,909 | `mov dword ptr [rbp+0x23c], edi` | `nop` form |

- **`CvPlayer::doInstantGreatPersonProgress` is identical in A and B** (B RVA 0xD7BDA0, A 0xD7BDB0; main entry 6,429 B,
  1,330 instruction lines in both, `gpvariant.py`; its funclets are among the identical entries). B is the
  store-removed variant (0373 5.2: S1 always removes that store), so **the release DLL `29F58E17...` holds the
  store-removed variant of that function** [C]. This answers the open point of
  `../06-tooling-and-workflows/x64-lto-link-repeatability.md` section 4 ("which variant it holds is not known").

What this predicts for speed [I]: six instructions fewer, each in a function that runs at most a few times per player and
turn (victory test, grand strategy, a barbarian move pass, a build-progress change, a unit's creation, a corporation
text), against several seconds of AI work per huge turn: far below anything a series can resolve. The remaining risk is
the moved code: a shift of 16 or 32 B keeps every 16-byte loop alignment, but changes which 32 B and 64 B windows hot
loops sit in (cache lines, the decoded-instruction cache), which can move a turn by a fraction of a percent either way.
The series measures that.

Normalisation limits: hexadecimal operands of 6 or more digits are treated as addresses, so a changed large immediate
would be missed; branch encodings (short against near) are compared as instructions, not bytes.

## 3. Result [L, 2026-11-16, grade A]

**Verdict: no measurable change.** The S1 build plays both games exactly as the release DLL (statscmp identical) and
at the same speed: huge **-0.21 % (95 % CI -0.92 % .. +0.50 %)**, Standard **+0.25 % (-0.38 % .. +0.84 %)**; neither
difference is significant, and the smallest difference each series could detect is about 0.94 % (huge) and 0.76 %
(Standard). Without the job's first run (the slowest of all, below), huge is +0.00 % (CI -0.57 % .. +0.57 %). No
regression beyond the interval, so nothing to post to the MC before the release. The code comparison of section 2
explains why: S1 removes six instructions from six functions that are not on any per-plot or per-unit path, and moves
the rest by 0, 16 or 32 B.

```
Metric / tool:        seconds per AI turn (mean of the host's per-turn times, boot and load excluded),
                      perf/tbjobs/series.py + analyze.py at branch task-0391-dse-turn-time 5cdaf28d (the job ran that commit)
Builds:               rel = <pins_dir>/dlls/x64-04b1211b-pfdee656/bin (deps/dll.lock release; DLL
                        29F58E174B354E02A3566841908E225F7678BB8A9BD1EA0774E1B560442A7B1D, server.exe 5B254430...)
                      s1  = the same folder with DLL CEFDFD4E2B526F3168049065238065E7548012AEA8B88A9903F3AA65BB7B250A
                        (fork 04b1211b8, tools of this branch with LTO_DETERMINISM_LINK, epoch 1794741052; section 2)
                      PDBs not shipped; every other file byte-equal (perf/evidence/0391/arms.sha256)
Game / config:        huge: TB-HUGE_0350.Civ5Save (sha256 c499d3c6...), 350 -> 355, metric over turns 350-354;
                      Standard: new game Continents, map seed 1001, sync seed 2002, 0 -> 50, metric over turns 0-49;
                      AI logging off in both arms (D49, the player's workload); statistics logging on with the D35
                      pragmas (pinned database bd9b8aa1...)
Series:               oldpc, quiet testbed job 20261116-022439-7aba (--quiet, the D106 equivalent of the quiet lock
                      on the testbed), 12 interleaved runs per arm per game, one process at a time, arm order rotated
                      each round; huge 02:25:04-02:40:39 EST, Standard 02:40:39-02:47:23 EST, 2026-11-16; 48 of 48
                      runs ok
Result huge:          rel 6.023 s/turn (sd 0.059, cv 0.99 %, median 6.011, 5.962-6.164, 95 % CI 5.985-6.061)
                      s1  6.010 s/turn (sd 0.037, cv 0.61 %, median 6.013, 5.965-6.075, 95 % CI 5.987-6.034)
                      s1 - rel = -0.0126 s/turn (-0.21 %), Welch 95 % CI [-0.0556, +0.0304], not significant,
                      detectable ~0.0565 (0.94 %); paired by round -0.0126, CI [-0.0525, +0.0273]
                      without rel round 1 (the job's first run, 6.164 s/turn, the slowest of all 24 runs):
                      rel 6.010 (sd 0.041, n 11); s1 - rel = +0.0002 s/turn (+0.00 %), CI [-0.0340, +0.0344],
                      detectable ~0.046 (0.76 %); paired (11) +0.0043, CI [-0.0100, +0.0186]
Result Standard:      rel 0.1308 s/turn (sd 0.0011, cv 0.81 %, 95 % CI 0.1302-0.1315)
                      s1  0.1312 s/turn (sd 0.0007, cv 0.53 %, 95 % CI 0.1307-0.1316)
                      s1 - rel = +0.0003 s/turn (+0.25 %), Welch 95 % CI [-0.0005, +0.0011], not significant,
                      detectable ~0.0010 (0.76 %); paired by round +0.0003, CI [-0.0007, +0.0013]
Boot+load, memory:    huge 8.83 / 8.82 s, peak working set 263 / 263 MB; Standard 10.22 / 10.25 s, 172 / 172 MB
Same game:            statscmp of round 1, rel against s1: huge 14 tables, 62,453 rows IDENTICAL (turns 350-355);
                      Standard 15 tables, 19,016 rows IDENTICAL (turns 0-50)
Machine load:         quiet proof alone=true, undisturbed=true, foreign 0.048 cores (limit 0.1; top MsMpEng 27.7 CPU s
                      over 1,342 s), no other job; machine CPU mean 14.5 % (huge) / 14.6 % (Standard), max 24.8 %;
                      free commit 30.1-30.2 GB at every start and end; the owner's session logged on there (637 of 637
                      samples), which is why 12 runs per arm (PERFORMANCE.md 4)
Profile before/after: not taken: nothing moved beyond the interval; section 2 names the code that differs
Why (not) moved:      six instructions fewer in six functions run a few times per player and turn; the rest of the
                      code is identical and moved by 0/16/32 B, which keeps 16 B alignment (section 2)
Correctness:          statscmp identical in both games (above); 0373's save test (run_d10.py) PASS on an S1 build
                      (x64-lto-link-repeatability.md 5.4); not repeated here
Ledger records:       48 c5-run/1 records in perf/evidence/0391/ledger/ (not ingested, D151)
```

- **The first run.** The job's very first run (rel, huge, round 1) took 6.164 s/turn, 0.064 s above the next-slowest
  rel run (6.100, round 9; the others 5.962-6.100, median of all rel runs 6.011), and pushed rel's cv to 0.99 %. Its
  excess is not on turn 350 (7.370 s against the other rel runs' mean 7.417) but on turns 351-353 (+0.27, +0.44,
  +0.17 s against their means). Why is not known: not the load of the save, and the quiet proof shows nothing else
  running [I: the first minutes of a job on that machine, e.g. caches or Defender scanning the newly shipped files].
  Both readings are given above; the verdict is the same either way. A later series on this machine could play one
  untimed warm-up run before the timed rounds.
- **Spread against 7010's set of 2026-11-12** (huge x64 off cv 0.17 %, nobody logged on then): this set's cv is
  0.5-1.0 % with the owner's session logged on and the slow first run. For orientation only, different set: rel's huge
  mean 6.023 (6.010 without the first run) against 7010's 5.915 on the older x64 pin `76992d235` (another DLL, server
  and pin batch; not comparable as a speed change).
- **What is not measured:** the 32-bit DLL (S1 is not in its build; x64-lto-link-repeatability.md 5.4), the product
  path through the proxy (the server's AI turn is the same code), frame time (the DLL's rendering-side calls are not
  among the six functions), and a second machine.
