# Why the x64 DLL's LTO link does not repeat (ticket 0358)

Written 2026-11-15 by the implementer of ticket 0358 (account B, Opus 5.5, branch `task-0358-lto-link-repeat`).
Scripts and records: `evidence/0358/`. Workstation, clang-cl / lld-link 19.1.7, MSVC 14.43.34808, SDK 10.0.26100.0.
Source: the DLL pin 04b1211b8 as a `git archive` copy (fingerprint = `deps/dll.lock`), release configuration,
`SOURCE_DATE_EPOCH=1794587302` (the epoch 4252 used). Earlier work on the same symptom: `implementation-effectiveness.md`
section 7 (issue 0115), `../03-dll-engine-boundary/upstream-fix-batch-1.md` 8c (one-off 9045, issue 0131),
`../03-dll-engine-boundary/player-limits.md` 8.1 (issue 4252, on epic 0334's branch).

## 0. Result

**Corrected 2026-11-15 23:05 (ticket 0373) [L].** With `/mllvm:-dse-memoryssa-scanlimit=1000` on the link line, which
`tools/x64-dll/build.py` now gives every release link (`LTO_DETERMINISM_LINK`), the link **repeats**: 6 of 6 relinks of
one build's byte-equal inputs in 0358's flipping environment gave one DLL, `9C4C3C4D9636C911...1E0B`, while 3 of 6
control relinks without it flipped; two full `build_all.py --config release` builds with it (different folders, the
second with the pad in its parent environment) gave that same DLL, PDB and import library. The cause is now known in
mechanism: DSE's read walk for the store needs a scan budget of 156-160 against the default 150, and whether it fits
depends on the walk's order (MemorySSA use order, which varies between linker runs; section 5). The text below is
0358's and describes the link **without** the setting; it stays as the record of that link. (Replaced: "not
deterministic run to run" as the state of the build; it is the state of a release link without the setting.)

**Cause [L, 2026-11-15].** The whole-program (LTO) link is **not deterministic run to run**. lld-link chooses one of
two code variants of `CvPlayer::doInstantGreatPersonProgress` **by chance in each run**, even when everything is the
same: the link inputs, the folder, the command line and the environment.

- **The proof.** Three relinks of one build's byte-equal inputs, in one folder, with one environment (build A's plus one
  variable `C5_0358_PAD` of 40 digits), gave the two variants in turn:
  - `DCE69943...20CE` at 10:43 and 11:34;
  - `125E2E2A...BB96` at 11:41.

  These two DLLs are exactly 4252's pair (`m22` and `m22base`). They differ only in that function: 629 bytes of
  `.text` (the function and its funclets, RVA 0xD7BDB0, 7,936 B), 19 bytes of `.rdata`, and the `/Brepro` stamp. The
  function's two SHA-256 values are `481C242B...` and `D39349FF...`.
- **What the cause is not:** a path, the output folder or its length, a time stamp, the PDB or `/PDBALTPATH`, the link
  order, or an LTCG cache.
  - The link inputs and `link.rsp` were byte-equal in every run (copies of one build).
  - The folder lengths 32, 36, 40, 50 and 71 gave the usual variant.
  - `/Brepro` is set, and the 0131 repeat check found 0 stamped inputs.
  - No `/lldltocache` is given.
- **The environment changes the odds, but does not decide.**
  - Without the extra variable, 0 of 8 runs flipped: A's environment 4 times; the minimal one of section 2 at 4 folder
    lengths.
  - With it, 3 of 5 plain relinks flipped (5 of 12 with the dump runs of section 1): the 40 digits 2 of 3 times, 1 character once of 1, and 40 letters 0 of 2.
  - The build's environment is the caller's whole environment plus vcvars: a Claude session's ids and tokens, `PWD`,
    the gate's job id. So it differs between sessions, and with it the odds.
  - That fits every earlier observation as chance draws: 9045's pair differed, 0131's r3/r6 flipped and its double
    build agreed, and 4252's pair differed.
- **Where in LLVM [L, 2026-11-15 13:43]: dead store elimination (`DSEPass`) in the LTO optimisation pipeline.** Per-pass
  IR dumps of the one function from a usual run and a flipped run are identical through `MemCpyOptPass`. After
  `DSEPass`, the flipped run has deleted one store: `store i32 %4, ptr %11`, which comes before an `invoke` of
  `CvCityCitizens::GetSpecialistUpgradeThreshold` and is overwritten after the call returns. The usual run keeps it.
  Everything later follows from that one decision: SimplifyCFG's `select`, then other registers and instructions in the
  machine code (section 3.2).
  - Both outputs are correct: a store that DSE proves dead may be dropped or kept. The DLLs are expected to behave
    alike [I, strong: no game was run on the second variant].
  - How DSE comes to decide differently run to run was not found. A query result that depends on memory addresses or on
    walk limits is likely [I]. A similar case in LLVM's history: in 2021 Chromium reported DSE sometimes removing and
    sometimes keeping a store before a call, traced to the dominator tree's child order (reviews D110368 and D110292;
    https://reviews.llvm.org/D110368). That was fixed long before 19.1.7. Whether 19.1.7 has a related defect is not
    known [I].

**Consequence.**
- A release link of the pin is **not guaranteed to give one hash**, on this machine or any other.
- Two builds that agree do not prove that the next one will.
- The DLL's code differs between the variants in this one function only. In every pair seen so far (0131, 9045,
  4252, here), both variants come from the same IR; they differ in register and instruction choices.
- What the release driver's purity and parity checks compare instead: section 4.

**What was changed anyway (this branch).** `tools/x64-dll/build.py`:
- runs every DLL link with a fixed minimal environment: `link_env` = `LIB`, `SYSTEMROOT`, `TEMP` and `TMP` from the
  vcvars environment, and `PATH` = the LLVM folder + system32;
- names the response file relative to the link folder (`@link.rsp`), so that nothing of the caller and no folder
  reaches the linker process;
- records `link_env` (names, size, SHA-256) in `build-summary.json`.

This takes the session-dependent part out of the odds. 0 of 4 minimal-environment runs flipped, but that is not a proof
of repeatability. Tests: `tools/x64-dll/tests/test_link_env.py`.

## 1. Build A and the relinks [L, 2026-11-15]

**Build A.**
- Command: `research/06-tooling-and-workflows/evidence/0358/s01_build.sh` = `python -I tools/x64-dll/build_all.py --src
  C:/c5-64bit-work/0358/src --out C:/c5-64bit-work/0358/A/deps --config release --offline --jobs 8`.
- Tools: main f444c636 (before this ticket's change).
- Ran 09:59-10:12 EST (766 s; link 451 s, peak private 13.3 GB). Every step rc 0; repeat check OK (181 inputs, 531
  members, 0 stamped).
- DLL **`125E2E2AD09BE19648ECC9AAA04DF452AB97638536C8B23BF166C122D961BB96`**, 23,655,424 B. These are the same bytes as
  4252's `m22` build (tools 3dd1c972, on F:, another folder and session).

**The relinks.** `evidence/0358/relink.py` does each one:
- It copies A's `dll/obj`, `link.rsp`, `CvGameCoreDLL.def` and `stubs/*.lib` into `<dest>/dll` and `<dest>/stubs`.
- It runs `lld-link @link.rsp` in `<dest>/dll` with A's `vcvars-env.json`, without the gate's `C5_QUIETLOCK_JOB`, plus
  the changes named for the arm.
- One link runs at a time, under a link place (`perf/quietlock/linkplace.py`) and `quietlock.py run`.
- Series driver: `s02_relinks.sh`, with the arms files `arms1.txt` to `arms4.txt`. Records: `evidence/0358/relinks.jsonl`.
- "Function" is the SHA-256 of `doInstantGreatPersonProgress` with its funclets: RVA 0xD7BDB0, 7,936 B, taken from the
  map by `funcvariant.py`.

| Arm (end time EST) | Link folder (length) | Environment | DLL SHA-256 | Bytes differing from A | Function |
|---|---|---|---|---|---|
| s1-B-samelen (10:21) | `C:\c5-64bit-work\0358\B\deps\dll` (32) | A's (137 vars, 13,542 chars) | `125E2E2A...` | 0 | `481C242B...` |
| s1-Bbase-plus4 (10:28) | `...\Bbase\deps\dll` (36) | A's | `125E2E2A...` | 0 | `481C242B...` |
| s1-B-repeat (10:36) | `...\B\deps\dll` (32) | A's | `125E2E2A...` | 0 | `481C242B...` |
| **s1-B-envpad** (10:43) | `...\B\deps\dll` (32) | A's + `C5_0358_PAD=` 40 digits (138 vars, 13,595 chars) | **`DCE69943...`** | **652** | **`D39349FF...`** |
| s2-min-len32 (10:51) | `...\B\deps\dll` (32) | minimal (5 vars, 467 chars) | `125E2E2A...` | 0 | `481C242B...` |
| s2-min-len40 (10:58) | `...\Bbase1234\deps\dll` (40) | minimal | `125E2E2A...` | 0 | `481C242B...` |
| s2-min-len50 (11:06) | `...\Bbase12345678901234\deps\dll` (50) | minimal | `125E2E2A...` | 0 | `481C242B...` |
| s2-min-len71 (11:13) | `...\B-a-much-longer-folder-name-for-0358-xyz\deps\dll` (71) | minimal | `125E2E2A...` | 0 | `481C242B...` |
| s2-pad-samelen-othercontent (11:20) | (32) | A's + pad of 40 letters | `125E2E2A...` | 0 | `481C242B...` |
| **s2-pad-short** (11:27) | (32) | A's + pad `x` | **`DCE69943...`** | **652** | **`D39349FF...`** |
| **s3-envpad-repeat1** (11:34) | (32) | A's + pad of 40 digits (as s1-B-envpad) | **`DCE69943...`** | **652** | **`D39349FF...`** |
| s3-envpad-repeat2 (11:41) | (32) | the same | `125E2E2A...` | 0 | `481C242B...` |
| s3-letters-repeat1 (11:47) | (32) | A's + pad of 40 letters | `125E2E2A...` | 0 | `481C242B...` |

The runs below printed LLVM dumps (sections 3-3.2). The extra `/mllvm` options change the PDB, so the DLL hash
differs from A even when the code does not. The Function column shows the variant.

| Arm (end time EST) | Pad | Dump | DLL SHA-256 | Function |
|---|---|---|---|---|
| s4-dump1 (12:16) | 40 digits | every pass | `2D5A250A...` | `481C242B...` |
| s4-dump2 (12:49) | 40 digits | every pass | `2D5A250A...` | `481C242B...` |
| s5-ckpt1 (12:59) | 40 digits | checkpoints | `549DD5BB...` | `481C242B...` |
| **s5-ckpt2** (13:08) | 40 digits | checkpoints | `70775D42...` | **`D39349FF...`** |
| s5-ckpt3 (13:18) | `x` | checkpoints | `549DD5BB...` | `481C242B...` |
| **s6-ir1** (13:40) | 40 digits | IR passes | `DD273A71...` | **`D39349FF...`** |
| s6-ir2 (14:02) | 40 digits | IR passes | `AC7C92CA...` | `481C242B...` |

Counting every run with a pad, 5 of 12 flipped. Without one, 0 of 8 flipped.

## 2. The minimal link environment (this branch) [C, tests; L, relinks]

`link_env` in `tools/x64-dll/build.py`:
- `LIB`, `SYSTEMROOT` (else `WINDIR`), `TEMP` and `TMP` from the vcvars environment, looked up without regard to case;
- `PATH` = `C:\Program Files\LLVM\bin;<SYSTEMROOT>\system32`;
- nothing else. `LINK` and `_LINK_`, from which lld-link reads extra options, are never passed.

`link()` runs `lld-link @link.rsp` with it, in the output folder. `build-summary.json` records `link_env` (the names, the
size in characters, and the SHA-256 of the sorted `KEY=VALUE` block). The build log says `link environment (issue 0358):
LIB, PATH, SYSTEMROOT, TEMP, TMP only, sha256 <16 hex>`. The relink arms s2-min-* used the same five variables
(`relink.py --env-min`).

Tests: `python -I -m pytest tools/x64-dll/tests/test_link_env.py -q -p no:cacheprovider`, 6 passed on 2026-11-15:
- only the fixed keys reach the linker;
- session variables, `PWD`, the job id and a pad change nothing;
- `LINK` and `_LINK_` never reach it;
- names are matched without regard to case, with the `WINDIR` fallback;
- the record is stable;
- `link()` hands lld-link exactly `link_env(env)` and `@link.rsp`. This test is a negative check: a caller variable must be
  absent, and the test fails on the code before the change, which passed `env=env` and `@<absolute path>`.

## 3. Where in LLVM: the per-pass dumps [L, 2026-11-15]

Method:
- lld-link's own options `/mllvm:-print-after-all /mllvm:-filter-print-funcs=?doInstantGreatPersonProgress@CvPlayer@@...`
  print the function after every IR and codegen pass into `link.log`: 264 dumps, 143 MB.
- Runs `s4-dump1` (11:48-12:16, 1,712 s) and `s4-dump2` (12:16-12:49, 1,988 s), both with the 40-digit pad.
- The dump makes a link about four times slower.
- `evidence/0358/dumpdiff.py` splits each log into its dumps and names the first one that differs.

Result:
- **Both runs gave the usual variant.** The function's code was `481C242B...`. The DLL was `2D5A250A...`, which
  differs from A only in the debug directory's PDB signature (4 header bytes and 16 `.rdata` bytes), because the PDB
  records the extra options.
- **All 264 dumps are identical between the two runs.** That includes the 123 IR passes of the LTO pipeline and
  instruction selection, the scheduler and the greedy register allocator.
- So the dumps of two equal runs agree, and the printing did not itself disturb the IR. A run that flips is needed to
  find the pass: section 3.1.

### 3.1 Checkpoint dumps: the variants differ already in the optimised IR [L, 2026-11-15]

Runs `s5-ckpt1` and `s5-ckpt2` (`arms5.txt`, 40-digit pad, about 570 s each) printed only checkpoints: the end of the
IR pipeline (`annotation-remarks`), then `codegenprepare`, `x86-isel`, `machine-scheduler`, `greedy`,
`virtregrewriter`, `prologepilog`, `block-placement` and `unpack-mi-bundles`.

- ckpt1 gave the usual variant (`481C242B...`); ckpt2 flipped (`D39349FF...`, DLL `70775D42...`).
- `dumpdiff.py` finds the **first difference in the first checkpoint**, the IR at the end of the LTO optimisation
  pipeline. All later checkpoints differ as a consequence.
- **The difference.** Before the call `CvCityCitizens::GetSpecialistUpgradeThreshold`:
  - The usual variant keeps a conditional store of the function's argument `%4` into a local (`store i32 %4, ptr %11`
    in its own block, then a branch). The local is overwritten after the call (`store i32 %557, ptr %11`).
  - The flipped variant has dropped that store and turned the block into `select i1 %168, i32 %4, i32 0`.

  So one run proves the first store dead and the other does not. That is the kind of decision dead-store elimination,
  GVN or MemCpyOpt makes, with alias-analysis queries under walk limits [I]. The pass is named in 3.2.
- The register and instruction differences that 0131 and 9045 saw in the machine code follow from this one IR
  difference.

### 3.2 The pass: DSE [L, 2026-11-15 13:18-13:43]

Run `s6-ir1` (`arms6.txt`) printed the IR after every pass of the LTO optimisation pipeline, but no codegen dumps:
`/mllvm:-print-after=<the 51 IR pass names>` with the function filter, 121 dumps, 15 MB, 1,307 s. It had the
40-digit pad and **flipped** (`D39349FF...`, DLL `DD273A71...`). `dumpdiff.py --align` paired its 121 dumps with the
same passes in `s4-dump1` (usual variant, same pad):

- **Dumps 0-48 are identical:** every pass through `MemCpyOptPass`, including the inliner, SROA, GVN and LICM.
- **Dump 49, `DSEPass`, is the first difference.** In the block before the `invoke` of
  `?GetSpecialistUpgradeThreshold@CvCityCitizens@@QEBAHW4UnitClassTypes@@@Z`:
  - the usual run keeps `store i32 %4, ptr %11` and then `br label %543`;
  - the flipped run has only the branch.

  `%11` is overwritten in the invoke's normal successor (`store i32 %554, ptr %11`). So the store is dead unless `%11`
  can be read on the unwind path. One run proves that it cannot be; the other does not.
- 52 of the 72 later dumps differ, which follows from this.
- **Confirmed with identical options [L, 14:02].** Run `s6-ir2` had the same options and pad as `s6-ir1` (1,343 s) and
  gave the usual variant (`481C242B...`, DLL `AC7C92CA...`). `dumpdiff.py` on `s6-ir2` against `s6-ir1` finds the same
  thing: dumps 0-48 identical, and the first difference at dump 49, `DSEPass`, at the same store.

Not done: which DSE query differs, and a switch that makes DSE decide the same way every time. Candidate switches are
DSE's MemorySSA walk limits (`-dse-memoryssa-*`). Each would need at least 6 runs with the pad, about 1 h per setting,
to show 0 flips at a useful confidence. A newer LLVM may also behave differently.

## 4. Open points (2026-11-15; corrected 23:05 by ticket 0373)

- ~~A deterministic setting for DSE.~~ **Done (0373, section 5):** `/mllvm:-dse-memoryssa-scanlimit=1000`, the first
  candidate tried; the others (walk limit, both, `-dse-optimize-memoryssa=false`, a newer LLVM) were not needed and
  not tried. (Replaced: "Candidates: a `/mllvm:-dse-memoryssa-*` limit; DSE's escape analysis; a newer LLVM. ... Until
  then the link is not repeatable".) Still open: `deps/dll.lock` `repeat` is the MC's to change (text in 5.4), and
  whether other functions sit at another limit's edge is not proven (8 equal links of the whole DLL so far).
- ~~An upstream report to LLVM.~~ **Drafted (0373):** `evidence/0373/upstream-llvm-report-draft.md` with the reduced
  input `dse-scanlimit-edge.ll` (the function before DSE, cut from a `-print-before=dse -print-module-scope` dump rather
  than from `/lldsavetemps`, which would have given the module before the whole pipeline). Not sent: D95 (the owner's yes to
  the exact text), D130 (after the beta).
- No game was run on a DLL of the second variant. 4252's `m22base` (`DCE69943...`) was not played either.
- The release layout's DLL `29F58E17...` (0351, epoch 1794741052) was not rebuilt here. **Answered 2026-11-16 (ticket
  0391) [C, disassembly]:** it holds the **store-removed** variant: its `doInstantGreatPersonProgress` is instruction for
  instruction the S1 build's (moved by 16 B), and S1 always removes that store (5.2). An S1 build of the same source and
  epoch differs from it in 6 functions only, one instruction each, the rest moved by 0, 16 or 32 B
  (`../08-performance/dse-scanlimit-turn-time.md` section 2). (Replaced: "Which variant it holds is not known, and a
  rebuild could give either.")

Scratch: `C:/c5-64bit-work/0358/` (build A, the relink folders and the dump logs, 3.5 GB) was deleted at hand-back on
2026-11-15 at 14:05. The dump logs are not kept. `evidence/0358/relinks.jsonl` (20 arms) and `relinks.log` keep every
arm's result.

## 5. Ticket 0373: a DSE setting that decides the same way every run (2026-11-15)

Written by the implementer of ticket 0373 (account B, Opus 5.5, branch `task-0373-lto-dse-deterministic`). Scripts and
records: `evidence/0373/` (copies of 0358's scripts, paths changed to 0373; 0358's own files are not edited).

**Inputs.** Build A of 0373: `evidence/0373/s01_build.sh` (the pin 04b1211b8 as a `git archive` copy,
`SOURCE_DATE_EPOCH=1794587302`, tools at main 820fdc6f), 16:32-16:42 EST, DLL `125E2E2A...BB96`, the same bytes as 0358's
A and 4252's `m22` [L]. Relinks with `evidence/0373/relink.py` (0358's, unchanged but for its labels): A's
`vcvars-env.json` (this session's: 138 variables, 13,595 characters with the pad, by chance the same size as 0358's
s1-B-envpad) plus `C5_0358_PAD` = the 40 digits. Every relink folder is `C:\c5-64bit-work\0373\<3 chars>\deps\dll`
(34 characters).

**Why the scan limit first [I, from the LLVM 19.1.7 source, `llvm/lib/Transforms/Scalar/DeadStoreElimination.cpp`].**
The store `store i32 %4, ptr %11` goes to an `alloca`. For an alloca DSE skips the all-paths CFG check
(`isInvisibleToCallerAfterRet` is true for allocas) and needs only the read walk of `getDomMemoryDef`: a breadth-first
walk over the MemorySSA uses of the dead store's MemoryDef, through the `invoke` and into the unwind path (the EH
cleanup funclets that many invokes share), looking for a read. That walk stops with "keep" when
`ScanLimit < WorkList.size() - I`, that is when the *pending* part of the worklist outgrows what is left of the budget
(`-dse-memoryssa-scanlimit`, default 150, shared by every candidate of one killing store). The pending count depends on
the order of the walk, which is the use-list order of MemorySSA's accesses and the operand order of its MemoryPhis.
Neither is printed in an IR dump; both come from how MemorySSA was built and updated by the passes before DSE (LICM,
MemCpyOpt keep it). So two runs with identical IR before DSE can still walk in another order, and a walk that ends just
under the limit in one order can pass it in another. A limit large enough that the walk always completes takes the
order out of the decision. The other candidates, in order: `-dse-memoryssa-walklimit` (the upward walk, 90); both
limits; `-dse-optimize-memoryssa=false`; a newer LLVM (a toolchain change: MC first).

What makes the order differ between runs with byte-equal inputs is not shown. Windows' low-fragmentation heap places
allocations at addresses that vary from run to run, and the environment block's size changes the heap's early layout;
anything in the passes before DSE that iterates a pointer-keyed set or map would then vary with it [I, not traced].

### 5.1 The decision sits at the edge of DSE's scan limit [L, 2026-11-15 20:16-21:05, official opt 19.1.7]

**The input.** Relink `r01-predse` (`arms0.txt`, 20:16-20:57, 2,481 s; the pad; `/mllvm:-print-before=dse
/mllvm:-print-module-scope` with the function filter) printed the whole module as DSE received it for the function:
1.26 GB of text in `link.log`. That run happened to flip (function `D39349FF...`); the IR before DSE is the same in both
variants (section 3.2). `s06_cut.sh` cut it out (`cut_module.py`), `llvm-as` made a 220 MB module, `llvm-extract
--func=<the function>` the function with its declarations (87 MB with debug info), and `opt -strip-debug` the reduced
input: `evidence/0373/dse-scanlimit-edge.ll`, 2,761 lines, 166,678 B, with its ModuleID line replaced and nothing else
changed. Tools: the official LLVM 19.1.7 Windows archive `clang+llvm-19.1.7-x86_64-pc-windows-msvc.tar.xz` from the
llvm-project release page (845,236,708 B, SHA-256 `b4557b4f012161f56a2f5d9e877ab9635cafd7a08f7affe14829bd60c9d357f0`;
its five tools `opt`, `llvm-extract`, `llvm-dis`, `llvm-as`, `llvm-link` in `F:/c5-64bit-work/0373/llvm-19.1.7`). The
link itself still uses the installed `C:\Program Files\LLVM` 19.1.7, unchanged.

**DSE alone (`opt -passes=dse`, MemorySSA built fresh by DSE; `s07_opt_probe.sh`, `opt_probe.txt`).**

| `-dse-memoryssa-scanlimit` | runs | store `store i32 %4, ptr %11` | output |
|---|---|---|---|
| 150 (default), 151, 152, 155 | 5, 1, 1, 1 | kept | `7361001f...` every run |
| 160, 170, 200, 300, 500, 1000 | 1, 1, 1, 1, 1, 5 | removed | `2a4a77b0...` every run |

The two outputs differ in that one line and nothing else. So the read walk for this store needs a budget between 156
and 160 in this order, a few steps above the default of 150.

**The same IR, two MemorySSA histories (`s08_mssa_history.sh`, `repro_check.sh`, `repro_check.txt`).**
- `opt -passes='function(loop-mssa(licm),gvn,memcpyopt,dse)'`: MemorySSA built by LICM, updated by GVN and MemCpyOpt,
  then DSE, as in the LTO pipeline: the store is **removed**.
- The IR that this DSE received (the same pipeline without `dse`, 6 lines changed against the input), given to
  `opt -passes=dse` alone: the store is **kept**.
- The two outputs differ only in the store (and the ModuleID line). So identical IR into DSE gives either result, and
  what decides is MemorySSA's state: the use-list order of its accesses and the operand order of its phis, which IR dumps
  do not show.
- Each `opt` command is deterministic: 12 runs of each of 5 pipelines (`opt_flip_probe.py --pad`, the environment
  alternating between no pad, the 40 digits and `x`) gave one output per pipeline. In the linker it is not: the IR before
  DSE was identical in 0358's runs and the store went either way. What varies the MemorySSA history inside the linker
  process is not traced [I: some ordering by pointer value in a pass that updates MemorySSA; heap addresses vary between
  runs].

**Consequence for the setting.** A scan limit well above 160 removes the store whatever order the walk takes, as long
as the walk in any order stays under the limit: 1000 leaves a margin of more than six times. It does not remove the
cause; another function at another limit could still sit on an edge (none was seen: section 5.2 compares whole DLLs).

**The upstream report.** Draft: `evidence/0373/upstream-llvm-report-draft.md` (D95 wording; not sent, D130: after the
beta), attachment `dse-scanlimit-edge.ll`, commands checked by `repro_check.sh` on 2026-11-15.

### 5.2 Series 1: control against S1 = `/mllvm:-dse-memoryssa-scanlimit=1000` [L, 2026-11-15 20:59-22:31]

`arms1.txt`, driven by `s02_relinks.sh` (each relink through `quietlock.py run` and a whole-program link place), control
and S1 alternating; records `evidence/0373/relinks.jsonl` (13 lines with r01), `relinks.log`, tally
`tally-series1.txt` (`tally.py`).

| Arm (end EST) | Setting | Seconds | DLL SHA-256 | Function SHA-256 |
|---|---|---|---|---|
| c01 (21:07) | none | 484 | `125E2E2A...` | `481C242B...` |
| s1-01 (21:15) | S1 | 455 | `9C4C3C4D...` | `F1728198...` |
| c02 (21:24) | none | 524 | `125E2E2A...` | `481C242B...` |
| s1-02 (21:31) | S1 | 457 | `9C4C3C4D...` | `F1728198...` |
| c03 (21:39) | none | 449 | `125E2E2A...` | `481C242B...` |
| s1-03 (21:46) | S1 | 447 | `9C4C3C4D...` | `F1728198...` |
| **c04** (21:54) | none | 428 | **`DCE69943...`** | **`D39349FF...`** |
| s1-04 (22:01) | S1 | 438 | `9C4C3C4D...` | `F1728198...` |
| **c05** (22:08) | none | 403 | **`DCE69943...`** | **`D39349FF...`** |
| s1-05 (22:16) | S1 | 464 | `9C4C3C4D...` | `F1728198...` |
| **c06** (22:24) | none | 486 | **`DCE69943...`** | **`D39349FF...`** |
| s1-06 (22:31) | S1 | 441 | `9C4C3C4D...` | `F1728198...` |

- **Control: 3 of 6 flipped** (the second variant is again 4252's `m22base`, `DCE69943...`, 652 bytes from A). The
  control is established at the first 6 runs, so series 2 (`arms2.txt`, the control extended to 12) was not run.
- **S1: 6 of 6 give one DLL**, `9C4C3C4D9636C911...` (23,655,424 B), byte for byte, not only one function.
- **Which variant S1 gives.** S1 changes code elsewhere (11,496,927 bytes differ from A; the function moved by 16 B to
  RVA 0xD7BDA0), so its function hash is a third value. `evidence/0373/cmpvariant.py` disassembles the function in each
  DLL and compares the instructions with addresses and RIP displacements replaced: s1-01 = c04 (the store removed) in
  all 1,839 instructions; c01 (store kept, 1,841) differs in 1,469. So S1 always takes the store-removed decision, as
  the `opt` probes predicted (5.1).
- **Link time, an indication only (workstation, other teams' jobs running, not a PERFORMANCE.md measurement):**
  control 403-524 s (mean 462), S1 438-464 s (mean 450). No sign that the larger limit slows the link. No performance
  claim is made for the DLL either way.
- At the control's observed rate (3 of 6; 0358 had 5 of 12), 6 equal runs would come by chance with a probability of
  about 2 x 0.5^6 = 1/32 if S1 changed nothing. The stronger argument is the mechanism (5.1): with the limit at 1000 the
  walk for this store completes in any order.

### 5.3 The double build, purity and the save test with S [L, 2026-11-15 22:32-23:26]

- **Double build** (`s03_double_build.sh`, log `double.log`, comparison `cmp_double.sh` -> `double-compare.txt`): two
  `build_all.py --src C:/c5-64bit-work/0373/src --config release --offline --jobs 8` at `SOURCE_DATE_EPOCH=1794587302`,
  F1 into `C:/c5-64bit-work/0373/F1/deps` (22:32-22:45, 810 s; tools ab6ab891) and F2 into
  `.../F2-second-build-longer-folder/deps` with `C5_0358_PAD` = the 40 digits in the parent environment (22:45-22:58,
  773 s; tools 9f1e4666, which differs from ab6ab891 only in research files). Every step rc 0. **Equal:** the DLL
  `9C4C3C4D9636C911DDB2750890CFB36B5676192ED9EC9F98E71DEA4031EA1E0B` (23,655,424 B), its PDB `3E383431...`, its import
  library, `link.rsp` (with the setting), and the stub DLLs. Both builds' repeat check OK (181 inputs, 531 members, 0
  stamped) and the same `link_env` (`fe9449c6...`). The DLL is also byte-equal to the 6 S1 relinks of 5.2, where the
  option came after `@link.rsp` instead of inside it: 8 links, one DLL.
- **Purity** (`s09_validate.sh`, `proxy/tools/derive_purity.py --src C:/c5-64bit-work/0373/src --dll <F1 DLL>`,
  22:58): **143 of 143 audited rows kept** (114 methods, 29 Lua names), 0 dropped, bound to `9c4c3c4d...`; derived from
  76992d235996 for 04b1211b8d5e, as 0351's release. `evidence/0373/purity/report.tsv`, `purity-head.txt`.

### 5.4 Settings tried, the 32-bit build, the lock text, what is not shown

**Settings tried (each with its run counts).**

| Setting | Where | Runs | Result |
|---|---|---|---|
| none (control, 0358's pad environment) | relinks c01-c06 | 6 | 3 usual `481C242B...`, 3 flipped `D39349FF...` |
| `/mllvm:-dse-memoryssa-scanlimit=1000` (S1, **kept**) | relinks s1-01..06; builds F1, F2 | 6 + 2 | one DLL `9C4C3C4D...` in all 8 |
| scan limit 150, 151, 152, 155 | `opt -passes=dse` on the reduced input | 5, 1, 1, 1 | store kept every run |
| scan limit 160, 170, 200, 300, 500, 1000 | the same | 1, 1, 1, 1, 1, 5 | store removed every run |
| 5 pipelines, pad alternating | `opt_flip_probe.py`, 12 runs each | 60 | one output per pipeline (opt alone does not flip) |

Not tried, as S1 passed first: `-dse-memoryssa-walklimit`, both limits, `-dse-optimize-memoryssa=false`, a newer LLVM.
S1 does not switch DSE off; it lets DSE look further (more stores can be removed), so PERFORMANCE.md's turn-time check
for "DSE off" does not apply, and no performance claim is made: the DLL's code changed in many functions (11.5 MB of
differing bytes against A, mostly moved addresses) and its game speed was not measured. *(Corrected 2026-11-16, ticket
0391 [C]: against the release DLL `29F58E17` at the same epoch, an S1 build differs in **6 functions**, one instruction
each, plus frame offsets and padding; the other 56,531 of 56,551 `.pdata` entries are identical and only moved. Its
speed: `../08-performance/dse-scanlimit-turn-time.md`.)*

**The 32-bit build: not now; a fork change if ever [C: the fork's `build_vp_clang.py` read 2026-11-15].** The 32-bit
DLL is built by the fork's own `build_vp_clang.py`, which also compiles with `-flto` and links with `lld-link /LTCG`
(release), so the same DSE runs in its link and could in principle decide this way too. But the 32-bit build is not
byte-repeatable for other reasons already (absolute `__FILE__` paths, `__DATE__`/`__TIME__`, the link time:
`../03-dll-engine-boundary/upstream-fix-batch-1.md` section 8c, `release-driver.md` W7.5), and its pins are identified by their
recorded hashes, so a DSE setting there would make nothing repeatable and would change the 32-bit DLL's code and pins.
The script lives in the DLL fork: a change would be a fork branch off `i0-ship` and the MC's decision (AGENTS.md 3), and
would only pay once the other stamps are gone. Not proposed. (Corrected 2026-11-15 23:15: an earlier draft of this
paragraph said the 32-bit build links with MSVC's linker without LTO; the script shows lld-link with `/LTCG`.)

**`deps/dll.lock` `repeat`: the text proposed for the MC** (this ticket does not change the lock; the lock's DLL hashes
belong to the build without the setting, `29F58E17...`, and stay valid as a record of what was validated):

```
repeat      = REPEATABLE from tools <the merge of ticket 0373>: the release link line carries
              /mllvm:-dse-memoryssa-scanlimit=1000 (tools/x64-dll/build.py LTO_DETERMINISM_LINK). With it, 6 of 6 relinks
              of one build's byte-equal inputs in 0358's flipping environment and two full release builds in different
              folders and environments gave one DLL (9C4C3C4D... at epoch 1794587302), while 3 of 6 control relinks
              without it flipped (ticket 0373, 2026-11-15, research/06-tooling-and-workflows/x64-lto-link-repeatability.md
              section 5). The cause was LLVM 19.1.7's DSE deciding one store at the edge of its default scan limit in a
              run-dependent order. The release hashes above were built without the setting and are one of that link's
              two variants; a rebuild at this lock's tools commit can still give either. A build with the setting is a
              new candidate: its own purity file (0373: 143 of 143 kept) and its own save test and parity.
              Replaced (ticket 0373): "NOT repeatable: ... (ticket 0358) ...".
```

**Not shown.** That no other function of the DLL sits at the edge of some other DSE or optimiser limit: 8 links of one
DLL are evidence, not proof. What, inside lld-link, makes the MemorySSA order vary between runs. That S1's DLL plays
games the same as the validated release beyond the save test below; the whole-game parity (oldpc and laptop) was not
run for it.
- **Save test (D10, D24)** (`s09_validate.sh`: `tools/d10/run_d10.py --out C:/c5-64bit-work/0373/savetest --srv-bin
  C:/c5-64bit-work/0373/bin --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, 22:58:53-23:26:29, 1,656 s):
  **PASS**, tiny, standard and huge, parts 1-3, only the version-string exception. x86 direct = x64 direct in 30,666 /
  351,496 / 683,123 rows: the same counts as 0351's validated release (`29F58E17...`), so the setting did not change a
  game result in these games. The server bin was the release layout's `bin` (`server.exe` `5B254430...`; its
  `CvGameDatabase_x64`, `CvLocalization_x64` and `lua51_x64` DLLs are byte-equal to F1's) with F1's DLL and PDB in
  place of the release DLL; the x86 DLL the lock's `i0ship-26234371` (`F92881A3...`). AI logging off (D49). Report
  `evidence/0373/savetest/report.txt`. No game window and no crash dialog (no WerFault process at five checks during
  the run).
- Not run on this DLL: the whole-game parity on oldpc and the laptop, the offline proxy suites, and a release-driver
  candidate (`take_release.py`). It is a candidate by the ticket's checks only.

**Scratch (2026-11-15 23:36).** `C:/c5-64bit-work/0373` (build A, 13 relink folders, F1, F2, the save test, 6.9 GB) and
the LLVM archive and reproducer work files on F: were deleted at hand-back. Kept: the five official LLVM 19.1.7 tools in
`F:/c5-64bit-work/0373/llvm-19.1.7` (64 MB), for `repro_check.sh` and the upstream report. To rebuild the reproducer's
input: `arms0.txt` relink, then `s06_cut.sh` and `opt -strip-debug`.
