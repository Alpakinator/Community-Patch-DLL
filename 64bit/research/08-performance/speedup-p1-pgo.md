# P1: profile-guided optimisation (PGO) of the x64 DLL, as a measurement (ticket 4801, epic 0504)

Ticket 4801, account A, impl-4801, started 2026-11-17 15:06 EST. Plan: `speedup-epic-proposal.md` P1 and section 9;
the Architect's review `research/07-architecture-decisions/architect-2026-11-17-speedup-epic-review.md` section 3 wins
where they differ. P1 measures what PGO gains and whether a profile can be a locked, path-free, reproducible build
input. Making `release` consume a profile is not this ticket's (a later ticket in the build's own line); the
`release` configuration's bytes do not change here. Marks: `[C]` confirmed in a file, a dump or a tool's output, `[L]`
measured live, `[I]` inferred.

Toolchain: clang-cl / lld-link / llvm-profdata 19.1.7 (`C:\Program Files\LLVM\bin`), profile runtime
`lib\clang\19\lib\windows\clang_rt.profile-x86_64.lib` (present [C]). DLL pin 04b1211b8 (`deps/dll.lock`).

## 1. Design (written before the first build)

### 1.1 Two new build kinds in `tools/x64-dll/build.py`

| | `release` (unchanged) | `pgo-instr` (new, marked) | `release` + `--profile FILE` (new input) |
|---|---|---|---|
| compile | `/Ox /Ob2 /Zo -flto` | release's + `/clang:-fprofile-generate` (IR instrumentation, no file name compiled in) | release's + `/clang:-fprofile-use=<copy of FILE in <out>/gen>` |
| defines | release's | release's | release's |
| link | `/LTCG /OPT:REF /OPT:ICF` + 0373's DSE limit | release's + the profile runtime library + `gen/vppgo_flush.obj` + marker | release's + a `.vppgo` data section naming the profile's SHA-256 |
| link place (D107/D121) | yes | yes (whole-program link) | yes |
| marker | none | `.vpcfg` `VP-BUILD-CONFIG=pgo-instr` (refused for timing, as `fast`) | `.vppgo` `VP-BUILD-PROFILE=<sha256>` (timing allowed: it is the measured arm) |
| `build-summary.json` | as before | `config: pgo-instr`, `for_timing: false`, `instrumented: true` | `profile: {sha256, bytes, name}`, `pgo_diagnostics` counts |

Why IR instrumentation (`-fprofile-generate`) and not front-end instrumentation (`-fprofile-instr-generate`): the IR
counters are placed after the early simplification passes, so they are fewer and the hashes depend on the optimised CFG
of the release compile; it is what the proposal names and what LLVM recommends for optimisation (front-end counters are
for coverage) [I]. No `=<dir>` on `-fprofile-generate`: a folder given there would be compiled into the DLL as its
default output name (an absolute path in a DLL); the output name comes from `LLVM_PROFILE_FILE` at run time instead.

### 1.2 Writing the profile: the server ends with `TerminateProcess`

`server/src/srv_main.cpp` ends every headless run with `TerminateProcess(GetCurrentProcess(), ...)` after its own
shutdown (line 1210: "The DLL's static destructors and threads are not run: leave at once") [C]. The profile runtime
writes its file from an `atexit` handler, which `TerminateProcess` never runs. Experiment
`C:/c5-64bit-work/4801/exp1` (2026-11-17 15:11-15:14 EST, a 10-line DLL and a host that calls it and ends with
`TerminateProcess`) [L]:

| Variant | Result |
|---|---|
| A: plain `-fprofile-generate` | the runtime creates the file at load and never fills it: **0 bytes** ("empty raw profile file") |
| B: continuous mode (`LLVM_PROFILE_FILE=...%c...` + `-mllvm -runtime-counter-relocation`) | block counters correct without value profiling; **with an indirect call or a `memcpy` in the DLL the file is "truncated profile data"** (value-profile records are only written at exit) |
| C: plain + an explicit `__llvm_profile_write_file()` before `TerminateProcess` | complete profile: block counts, indirect-call targets, memcpy sizes |

So the instrumented DLL writes the profile itself, from `tools/x64-dll/pgo/pgo_flush.c`, an object linked into the
`pgo-instr` DLL only: at load (a `.CRT$XCU` initialiser) it replaces the host EXE's import-table entry for
`kernel32!TerminateProcess` with a function that, when the process ends itself, calls `__llvm_profile_write_file()`
once and then the real `TerminateProcess`. No change to the server or to the DLL's sources; the release and fast DLLs
do not contain it. Continuous mode (B) was rejected because the DLL's virtual calls (indirect-call promotion) and
`memcpy` sizes are a large part of what PGO uses.

A further finding of the same experiment [C, `llvm-profdata show`]: a function with internal linkage is named
`<source file as given to the compiler>;<name>` in the profile (`lib.c;helper_static`). `build.py` compiles every TU in
the source folder with its relative name (issue 0115, `source_cwd`), so the DLL's static functions should carry names
like `CvGameCoreDLL_Expansion2\CvFoo.cpp;?name@@...`: checked on the real profile in section 4.

**`-ffile-prefix-map` does not reach profile names** [C, `C:/c5-64bit-work/4801/exp1/run3.bat`, 2026-11-17 15:40]: the
same 10-line DLL compiled by its absolute path with `/clang:-ffile-prefix-map=C:\c5-64bit-work\4801\exp1=VPSRC` still
names its static functions `C:\c5-64bit-work\4801\exp1\lib.c;helper_static`. Only the file name as given on the
compile line decides. So the relative compile of issue 0115 is what keeps the profile path-free and folder-independent;
a build that compiles by absolute path would leak the folder into the profile and lose every static function's
profile in another folder, silently. `check_profile.py` failed that profile (drive path in 2 names) while
`tools/export/check.py` passed it: the export check looks for user-folder paths (`C:/Users/<name>`), not for every
absolute path, so it alone would not have caught a build-folder path.

### 1.3 Training

Headless `server.exe` of the pin layout `x64-04b1211b-pfdee656/bin` with the instrumented DLL in place of the release
DLL (a copy of the layout under `C:/c5-64bit-work/4801/`), AI logging off (`--dll-log-flags 0`), job-object cap,
through `server/run_server.py` (gated). Training set (proposal P1): H 300-305 and M 150-160 from 0438's saves
(`TS0438-H-T300.Civ5Save`, `TS0438-M-T150.Civ5Save`, copied), and the Standard fixed-seed game
(`continents-standard-fixed-seed.json`, the D24 test's standard game) 0-50. `LLVM_PROFILE_FILE` =
`F:/c5-64bit-work/4801/raw/<set>-%p.profraw`; `llvm-profdata merge` of the three into
`F:/c5-64bit-work/4801/profile/vp64-04b1211b8.profdata`; its SHA-256, size, training set and tool versions in
`tools/x64-dll/pgo/pgo.lock`. The profile file itself is not committed (lead-0504, MC #4429 (2)).

### 1.4 Checks

(a) `llvm-profdata show --all-functions`: no absolute path, no user name in any function name; (b)
`tools/export/check.py` on the profile file; (c) two `release --profile` builds in two folders (C: and F:, different
path lengths) give the same DLL SHA-256; (d) the profile's matched/mismatched counts on the 43-civ build and on the
plain 22-civ build, from the compiler's PGO diagnostics.

### 1.5 Identity and timing

Identity (D164): the profile-built DLL against the pin's release DLL `29F58E17...` (`deps/dll.lock`), `stats.db`
IDENTICAL on M 150-160, H 300-305 and the Standard 0-50 (`perf/evidence/0438/idtrace.py`, `tools/d10/statscmp.py`),
and the D24 save test (`tools/d10/run_d10.py`) PASS. Timing: grade A on `oldpc` (`tools/testbed/tb.py submit --quiet`,
`perf/tbjobs/series.py`, per-arm DLLs), release against release + profile, >= 6 interleaved runs per arm, H 300-305 and
M 150-160, sampler profile per arm, GPU driver named, memory after 10 turns per arm.

## 2. Builds and link memory

All from a plain copy (`git archive`) of the pin 04b1211b8 at `C:/c5-64bit-work/4801/src` (fingerprint matches),
`SOURCE_DATE_EPOCH=1794741052` (the pin's tools commit fdee65668, as `deps/dll.lock` `epoch`), `--shorten --jobs 8`,
on the workstation while other teams ran work (times are indications; memory peaks do not depend on load), tools of
branch `task-4801-pgo`.

| Build | When (EST, 2026-11-17) | Command | Link | Link peak private | Peak working set | DLL |
|---|---|---|---|---|---|---|
| release (own, the timing base) | 15:17-15:31 | `build_all.py --config release --offline` (stubs, libraries, DLL, smoke OK) | 538.6 s | **13,357 MB** | 12,765 MB | 23,666,688 B, `27365428...5730` |
| pgo-instr | 15:33-15:47 | `build.py --config pgo-instr --stubs rel/stubs` | 598.4 s | **16,323 MB** | 15,915 MB | 48,802,304 B, `bf8d4bd3...3fc4` |
| release + profile, build 1 (C:) | 15:53-16:05 | `build.py --config release --profile F:/.../vp64-04b1211b8.profdata --stubs b1/stubs` | 564.6 s | **14,662 MB** | 14,117 MB | 24,720,384 B, `076323c3...5ae7` |

[L] `C:/c5-64bit-work/4801/{rel/dll,instr}/build.log` and `build-summary.json`. Findings:

- **The instrumented link needs 16.3 GB of private memory at its peak, 3.0 GB (22 %) more than release's 13.4 GB**,
  and about 10 minutes (release 9). It takes the one whole-program link place; with 64 GB and one place that is
  enough (D107/D121); it must never run beside another whole-program link.
- The instrumented DLL is 2.06x release's size (counters, the names section, the profile runtime). Its compile took
  172 s wall for 173 TUs (sum 798 s) against release's (same machine, not measured in the same quiet).
- The own release DLL is not the pin's `29F58E17...` (23,655,424 B): the tools moved since fdee65668 (the
  closed-library stub `CvWorldBuilderMap` gained a map writer, `git diff fdee65668 -- tools/x64-dll/closedlib-stubs`)
  and 0373's DSE setting is on every release link since; both arms of the timing series are built here with the same
  stubs, so the comparison isolates the profile. Identity is checked against the pin's DLL (section 5).
- **The profile-consuming release link peaks at 14.7 GB, 1.3 GB (10 %) above the plain release link**, and its DLL is
  1,053,696 B (4.5 %) larger: the profile makes LTO inline and unroll more in hot code (and less in cold) [C size; the
  reason I]. `build-summary.json` of build 1 records `profile.sha256 e06e4628...`, `dll_profile` (read back from the
  DLL's `.vppgo` section) equal to it, `for_timing: true`, and no PGO diagnostic (no hash mismatch, no function missing
  from the profile: the profile was trained on this source).
- `repeatcheck` passes for pgo-instr too: the profile runtime's library members carry no time stamps [C].
- The compiler reports no PGO diagnostics on the instrumented build (none expected) [C].

## 3. Training and the profile

`tools/x64-dll/pgo/train.py` 2026-11-17 15:48-15:53 EST on the workstation (headless, gated, one run at a time),
instrumented DLL `bf8d4bd3...` in a copy of the pin layout `x64-04b1211b-pfdee656/bin` (server `5B254430...`), AI
logging off. The flush object worked on the real server: each run left one complete raw profile [L].

| Window | Input | Instrumented run | Raw profile |
|---|---|---|---|
| M 150 -> 160 | `TS0438-M-T150.Civ5Save` (`98fc454a...`), 0438's statistics-on database (`9335186b...`) | passed, 2.579 s/turn (release about 1.4-1.6 on this machine, an indication) | 13,333,712 B |
| S 0 -> 50 | `continents-standard-fixed-seed.json` (civ5-headless), the release descriptor's test database | passed, 0.369 s/turn | 13,315,088 B |
| H 300 -> 305 | `TS0438-H-T300.Civ5Save` (`308a96bb...`), the same database as M | passed, 43.25 s/turn (release about 21: the instrumented DLL runs about 2x slower) | 13,352,640 B |

`llvm-profdata merge` of the three: **`F:/c5-64bit-work/4801/profile/vp64-04b1211b8.profdata`, 13,502,368 B, SHA-256
`e06e4628889d31af2e9932dcf7a2d7f13ea66587fe5f1ca0387c711e438afc46`**; 49,286 functions, of which 13,226 (26.8 %) ran in
training; 509,064 counters; 1,646 blocks hold 90 % of all counts (`llvm-profdata show --detailed-summary`, kept in
`C:/c5-64bit-work/4801/check/detailed-summary.txt`). Lock data (pin, SHA-256, size, the training set with each raw
file's SHA-256, LLVM 19.1.7, the profile runtime's SHA-256): `tools/x64-dll/pgo/pgo.lock`. The profile file is not
committed (MC #4429 (2)).

## 4. The four checks

### 4a. No path or user name in the profile [C, 2026-11-17 16:02]

`python -I tools/x64-dll/pgo/check_profile.py F:/c5-64bit-work/4801/profile/vp64-04b1211b8.profdata --work
C:/c5-64bit-work/4801/check` -> **OK**:

- `llvm-profdata show --all-functions`: 49,286 names; **no drive path, UNC path, `Users`/`home` folder, user or machine
  name** (whole words, as `tools/export/check.py` matches them). 2,870 functions with internal linkage carry a file
  prefix, in 28 files, **every prefix relative under the source folders**, e.g. `CvGameCoreDLL_Expansion2\CvCity.cpp`
  (708 names), `CvGameCoreDLL_Expansion2\CvAStar.cpp`: the build's relative compile (issue 0115) gives them.
- The raw bytes (Latin-1 and UTF-16LE views): no path with a folder name after a drive, no user or machine name. (A
  lone `M:/` and `m:\` occur by chance among hashes and counters; the check needs a folder name after the drive.)
- Negative controls (the 10-line DLL, `exp1/run3.bat`, `run4.bat`): compiled by its absolute path, the static
  functions are named `C:\c5-64bit-work\4801\exp1\lib.c;...` (check FAILED, drive path); compiled from a folder under the
  user's home, `C:\Users\<name>\...\lib.c;...` (FAILED on every detector: drive path, `Users` folder, user name, and
  `tools/export/check.py` on both forms).

### 4b. `tools/export/check.py` reads the profile [C]

It reads the binary file (it searches files with NUL bytes as Latin-1 plus UTF-16LE, every finding at line 0) and
reports **one false positive**: the e-mail rule matches a mangled name `...QEBAH_N@Z` followed by the random bytes
`.dB` (offset 9,884,920). On the text form of the same profile (`llvm-profdata merge --text`: every name, hash and
counter as text, no random bytes) it **passes with 0 findings**. What it would need to gate a binary profile: run it on
the text form (as `check_profile.py` does), or a rule in check.py that converts `.profdata` with `llvm-profdata merge
--text` before searching; the e-mail rule cannot be made reliable on random bytes. The export check looks for user-folder
paths only, so a build-folder path (`C:\c5-64bit-work\...`) passes it: `check_profile.py`'s name check is the one that
catches that (section 1.2).

### 4c. Two folders, the same DLL bytes [C, 2026-11-17]

| | Build 1 | Build 2 |
|---|---|---|
| Output folder | `C:/c5-64bit-work/4801/b1/dll` | `F:/c5-64bit-work/4801/b2/dll` (another drive, another path length) |
| Source copy | `C:/c5-64bit-work/4801/src` (`git archive` of 04b1211b8) | `F:/c5-64bit-work/4801/src2` (a second `git archive`) |
| Profile given as | `F:/.../profile/vp64-04b1211b8.profdata` | `F:/.../b2/in/copy-of-the-profile.profdata` (another name and folder) |
| Stubs | `b1/stubs` (copy of `rel/stubs`) | `b2/stubs` (copy) |
| When (EST) | 15:53-16:05 | 16:17-16:27 |
| Link | 564.6 s, 14,662 MB | 470.2 s, 14,666 MB |
| DLL | **24,720,384 B, `076323c3a9d7324a9fe7b3dd21989c7eb54f1178b01a654d99e90c25ee865ae7`** | **the same bytes** |

Same epoch, tools and command otherwise. So the profile is a reproducible build input: the compile reads its copy in
`<out>/gen`, nothing of its path reaches the DLL or the PDB identity, and the whole-program link (with 0373's DSE
setting) repeats. `repeatcheck` OK in both (182 inputs, no time stamps).

### 4d. The 22-civ profile on the 43-civ build [C, 2026-11-17 16:49-16:55]

`build.py --config release --profile <the profile> --major-civs 43 --compile-only` (and the same with 22 as the
control), with `_CL_=-mllvm -no-pgo-warn-mismatch-comdat-weak=false` so that LLVM also reports COMDAT (inline and
template) functions, which it hides by default; diagnostics only (`C:/c5-64bit-work/4801/run_m43.ps1`, outputs
`m43c/`, `m22c/`). The IR-PGO diagnostics arrive as `-Wbackend-plugin` warnings at compile time (with `-flto` the
profile is applied before the bitcode is written):

| Build | Hash mismatches | Functions missing from the profile | Warning lines (TUs) |
|---|---|---|---|
| 22 civs (the profile's own) | 0 | 0 | 0 |
| 43 civs | **8 functions** | **8 functions** | 20 (6 TUs) |

16 of about 49,300 functions do not match: **99.97 % matched**. They are all cold set-up code that the player count
reaches: `CvPreGame` (`handleKnownPlayerReq`, `isKnownPlayer`, the known-players table and its `vector<uint64>`
serialization helpers, `loadSlotsHelper`), `CvGameReligions::IsPantheonBeliefAvailable`,
`CvStartPositioner::SubdivideRegion` and the map loader (`InitMap`, `SetupCity`, `SetupUnit`, `GetPlayerType`).
Mismatched and missing functions are compiled without profile data (as in plain release), the rest with it. So
**one profile serves both variants**; a 43-civ build from the 22-civ profile loses at most the profile of those 16
cold functions [C counts; the effect on speed I, not timed]. The 43-civ DLL was not linked or run here.

## 5. Identity (D164) [L, 2026-11-17 16:05-16:15 EST, workstation, headless]

`tools/x64-dll/pgo/identity.py` with four arms in copies of the pin layout's bin: **pin** (the pin's release DLL
`29F58E17...`), **rel** (the own release build `27365428...`, the timing base), **pgo** (build 1, `076323c3...`) and
**instr** (the instrumented DLL; its training runs reused). Each window compared pairwise by
`perf/evidence/0438/idtrace.py` (`tools/d10/statscmp.py`, every table of `stats.db`). Evidence
`C:/c5-64bit-work/4801/ident/` (identity.log, idtrace-*.txt).

| Window | Rows | pin = rel | pin = pgo | pin = instr |
|---|---|---|---|---|
| M 150-160 | 30,976 in 15 tables | IDENTICAL | **IDENTICAL** | IDENTICAL |
| H 300-305 | 110,420 in 14 tables | IDENTICAL | **IDENTICAL** | IDENTICAL |
| S 0-50 | 19,016 in 15 tables | IDENTICAL | **DIFFERENT**: 14 tables identical; `ReligionBeliefYields.IsReligionOwner` in 194 of 431 rows | DIFFERENT: the same column, all 431 rows |

Every game-state table of S (MapUnitsState, MapPlotsState, MilitarySummary, buildings, yields, techs, policies,
religion choices, WorldStateLog) is identical in all four arms; only the one logged flag differs, on Pantheon rows only
(pin and rel: 1 in all 431 rows; pgo: 0 for the Babylon, Maya and Shoshone pantheons; instr: 0 in all). The cause is
a latent defect of the DLL, section 6, not floating point. So **PGO is not identity-clean on the pin as it is**; the
section 6 check shows whether it is once the defect is fixed.

## 6. The latent defect behind the S difference (bug 0508)

Filed by the MC as bug 0508 (`tracker/issues/0508-dll-pantheon-holy-city-uninitialised.md`, parent 0504).

- **The defect** [C, source at 04b1211b8]: `CvGameCoreDLL_Expansion2/CvReligionClasses.cpp` line 185, the constructor
  `CvReligion(ReligionTypes, PlayerTypes eFounder, CvCity* pHolyCity, bool bPantheon)` initialises `m_iHolyCityX` and
  `m_iHolyCityY` only inside `if (pHolyCity)`. A pantheon is created with no holy city, so both fields keep whatever
  the constructing stack frame held. The default constructor (line 171) sets them to -1.
- **The readers**: the statistics loggers of `ReligionBeliefYields`, `PlayerOwnsReligion` in `CvEconomicAI.cpp`
  (line 2245) and the owner block in `CvPlayer.cpp` (line 43297), call `GC.getMap().plot(m_iHolyCityX, m_iHolyCityY)`
  and treat a pantheon as owned by its founder only when that plot is NULL; otherwise they compare the plot's owner.
  The comment in `CvEconomicAI.cpp` says the check mirrors the holy-city-owner check of
  `CvReligionBeliefs::IsBeliefValid`, so game logic can read the same fields [C for the loggers; whether
  `IsBeliefValid` reaches the garbage for a pantheon belief: I, not traced].
- **Why the code layout changes the value** [I, from the results]: the garbage is the previous contents of the stack
  slot that holds the local `CvReligion newReligion(RELIGION_PANTHEON, ePlayer, NULL, true)` of
  `CvGameReligions::FoundPantheon` (line 1102), which is then copied into the religion list. Inlining and frame layout decide which value
  last sat there. The plain LTO build happens to leave a value whose plot lookup is NULL (owner = 1 in all 431 rows),
  the PGO build inlines other code into the founding path and leaves an on-map coordinate for three of the eight
  pantheons, and the instrumented build for all of them.
- **Saves carry the garbage**: both fields are serialized with the religion (the visitor, line 207), so a save
  written by one build holds that build's garbage, and a game loaded from it reads the saved values whatever the
  build. That is why M and H (loaded at 150 and 300, no new pantheon in the window) are identical across all arms, and
  only the new game S shows it.
- **The fix**: `, m_iHolyCityX(-1), m_iHolyCityY(-1)` in that constructor's initialiser list.

Scratch check (lead-0504's item): fork branch `fix-pantheon-holycity-scratch` (scratch, not a pin branch), commit
`23b822875` = 04b1211b8 + those two lines; built as release and as release + the current profile; S 0-50 and M 150-160
against the pin's runs.

| Build (2026-11-17, `build.py --unlocked --src` the scratch worktree, same stubs and epoch) | Link | DLL |
|---|---|---|
| **fixrel**: release | 368.5 s, 13,325 MB | 23,666,688 B, `dc02fa1e7a96c8ea79313ec07d2603a078222a703eefa20ed6e7c48a302e6887` |
| **fixpgo**: release + the current profile `e06e4628...` (trained without the fix) | 418.2 s, 14,658 MB | 24,720,384 B, `fa9bed7792cd5a4397311778e09aeaf624d5e9ca4abd35f127ef228713326425` |

The profile still matches the fixed source: **0 hash mismatches, 0 functions missing**, no out-of-date warning (the two
initialisers add no branch, so the constructor's control-flow hash is unchanged) [C, `build-summary.json`
`pgo_diagnostics`].

Identity [L, 16:46-16:48 EST, `tools/x64-dll/pgo/identity.py`, `C:/c5-64bit-work/4801/fixident/`; the pin's runs of
section 5 reused]:

| Window | pin = fixrel | pin = fixpgo | fixrel = fixpgo |
|---|---|---|---|
| S 0-50 (19,016 rows, 15 tables) | **IDENTICAL** | **IDENTICAL** | IDENTICAL |
| M 150-160 (30,976 rows, 15 tables) | **IDENTICAL** | **IDENTICAL** | IDENTICAL |

So: **(1) the fix does not change today's release game** on these windows: the pin's garbage already led the
loggers to the founder branch (plot NULL), which -1 now takes on purpose; **(2) with the fix, PGO is identity-clean**:
the only S difference of section 5 was the uninitialised read. By lead-0504's rule this makes 0508 a defect fix for
both pins in the A9/A10 pin batch (the pin branches are 0508's worker's, not made here). Not checked: H with the fix
(no pantheon is founded in 300-305, section 5); the 32-bit i0-ship pin (its pantheons read their own garbage; whether
x86 = x64 parity on a game with new pantheons ever depended on that garbage agreeing is not traced) [I].

## 7. Grade A timing on `oldpc`

Testbed job `20261117-161642-a981` (submitted 16:16 EST, `--quiet`, expected 60 min): `tools/x64-dll/pgo/tbjob.py`
-> `tbseries.py` -> `perf/tbjobs/series.py`, arms **rel** = the pin layout's bin with the own release DLL `27365428...`
and **pgo** = the same with build 1's DLL `076323c3...` (server `5B254430...` in both), AI logging off, 0438's
statistics-on database; H 300-305 (6 interleaved runs per arm), then M 150-160 (12 per arm); `--compare rel,pgo`
(round 1's stats.db on oldpc); the GPU and driver read by the job into `gpu.txt` (D175).

**Result: the profile makes the DLL's AI turn 13.3 % faster on H and 15.0 % faster on M, the same game, no memory
change** [L, grade A]. Evidence `perf/evidence/0504/4801/oldpc-20261117-161642-a981/` (series.json, turnbench.json,
`cmp-rel-pgo/`, the ledger run records, `gpu.txt`, the quiet proof `quiet-summary.json`); statistics by
`perf/tbjobs/analyze.py <out>/H --ref rel` (and M).

```
Metric / tool:        seconds per AI turn (server turn-times.tsv, boot and load excluded), perf/tbjobs/series.py
                      through tools/x64-dll/pgo/tbjob.py + tbseries.py (start turn 300 / 150), branch task-4801-pgo
Builds:               rel = pin layout x64-04b1211b-pfdee656/bin with the own release DLL 27365428... (04b1211b8,
                      tools of this branch, epoch 1794741052); pgo = the same with release + profile e06e4628...
                      (build 1, DLL 076323c3...); server.exe 5B254430... in both
Game / config:        H = TS0438-H-T300 (Huge Continental Drift) 300->305; M = TS0438-M-T150 (Standard Communitu_79a)
                      150->160; 0438's statistics-on database (D35 settings); AI logging off (dll_log_flags 0)
Series:               oldpc, job 20261117-161642-a981, quiet (alone, undisturbed, foreign cores 0.038), 2026-11-17
                      H 16:17:20-16:33:08 (6 interleaved runs per arm), M 16:33:09-16:37:38 (12 per arm), order
                      rotated per round; GPU NVIDIA GeForce RTX 2080, driver 32.0.16.1088 (2026-07-21) (D175)
Result H:             rel 16.178 s/turn (sd 0.113, cv 0.70 %, 16.097-16.366, 95 % CI 16.060-16.297)
                      pgo 14.022 s/turn (sd 0.075, cv 0.53 %, 13.947-14.151, 95 % CI 13.944-14.101)
                      pgo - rel = -2.156 s/turn = -13.33 % (95 % CI -2.284 .. -2.029 s), detectable ~0.155 s (1.0 %)
Result M:             rel 0.940 s/turn (sd 0.002, cv 0.22 %, 0.938-0.944, 95 % CI 0.939-0.941)
                      pgo 0.799 s/turn (sd 0.003, cv 0.35 %, 0.792-0.803, 95 % CI 0.798-0.801)
                      pgo - rel = -0.1405 s/turn = -14.95 % (95 % CI -0.1426 .. -0.1384), detectable ~0.003 s (0.3 %)
Per turn:             every turn moves by the same share (H 300: 17.27 -> 14.94, 301: 13.72 -> 11.86, ...; M 158:
                      1.155 -> 0.988): a broad gain, not one turn's event
Boot + load:          H 3.31 -> 3.17 s, M 2.31 -> 2.29 s
Memory (D151 metric): peak private bytes after the window: M (10 turns) rel 256.5 / pgo 256.7 MB, H rel 370.5 /
                      pgo 370.4 MB; peak working set M 192.3 / 192.6 MB, H 298.7 / 300.9 MB: unchanged
Machine load:         total CPU mean 14.1 %, max 31.4 %; foreign CPU 46 s over
                      20 min (Defender 20 s, tablet service 6 s, explorer 5 s)
Profile before/after: section 7.1 (workstation sampler, attribution only)
Why it moved:         section 7.1
Correctness:          stats.db of round 1 on oldpc rel = pgo: H 14 tables 110,420 rows IDENTICAL, M 15 tables 30,976
                      rows IDENTICAL; the workstation identity of section 5 (S differs by bug 0508, section 6);
                      D24 save test: section 8
```

The gain is at the top of the proposal's 5-15 % estimate [I there; L here], close to the proposal's note of about 15 %
for PGO on top of whole-program optimisation in the other port. It is a property of `oldpc` (a different CPU from the
workstation's); the workstation's single runs of section 5 (pin 29.3 / pgo 26.3 s on H, 1.55 / 1.36 s on M, loaded
machine) point the same way but are indications only.

### 7.1 Where the time went: sampler profiles, rel against pgo [L, attribution only]

`perf/sampler/sampler.py --target server.exe` around one H 300-305 run per arm on the workstation (2026-11-17
16:50-16:55 EST; the same layouts and DLLs as the series; sampled runs are about 21 % slower and the machine was shared,
so these are shares, never times); `tools/x64-dll/pgo/profile_delta.py` compares the two reports. Evidence
`perf/evidence/0504/4801/sampler-H-workstation/` (both `report.txt`, `delta.txt`).

- Cycles sampled over the window: rel 385.1 G, pgo 336.9 G: **-12.5 %**, in line with the grade A -13.3 %.
- **The self-cycle table changes shape, because PGO inlines the hot leaves into their callers**: the binary-heap pop of
  the A* open list (`std::_Pop_heap_hole_by_index<CvAStarNode**,...>`, 6.9 % of rel's cycles), `TradePathWaterValid`
  (4.9 %), `CvPlayerAI::getPlayer` (3.7 %), `plotDistance` (3.2 %), `CvAStar::LinkChild` (2.3 %), `UpdateNodeCacheData`,
  `CvTacticalAnalysisMap::GetZoneByID`, `PathCost`, `PathValid`, `CvUnitMovement::GetCostsForMove` and about 15 more
  disappear as functions of their own, and `CvAStar::FindPathWithCurrentConfiguration`'s self share grows from 16.0 G to
  119.4 G: they now run inside it.
- **Inclusive moves** (the honest comparison): the path finder `FindPathWithCurrentConfiguration` 197.0 -> 171.0 G
  (-26.1 G, -13.2 %; it is half of the turn); trade-path work `CvGameTrade::HavePotentialTradePath` 79.6 -> 43.2 G;
  `CvPlayer::setTurnActive` 191.0 -> 162.8 G; `CvPlayerAI::AI_unitUpdate` 161.9 -> 144.4 G; `CvHomelandAI::Update`
  91.2 -> 80.1 G; `CvPathFinder::GetPlotsInReach` 99.8 -> 85.6 G; danger plots `UpdateDanger` 52.0 -> 42.1 G.
- **Why it moved** [I from the profile]: the turn is dominated by the A* path finder and its per-node callbacks
  (validity, cost, the open-list heap), called through function pointers and small out-of-line helpers. With the
  profile, LTO inlines the hot callbacks and helpers into the A* loop, lays out the hot blocks together and moves
  cold paths (asserts, logging) out of line; so the same work runs in fewer cycles in nearly every AI subsystem
  (path finding, trade paths, tactical and homeland moves, danger), which is why every turn of both games gains about
  the same 13-15 % and the gain does not hang on one function. The game itself is the same (section 5).

## 8. The D24 save test of the profile-built DLL

`tools/d10/run_d10.py --out C:/c5-64bit-work/4801/d10-pgo --x86-dll <i0-ship pin i0ship-26234371> --srv-bin
C:/c5-64bit-work/4801/ident/layout-pgo/bin --huge-save <E2000-HUGE_0350> --order-exception none --d18-x86 yes
--d18-x64 yes --jobs 4`, started 2026-11-17 16:56 EST (build 1's DLL `076323c3...` in the pin layout).

**Result: FAIL in all three games and all three parts** [L, 16:56-17:21 EST, report
`C:/c5-64bit-work/4801/d10-pgo/report.txt`]. What the failures are:

- **Part 2, tiny and standard**: x86 direct against x64 direct differ in one table only,
  `ReligionBeliefYields.IsReligionOwner` (tiny 162 rows from turn 36, standard 988 rows from turn 29; 1 on x86, 0 on
  the PGO x64): bug 0508, section 6.
- **Part 2, huge**: x86 direct against x64 direct differ in 13 of 14 tables from turn 350. **The PGO x64 run is not
  the cause**: its stats.db over 350-354 is IDENTICAL to the pin's own x64 run of the same input
  (`E2000-HUGE_0350`, `tools/x64-dll/pgo/identity.py --sets E`: pin = own release = pgo, 62,453 rows; and the D24 x64
  direct run = the pin's run, 57,239 rows), while D24's x86 direct run differs from both. So this test's x86 arm does
  not play the pin's game on the huge input today [L].
- **Part 1** (writer identity) fails for every input in every game, with 1-byte offset shifts in the body; **part 3**
  follows from parts 1 and 2. Not attributed yet.

A control run of the same command with the pin's own x64 layout (`C:/c5-64bit-work/4801/d10-pin`, started 17:30) tells
whether parts 1 and 3 and the huge part 2 fail without PGO too (the pin passed D24 on 2026-11-15, `deps/dll.lock`
`verified`).

**Control** [L, 17:30-17:57 EST, `C:/c5-64bit-work/4801/d10-pin/report.txt`]: the pin's own x64 layout
(`x64-04b1211b-pfdee656`, DLL `29F58E17...`) under the same command **fails the same way**: part 1 in every game
with the same offsets and bytes (tiny `A@2512824 B@2512823 00/04`, standard `A@5155272 ... 00/03`, huge
`A@8460965 ... 000e/0412`, ...), part 3 in every game, and huge part 2 (13 of 14 tables from turn 350). Only tiny and
standard part 2 **pass** on the pin, where the PGO DLL fails on `IsReligionOwner` (bug 0508).

| | pin (control) | PGO (build 1) |
|---|---|---|
| tiny / standard part 2 | pass | FAIL: `IsReligionOwner` only (0508) |
| huge part 2 | FAIL (x86 arm differs from turn 350) | the same FAIL; x64 run = the pin's x64 run |
| part 1, part 3 (all games) | FAIL | the same FAIL, the same offsets |

So, measured against the pin on the same day and command, **the PGO DLL's D24 result differs from the pin's only by
bug 0508's column**. **The D24 test itself does not pass for the pin today** (it passed on 2026-11-15, `deps/dll.lock`
`verified`); the huge part 2 shows its x86 arm (c5h with `i0ship-26234371`) no longer plays the x64 pin's game from
the huge input. Cause not traced here (outside 4801's scope; reported to lead-0504 for the MC). A D24 PASS for a PGO
build waits for that and for 0508.

## 9. Not verified

- The 43-civ DLL built from the 22-civ profile was compiled only (match counts, 4d), not linked, run or timed.
- The optional asserts-off sizing arm (release without `VPRELEASE_ERRORMSG`) was not built or timed.
- Timing on the workstation's CPU or the laptop: only `oldpc` (grade A). The client (proxy route, D33 product path)
  was not timed: the gain is the server's AI turn, headless.
- x64 = x86 parity on 0351's validated tier (D127) for a PGO build: not run (beyond D24, section 8).
- H with the bug 0508 fix (no pantheon founded in 300-305); the 32-bit pin's behaviour for 0508.
- How fast a profile goes stale: one pin only; the retrain rule below is a proposal.
- Whether `CvReligionBeliefs::IsBeliefValid` (game logic) reads a pantheon's uninitialised holy-city fields (0508).

## 10. Questions for the build line's ticket (adopting the profile; the MC files it)

*Answered by ticket 0513 (2026-11-24), section 11: (1) a fifth named configuration `release-pgo`; (2) the profile is
locked in `deps/dll.lock` (`profile`, `profile-file`), the file itself stays uncommitted; (3) the release take's build
stage consumes it and refuses a SHA-256 mismatch, the export's clean build is noted for 0468; (4) the retrain rule is
in `build.py`; (5) one profile for 22 and 43 civilizations stands; (6) 0508's fix is in the pins since batch 4.*

**Decided (the owner, 2026-11-17 19:35, D184, relayed by the MC on thread 0504 #4693):** x64 release builds use PGO once
bug 0508's fix is in the pins. The build line's ticket is 0513 (backlog; it depends on 0508 and on 0504's merge, which
carries `tools/x64-dll/`'s P1 work). In the D181 pin batch 0508's fix comes first; the profile is retrained on that
pin under 0513. The questions below stay open for 0513.

1. **Configuration.** `release --profile` as tested here, or a fifth named configuration (`release-pgo`) whose name is
   in the layout and the manifest? The DLL already names its profile (`.vppgo`); a configuration name would make the
   timing records and layouts self-describing too.
2. **Where the profile lives.** The binary file is 13.5 MB; its text form (`llvm-profdata merge --text`) is the one
   `tools/export/check.py` reads cleanly and diffs line by line, and converts back with `llvm-profdata merge` (whether
   that round trip gives the same indexed bytes, and so the same DLL, was not checked). `tools/x64-dll/pgo/pgo.lock`
   holds pin, SHA-256, size, training set and tool versions; moving it into `deps/dll.lock` (a `profile =` line) is
   the MC's.
3. **The one-command release build (3500) and the export's clean build (D174)** would consume the locked profile and
   refuse on a SHA-256 mismatch (as `--profile` reads the file's hash today); the two-folder result (4c) says the clean
   build repeats the DLL bytes from the export alone, given the same profile, epoch, stubs and LLVM 19.1.7. A build
   without a profile stays possible (upstream's check job).
4. **Retrain rule.** A stale profile costs speed, never correctness (unmatched functions compile as without a profile).
   Proposal: `build.py` already counts hash mismatches and missing functions (`pgo_diagnostics`); retrain when they pass
   1 % of the profile's functions (the 43-civ variant is at 0.03 %), and before each release. A retrain costs one
   pgo-instr build (about 14 min, 16.3 GB link), the three windows (about 5 min) and one profile-consuming build.
5. **One profile for 22 and 43 civs**: yes on these counts (4d).
6. **Bug 0508 first.** With the fix, PGO is identity-clean (section 6); without it, any code-layout change (PGO, a
   compiler update, ThinLTO) can change a logged flag and possibly game logic in new games with pantheons.
7. **Memory.** The profile-consuming release link needs 14.7 GB (release 13.4); a retrain's instrumented link 16.3 GB.
   One whole-program link at a time (D107/D121) covers both on 64 GB.

## 11. The profile as a locked build input: `release-pgo` (ticket 0513, D184, 2026-11-24)

Ticket 0513, account A, impl-0513, branch `task-0513-release-pgo`, 2026-11-24 15:44 EST onwards. The configuration,
the lock lines, the release take's switch and the retrain rule are described in
`research/06-tooling-and-workflows/x64-dll-build-configurations.md` section 4b; the commands are in the runbook (1.8,
"PGO"). This section holds the measurements. Marks as above.

### 11.1 Retraining on the pin 1b87dd185 [L]

Source: the shared read-only copy `C:/c5-64bit-work/keep/pins-0.1b/src-x64-1b87dd185` (= deps/dll.lock's commit,
fingerprint matches; batch 5 + epic 0504's danger package + hidden embarks; marker defines `VP64_HIDDEN_EMBARK
VP64_SPEED_DANGER VP64_SPEED_DANGER_DG7`), stubs copied from `keep/pins-0.1b/deps-x64-release/stubs`, tools of this
branch, SOURCE_DATE_EPOCH unset, on the workstation while other teams ran suites and builds (times are indications).

| Step | When (EST) | Result |
|---|---|---|
| pgo-instr build (`build.py --config pgo-instr --shorten --jobs 8`) | 15:52-16:05 | DLL 48,914,944 B, `5696d842...2a47`; compile 131 s (173 TUs), link 571.6 s, **peak private 16,363 MB** (4801: 16,323); narrowing gate 0 new; repeatability OK (183 inputs) |
| template bin (`server/build_server.py --deps keep/.../deps-x64-release --config release --no-proxy`) | 16:06-16:09 | server.exe for the pin; the bin's DLL is the pin's release DLL (below) |
| training (`train.py --sets H,M,S`, AI logging off, gated) | 16:09:58-16:14:13 | H 300->305 passed, 189 s (36.2 s/turn instrumented); M 150->160 passed, 26 s (1.97 s/turn); S 0->50 passed, 39 s (0.34 s/turn); one complete raw profile each (13.38 / 13.36 / 13.34 MB) |
| merge | 16:14 | **`vp64-1b87dd185.profdata`, 13,529,528 B, SHA-256 `718d3e587aae4824b058136264a1bdf0820684a587722e845ace47f08d7d2eb5`**; 49,373 functions (4801's pin: 49,286), 510,306 blocks, 1,716 blocks hold 90 % of the counts |
| `check_profile.py` | 16:15 | OK: 2,901 internal-linkage names with a relative file prefix in 31 files, no drive path, user or machine name in names or raw bytes; `tools/export/check.py` on the binary form: the same single e-mail false positive on random bytes as 4801's (4b), the text form passes with 0 findings |

The file is kept at `C:/c5-64bit-work/keep/pgo/vp64-1b87dd185.profdata` (README there; the `profile-file` line) and
archived with `train.json` and the detailed summary in `F:/c5-64bit-work/0513/profile/`. The lock lines in
`deps/dll.lock` (`profile`, `profile-file`) were generated from `train.json` by `train.py`'s `dll_lock_lines`;
`tools/x64-dll/pgo/pgo.lock` holds the same training record in 4801's form.

### 11.2 The release-pgo build of the pin [L, C]

`bash C:/c5-64bit-work/0513/run_pgo.sh` (2026-11-24 16:16-16:43 EST): `tools/x64-dll/build_all.py --config release-pgo
--offline --jobs 8` from the same source (the locked profile through deps/dll.lock `profile-file`, SHA-256 checked
before the first step), then `server/build_server.py --config release --deps <that> --no-proxy`, the release take's own
path. Every build_all step rc 0 (stubs, cvgamedatabase, cvlocalization, dll, host, smoke; the four non-DLL steps built
as release, `lib_config`); smoke "Release-5.4.8-80-g1b87dd185 Copy", 22 civilizations.

| | the pin's release DLL (`keep/pins-0.1b/deps-x64-release`, 2026-11-20) | release-pgo (this build) |
|---|---|---|
| DLL | 23,725,056 B, `b84aa3e9...2fde` | **24,822,272 B (+1,097,216 B, +4.6 %), `56b609a8cc6d2417dde5ddb911b65162972df1f588e8726b168ea34833dfcf84`** |
| link | 477.9 s for the whole dll step | 630.6 s (after 5.5 min waiting for the link place), **peak private 14,670 MB** (4801: 14,662) |
| PGO diagnostics | - | 0 hash mismatches, 0 missing functions, 0 malformed: **the retrain rule says current** (0 of 49,373 functions unmatched, the profile's pin = the source's) |
| narrowing gate | 794 / 0 new / 4 gone | 794 / 0 new / 4 gone (release rows) |
| records | `config: release` | `build-summary.json` `config: release-pgo`, `for_timing: true`, `profile.locked: true`, `dll_profile` = the locked SHA-256 (read back from the DLL's `.vppgo`); `build_all.json` `config: release-pgo`, `lib_config: release`; `bin/build.json` `config: release`, `deps_config: release-pgo` |
| repeatability check | OK | OK (182 link inputs, none stamped) |

The size and memory moves match 4801's on the older pin (+1,053,696 B, 14,662 MB): the profile makes LTO inline and
unroll more in hot code. The pin's DLL and this one were built at different tool commits and epochs, so their bytes are
not comparable beyond that; 4c's two-folder result says a release-pgo build repeats its bytes given the same profile,
epoch, stubs and LLVM (not repeated here).

### 11.3 Identity (D164) of release-pgo against the pin's release [L, 2026-11-24 16:49-16:51 EST, workstation, headless]

`bash C:/c5-64bit-work/0513/run_ident.sh`: `tools/x64-dll/pgo/identity.py` with the arms **pin** (the pin's release DLL
`b84aa3e9...`, `keep/pins-0.1b/deps-x64-release`), **pgo** (11.2's `56b609a8...`) and **instr** (the pgo-instr DLL; its
training runs reused), each in a copy of the bin of 11.1's server build, AI logging off; `perf/evidence/0438/idtrace.py`
(`tools/d10/statscmp.py`, every table of `stats.db`). Evidence archived in `F:/c5-64bit-work/0513/evidence/`
(identity.log, identity.json, idtrace-M.txt, idtrace-S.txt, both build summaries, check_profile.txt).

| Window | Rows | pin = pgo | pin = instr | pgo = instr |
|---|---|---|---|---|
| M 150-160 (TS0438-M-T150, 0438's statistics-on database) | 31,126 in 15 tables | **IDENTICAL** | IDENTICAL | IDENTICAL |
| S 0-50 (the D24 standard game, new) | 19,016 in 15 tables | **IDENTICAL** | IDENTICAL | IDENTICAL |

S, which differed on 4801's pin through bug 0508's uninitialised pantheon fields (section 5), is identical now: the fix
is in the pin since batch 4, and with it the code layout no longer reaches the game. Single runs on the loaded
workstation, an indication only (COMMON 23; the grade A numbers are section 7's): M 1.481 -> 1.305 s/turn (-11.9 %), S
0.257 -> 0.196 s/turn.

### 11.4 Not verified (0513)

- Grade A timing of this profile on this pin (`oldpc`): not run; 4801's -13.3 % H / -15.0 % M were measured on
  04b1211b8 with its own profile. The MC's switch at a pin batch is the place for the release candidate's own numbers.
- H 300-305 identity of release-pgo (not in the acceptance; no pantheon is founded there, section 5).
- D24 save test, x64 = x86 parity and the HI journeys of a release-pgo layout: they belong to the release take that
  switches (`release-config = release-pgo`), not run here.
- A full `tools/release/take_release.py` run with the switch on: the stage is tested with a stand-in context
  (`tools/release/tests/test_take_release_pgo.py`); the real commands it runs were run by hand (11.2).
- The 43-civ build from this profile (4801 4d: one profile serves both; not rebuilt here).
- The export's clean build (D174, epic 0468) does not exist yet; it must consume the same lock lines.

## 12. Retrained on the VP 5.5 pin x64-vp55 a2627c8ac; release-config = release-pgo (ticket 0774, 2026-11-26)

Ticket 0774, account B, impl-0774, branch `task-0774-vp55-pgo` (from main 322ae7007, which carries 0765's move of both
locks to the 5.5 branches), 2026-11-26 00:46-03:00 EST, on the workstation while other teams ran builds and suites
(times are indications, COMMON 23). Runbook 1.8's retraining steps (1)-(6); scripts in
`tools/x64-dll/evidence/0774/scripts/`, logs and summaries in `tools/x64-dll/evidence/0774/`, every output on F:
(`F:/c5-64bit-work/0774/`, C: at its floor). Marks as above.

### 12.1 Inputs

- **Source**: impl-0783's shared read-only copy `C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac` (= deps/dll.lock
  `commit` a2627c8ac, Release-5.5-28-ga2627c8ac1; fingerprint v1 94579acb... 488 files, matches the lock), its stubs
  `keep/pins-5.5/deps-x64-release/stubs` (MC-B #16452: one shared deps build, no per-branch deps). Marker defines
  `VP64_HIDDEN_EMBARK VP64_SPEED_DANGER VP64_SPEED_DANGER_DG7` (as the pin).
- **Template bin and the pin's plain release DLL**: the lock's `release` layout as 0765 left it,
  `F:/c5-64bit-work/0765/layouts/x64-a2627c8a-p957531e/bin` (server.exe `8CBA59D6...`, DLL `05F9B101...`, both the
  hashes of deps/dll.lock `release`). Step (2) needed no server build of its own.
- **Training saves** [C, SHA-256 checked by `runs.check_inputs` before every run]: 0438's H and M are 5.4.8 saves and
  do not load on 5.5 (upstream 0848d52cc), so their 5.5 counterparts of the same kind from ticket 0771
  (`tools/hi/evidence/0771/saves.tsv`; copies in `F:/c5-64bit-work/0774/saves/`):

| Set | Save (0771) | SHA-256 | Game | Window | Replaces |
|---|---|---|---|---|---|
| G | `VP55-HUGE_0350-B.Civ5Save` (= the 5.5 descriptor's `reference_saves.huge`, `F:/c5-64bit-work/reference-saves/Release-5.5-HUGE_0350.Civ5Save`) | `87fbdfecc920f89d8e7c6be8be6ee0b6ae3bb404c124e0c5f95d56fd6f8af1a8` | Continents, Huge 128x80, 8 majors (7 alive), 21 CS, Modern | 350 -> 355 | H (TS0438-H-T300, Huge, 300 -> 305) |
| L | `HI-0771-LC21B55-T200.Civ5Save` | `3a660ea8d34b4e5808df2e5c9b0c2d6b9f10abb50bdf4051bf4e5400d6539825` | Communitu_79a, Large 87x59, 21 majors (20 alive), 19 CS, Renaissance | 200 -> 210 | M (TS0438-M-T150, Standard, 150 -> 160) |
| S | new game `examples/specs/continents-standard-fixed-seed.json` (civ5-headless) | - | the D24 standard game | 0 -> 50 | S (the same) |

- **Database**: every window on the Release-5.5 descriptor's test database (`C5_RELEASE=Release-5.5`:
  `keep/release-5.5-db/Civ5DebugDatabase.db`, `d27ffe6b...`; game folder 0764's testdata copy). 0438's statistics-on
  client database is a 5.4.8 database, so G and L run without `--db` (statistics as the test database sets them); the
  stats.db identity still covers 15 tables (12.4). AI logging off (`--dll-log-flags 0`).
- **Tool changes** (this branch): `tools/x64-dll/pgo/runs.py` sets G and L, each set bound to its VP release
  (`check_release`), `DEFAULT_SETS` by deps/dll.lock `base` (Release-5.5: G,L,S; Release-5.4.8: H,M,S), `C5_RELEASE`
  passed to `server/run_server.py`; `train.py` and `identity.py` take `--release` (default `$C5_RELEASE`, else the lock's
  base), `--db` only for H and M, and `train.py` writes `profile-file = <pgo_dir>/<name>` (12.5).

### 12.2 Steps and results [L]

| Step | When (EST) | Result |
|---|---|---|
| G, L, S on the pin's release DLL `05f9b101...` (the identity's pin arm, run first to prove the 5.5 saves play in server.exe) | 01:20-01:35 | G passed, 15.65 s/turn; L passed, 7.56 s/turn; S passed, 0.474 s/turn (loaded machine, runs on F:; load about 150 s each while F: was saturated) |
| (1) pgo-instr build (`build.py --config pgo-instr --shorten --jobs 8`, `quietlock.py run`) | 01:16-01:31 | DLL 39,080,960 B, `a925c9c0a552558a76ee8d21749466ad3372cd4a86d7c234d4b50aadf0a39752`; compile 231 s (178 TUs), link 519.2 s, **peak private 12,137 MB** (1b87dd185: 16,363); narrowing 799 against 808, 0 new; repeatability OK (188 inputs, none stamped); link place taken after 0 s |
| (3) training (`train.py --release Release-5.5 --sets G,L,S`, `quietlock.py run`) | 01:31-01:42 | G passed, 247 s (18.30 s/turn instrumented); L passed, 150 s (8.60 s/turn); S passed, 102 s (0.99 s/turn); one complete raw profile each (11,578,776 / 11,575,272 / 11,545,480 B) |
| merge | 01:42 | **`vp64-a2627c8ac.profdata`, 11,237,976 B, SHA-256 `9aef3841017994de79ce55a266cc1072c65b45b9579f34a425528d25196d5b8b`**; 47,410 functions (1b87dd185: 49,373), 359,700 blocks, 2,171 blocks hold 90 % of the counts |
| (4) `check_profile.py` | 01:43 | OK: 402 internal-linkage names with a relative file prefix in 35 files, no drive path, user or machine name in names or raw bytes; `tools/export/check.py` on the binary form: 2 e-mail false positives on random bytes (`..._N@Z.d..`, `_N@Z.p..`; 1b87dd185's had 1 of the same kind), the text form passes with 0 findings |
| (5) kept | 01:44 | `C:/c5-64bit-work/keep/pgo/vp64-a2627c8ac.profdata` (README line added; 0513's file stays) and the archive `F:/c5-64bit-work/0774/profile/` + `train/` (train.json, profile-summary.txt); deps/dll.lock `profile` and `profile-file` replaced with train.py's lines, `release-config = release-pgo` added; `tools/x64-dll/pgo/pgo.lock` rewritten by train.py |
| (6) release-pgo build (`build.py --config release-pgo --shorten --jobs 8`, the profile from the lock's `profile-file = <pgo_dir>/...`) | 01:46-02:01 | DLL **19,127,296 B, `1a8d52eca823b20dc19e40110d1ab8b2b98e209edf1e5e9adbb26b6f5b09c20b`**; compile 235 s, link 481.9 s, **peak private 10,899 MB**; PGO diagnostics 0 hash mismatches, 0 missing, 0 malformed: **the retrain rule says current** (0 of 47,410 unmatched, profile pin = source commit); `build-summary.json` `config: release-pgo`, `for_timing: true`, `profile.locked: true`, `from_lock_file: true`, `dll_profile` = the locked SHA-256; narrowing 799 / 0 new; repeatability OK (187 inputs) |

The release-pgo DLL is **+2,455,040 B (+14.7 %)** over the pin's release DLL (16,672,256 B); on 1b87dd185 (5.4.8) the
growth was +1,097,216 B (+4.6 %) [C, sizes]. Why the share is three times larger is not traced [I: 5.5's DLL is 7 MB
smaller than 5.4.8's while its hot AI code is about as large, so the same inlining and unrolling in hot code weighs
more; the profile has 47,410 functions against 49,373]. The link's memory fell with the DLL: 12.1 GB instrumented,
10.9 GB release-pgo (5.4.8: 16.4, 14.7).

### 12.3 Identity (D164) of release-pgo against the pin's release [L, 2026-11-26, workstation, headless]

`bash tools/x64-dll/evidence/0774/scripts/ident.sh` (02:07-02:55 EST): `tools/x64-dll/pgo/identity.py --release
Release-5.5 --sets G,L,S` with the arms **pin** (the pin's plain release DLL `05f9b101...` of deps/dll.lock `release`;
its runs of 12.2 reused), **pgo** (12.2's release-pgo `1a8d52ec...`, played here) and **instr** (the pgo-instr DLL; its
training runs reused), each in a copy of the template bin, AI logging off; `perf/evidence/0438/idtrace.py`
(`tools/d10/statscmp.py`, every table of `stats.db`). Evidence `tools/x64-dll/evidence/0774/` (identity.log,
identity.json, idtrace-G/L/S.txt).

| Window | Rows | pin = pgo | pin = instr | pgo = instr |
|---|---|---|---|---|
| G 350-355 (VP55-HUGE_0350-B) | 56,752 in 15 tables | **IDENTICAL** | IDENTICAL | IDENTICAL |
| L 200-210 (HI-0771-LC21B55-T200) | 64,684 in 16 tables | **IDENTICAL** | IDENTICAL | IDENTICAL |
| S 0-50 (the D24 standard game, new) | 20,249 in 16 tables | **IDENTICAL** | IDENTICAL | IDENTICAL |

**The release-pgo DLL plays the pin's game on all three windows** (the same rule as 0513, which checked M and S; G is
the huge window 0513 left out). Times are no indication here: the pgo arm's G spent 11 min of its 23.5 min in boot on
disk waits (F: saturated by other jobs, "log writer: a write of 2 lines took 994 ms") and played at 38.7 s/turn on a
loaded machine, against the pin's 15.65 s/turn an hour earlier; the grade A measurement is the release candidate's
(12.5).

### 12.4 The lock without machine paths (MC #15669)

- `deps/dll.lock` `profile-file = <pgo_dir>/vp64-a2627c8ac.profdata` (was `C:/c5-64bit-work/keep/pgo/vp64-1b87dd185.profdata`).
  `<pgo_dir>` is a new setting of `tools/release/settings.py` (`pgo_dir`, env `C5_PGO_DIR`; this machine's value
  `C:/c5-64bit-work/keep/pgo` in deps/local-settings.ini, an example line in deps/local-settings.example.ini).
  `settings.resolve_placeholder` resolves `<key>/rest` and `%VAR%` / `$VAR`; `tools/x64-dll/build.py
  resolve_profile_file` (called by `lock_profile`, so build.py, build_all.py and the release take's `pgo_plan` all read
  the resolved path) keeps a plain path as written (tests, measurement locks) and, when the setting is not set, returns
  the problem: build.py exits naming `C5_PGO_DIR`, the take stops naming it [C tests
  `test_profile_file_through_the_pgo_dir_setting`, `test_an_unset_pgo_dir_is_named_and_refused`; L 12.2 step (6): the
  build read the profile through the placeholder].
- No line of deps/dll.lock or deps/i0.lock names a user folder (`test_the_repository_locks_carry_no_machine_path`; the
  export check over the product, notes and unclear groups finds nothing in deps/). The other path lines were already
  `<pins_dir>` placeholders since 0765; the F: paths in the `release*` lines' prose name the MC's lay-out sources and
  are no user folder (left as they are).
- `release-config = release-pgo`: the release take's build stage now builds the x64 DLL as release-pgo. The take's test
  fixtures that run the build stage on a fake fork (`test_take_release_stages.py` StageFixture, `test_tier_build.py`
  cfg) read the repository lock, so they now pin `x64_config: release` (before the fix 39 and 45 of their tests stopped
  with "the locked profile was trained on a2627c8ac, this take builds <fake commit>").

### 12.5 Not verified (0774)

- The cause of the +14.7 % size growth of the 5.5 release-pgo DLL (12.2; +4.6 % on 5.4.8): not traced; left for the
  performance standards team after beta 5 (MC 2026-11-26 03:1x, on 0774's merge 24d0b9783). Beta 5 ships this profile (D184).
- Grade A timing of this profile on oldpc: not run (4801's -13.3 % / -15.0 % were 5.4.8's on 04b1211b8). The
  workstation's single runs of 12.3 are indications only.
- D24 save test, x64 = x86 parity and the HI journeys of a release-pgo layout: they belong to the release take that
  now builds release-pgo (beta 5's), not run here.
- The 43-civ build from this profile: not built (one profile serves both, 4801 4d).
- The profile's text-form round trip and the two-folder repeat (4c) on 5.5: not repeated.
- Statistics-on training (0438's database had the statistics logger on); G and L trained with the test database's
  setting. Whether a 5.5 statistics-on database changes the gain: not measured.
- Tests run on this branch [L 2026-11-26]: `test_release_pgo.py` 20 passed (3 new), `test_take_release_pgo.py` 9, `test_config.py`
  23, `test_tier_build.py` 47, `test_take_release_i2_compositor.py` 11, `test_take_release_new_tag.py` 5,
  `test_take_release.py` 74, `test_descriptor.py` 34; `test_take_release_stages.py` 47 of 48: the one failure,
  `test_ia_patch_4968_row_is_the_current_pin_and_runs_without_loadstring`, is main's since 0765 moved deps/i0.lock to
  i0-vp55 dc6b8ad48 (the 4968 row's base is i0-ship-batch5 5d18d3d6c; neither file is changed here) [I: it passes on a
  tree whose i0.lock is still f33fc46a4].
