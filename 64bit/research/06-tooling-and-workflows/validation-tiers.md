# Validation tiers: the build (epic 0630, D220, D221)

The build of the Architect's three validation tiers (hotfix about 1 h, intermediate at most 4 h, full up to 24 h on
the owner's word). The design is `research/07-architecture-decisions/architect-2026-11-20-validation-tiers.md` (a source,
not edited here); the epic is `tracker/issues/0630-epic-validation-tiers.md`; the release owner's guide is
`docs/RELEASE-TIERS.md`. Marks: `[C]` read in code or a test, `[L]` a live run, `[I]` inference. Started 2026-11-20 by
lead-0630.

## 0. Status

| Milestone | State (2026-11-21 01:45 EST) |
|---|---|
| M1 hotfix tier | code complete on the team branch 2026-11-21 00:46 (5010, 5011, 5012, 5013, 5014; hermetic tests only); the MC's review points 2(a)-(e) and the end-to-end take test done 2026-11-21 01:20 (sections 7, 8, 9, 10); nothing run live; dry runs (5015) booked Sat 2026-11-21 12:45 and 15:30 on c5h-new-a (MC-B #8895), run sheet in `tracker/issues/5015-hotfix-dry-runs.md` |

Test counts (corrected 2026-11-21 01:05 by the second lead-0630; the first lead's "158" at 00:55 was a miscount of the
same files): the tier tests `tools/release/tests/test_tier_*.py` were 142 on 90bec7909 + main (142 passed in 357 s,
2026-11-21 01:00, workstation under load). After 61876ea9e: see section 10.
| M2 intermediate, M3 full | not started |

## 1. What the release driver already gives, and what it does not [C, take_release.py at feccad4aa, 2026-11-20]

- **Skip inside one work folder, confirmed.** `Driver._run` (lines 686-693) does not run a stage again when its status
  is ok or skipped and its recorded `inputs_sha` equals the new one (unless the stage's own `again()` asks for a run).
- **It cannot carry across takes as it is.** `inputs_of()` hashes the stage's own `inputs()` and `jhash` of each
  dependency's whole outputs; the build stage's outputs hold work-folder paths (`x64_layout`, `i0_dll`, ...), so two
  takes in two folders never match. The tiers therefore key carried results on content hashes only (section 3).
- **Content hashes are already in the build's outputs:** `x64_hashes` (per file of the x64 layout: the DLL, server.exe,
  the proxy, the compositor), `i0_dll_sha256`, `i0ia_dll_sha256`.
- **The 32-bit DLL build is not repeatable** (`__FILE__`, `__DATE__`, link time; `stages/build.py`'s layout README
  text), while the x64 release link is (ticket 0373). A carried savetest or whole-game result therefore needs the
  32-bit DLLs ADOPTED by SHA-256 from the base take, not rebuilt: ticket 5012. An adopted file whose hash differs is
  refused, never rebuilt silently (MC #8887).
- Useful hooks that exist: `server/build_server.py --deps` (reuse a finished DLL build: the hotfix builds only server,
  proxy, compositor), `tools/d10/run_d10.py --games` (Tiny and Standard only), `C5_RELEASE_HI_INSTALLED` (the
  journeys on the installer-installed layout; stages/hi_journey.py), `C5_RELEASE_TESTBEDS=local1,local2` (whole games
  side by side on this machine).
- `stages/wholegames.py` treats a game subset as a stop by design ("never validated"); the hotfix tier's stage maps
  exactly that stop to its pass under D221's D127 wording (ticket 5011).
- The install and removal around the installed journeys were a hand script (`tools/release/evidence/0574/
  s20_install_check.sh`); ticket 5013 makes them part of the tier's stage.
- `stages/instance.py` claims an instance without `--ticket` (found by 0626, MC #8887): not used by the hotfix tier;
  to fix in today's driver only after the first beta's candidate take.

## 2. The frame (ticket 5010) [C: code and tests, 2026-11-20]

- `tools/release/tiers/take_tier.py <tag> --tier hotfix --base-take WORK [--results DIR] [take_release options]` runs a
  stage plan of `tools/release/tiers/plans.py` with take_release.py's own `Driver` (a subclass that looks a stage up in
  `tools/release/tiers/stages/` first, then `tools/release/stages/`). Same state file, resume, STOP file and exit codes.
  take_release.py, its stages, its tests and pack.py are not edited; `tests/test_tier_frame.py` asserts that
  take_release's plans are unchanged. Default work folder `<release root>/<tag>-hotfix`; a work folder of another tier
  is refused. `--for-art` is allowed (D162 still gates through the `hi_journey` stage).
- The hotfix plan: survey, branches, testdata, lock, tierraise, build, narrowing, census, model, purity, pack, offline,
  suites, savetest, wholegames, hi_journey, tierreport. Not in it (study R1, R2, column H): drift, fixchecks, refsave,
  instance.
- `tierlib.py`: `original(name)` loads the original stage module under its own name (a tier stage of the same name can
  call it); the base take's state; the CARRIED convention (outputs `carried = {from_take, from_ended, what{name: sha},
  line}`; parts under `carried_parts`; `fresh_why` when a carriable stage ran fresh).

## 3. The automatic raise (tierraise) [C: code, 10 tests; the rule confirmed by the MC, #8887, 2026-11-20 23:46]

A hotfix becomes an intermediate-tier take (a stop, exit 10, "raised to the intermediate tier: <what>") when, against
the base take:
- **DLL pin**: the pin BATCH changed. With `--carry pins` the x64 and i0 commits the lock stage took (they are the
  pins): any DLL change of a take by pins is a pin change and raises. With `--carry series` the carry mode and the tag
  commit: a hotfix's DLL fix as a row of the series stays a hotfix and takes the with-a-DLL-change path (savetest Tiny
  and Standard, one whole game; D127 as changed by D221).
- **Compiler**: the `clang-cl --version` and `lld-link --version` lines (now from the tools; the base's from its
  build's `build-summary.json` `versions`, or its own tierraise record).
- **VP release**: the candidate descriptor's tag, upstream_commit and mods.
- **EXE build** of the live checks: `C5_TIER_EXE_BUILD` (default 26577625, D200); the base's from its tierraise or
  hi_journey record, else `C5_TIER_BASE_EXE_BUILD`, which the outputs name as stated by the operator.
- A value that cannot be read on either side raises too (D118). Until M2 the unblock names today's validated tier.

## 4. The tier's report (tierreport) [C: code, 3 tests]

`<work>/tier-report.md` and `.json`: each stage run with its minutes from the state's clock times; a separate section
"Carried results (not run in this take)" with each carried line and its take; `fresh_why` and reduced checks
(`tier_subset`); the wall clock from the first stage start to the last stage end; the verdict.
`python -I tools/release/tiers/stages/tierreport.py <work>` writes it for a take that stopped earlier.

## 5. The journeys on the installed layout (ticket 5013)


2026-11-21, worker-5013, branch task-5013-installed-journeys. All `[C]` (hermetic tests only; the live run is 5015's).

- **Module.** `tools/release/tiers/stages/hi_journey.py` shadows the original by name and wraps it
  (`tierlib.original('hi_journey')`). It does the install and the removal of ticket 0574's hand script
  (`tools/release/evidence/0574/s20_install_check.sh`) in Python and calls the original's `_run` twice at most: product
  route with `C5_RELEASE_HI_INSTALLED=<instance>`, `C5_RELEASE_HI_ROUTES=product`; stock route the same with `stock`. The
  two variables are set only around those calls; set from outside they are refused (the stage fails before building).
- **Steps and their seconds** (`outputs.steps`): installer (build_installer.py --test-build), claim, player_state
  (provision.py set-dll --dll `<work>/inputs/player-x86/CvGameCore_Expansion2.dll`), tree_before, install
  (run_installer.py, --procroot the instance), tree_installed, product, stock (only when run fresh), removal.
- **One claim file per owner.** pool.py refuses a second claim of the same instance by the same owner (O_EXCL file),
  so the stage releases its claim after the install and the original claims the instance by name, as the hand script
  did. The removal claims it again by name (waits at most min(30, C5_RELEASE_HI_WAIT) min). A gap of a few seconds
  remains in which the pool could hand the installed instance to someone else `[I]`; the original then skips (not
  played) and the removal waits for the claim.
- **Instance rule.** EXE build from the instance manifest's `exe_build` (provision.py writes build, stamp, size) must be
  26577625 (`C5_RELEASE_HI_EXE_BUILD`); a pool choice is asked for with `pool.py claim --exe-build`. Refused (skipped,
  released): another build, an installer record already there, a provision.py proxy layout, a non-mods profile.
- **Removal check.** The listing before the install against the listing after the removal; rows under `Logs\`,
  `cache\`, `ModdedSaves\`, `Saves\`, `ModUserData\` are listed apart. Any other row, a removal exit other than 0, a
  failed `set-dll --original` or `verify --quick` fails the stage and keeps the claim. When the original left a game
  running (its LeftRunning, claim kept), nothing is removed and the stage fails.
- **Stock inputs** (`outputs.stock_inputs`, every take records them): `fallback_dll` (the installed
  `CvGameCore_Expansion2_x86.dll`, the stock route's DLL, as the original's layout check hashed it), `exe_build`
  (`<build> <stamp> <size>`), per journey `journey:<name>` (SHA-256 of its index entry), `trace:<name>`,
  `start_save:<name>` (the file's bytes), and `harness` (one SHA-256 over HARNESS_PARTS: tools/hi's replay and compare
  modules, the tools/harness modules they load, set_ini/launch/guard_check, and the original stage).
- **Carry rule.** Stock is carried from `--base-take` only when the product route passed on every journey, the base's
  hi_journey is ok, its stock route passed on every journey, its stock result files still exist (the comparison is
  made against them), and every stock input is present on both sides and equal. A carried row has `ran: false`,
  `carried: true` and no seconds; `outputs.carried_parts.stock` is tierlib's carried dict. A carried base row carries
  on, naming the take that ran it.
- **A base take by today's driver** records no `stock_inputs`: its stock is never carried (fresh_why names the missing
  hashes; `fallback_dll` alone is taken from its `layout.fallback_sha256` when it ran on the installed layout).
- **D162.** The tier module's `art_check` counts a carried stock route only with a complete carried dict; `art_line`
  says "stock carried from <take>" and "product fresh, stock carried". **take_release.py --art-check** (and so
  build_installer.py without --test-build, which calls it) loads the ORIGINAL module from tools/release/stages/ and
  sees a carried row as "route stock not played": a tier take with a carried stock answers NO there until that call
  learns the tier's module (take_release.py is not changed by epic 0630's rule 1).

## 6. The results store and the carried stages (ticket 5011)


**Why a store.** Today's driver skips a stage only inside one work folder: its inputs hash holds `jhash` of the
dependency stages' whole outputs, which contain work-folder paths, so it can never match across takes [C, read of
take_release.py Driver._run / inputs_of, 2026-11-20]. `tools/release/tiers/store.py` keys on content only.

**Record.** One JSON file per (stage, content key): `<root>/<stage>/<key>.json`, written through a temporary file and
`os.replace`. Key = SHA-256 of `{'stage': name, 'parts': {part: hash}}`. Root: config `results` (`take_tier --results`),
else env `C5_RELEASE_RESULTS`, else `C:/c5-64bit-work/3500/results`; a root inside the repository is refused (the
stage fails). A record is accepted only when its stage name, its own recomputed key, every part (none missing or
empty, none extra) and `status: ok` match; else the stage runs fresh and `fresh_why` says which check failed (and, with
no record, which parts differ from the nearest record of that stage).

**Key table** (`store.KEYS`, `store.TOOL_SOURCES`; one place to read what a carry proves):

| stage | parts |
|---|---|
| savetest | x64 DLL, server.exe, other x64 bin/ files, 32-bit DLL, huge save, reference counts, test DB, major count, tool sources (run_d10.py, savecmp.py, statscmp.py, descriptor.py, dll_source.py), stage module |
| wholegames | x64 DLL, other x64 bin/ files, 32-bit DLL, test DB, major count, tool sources (longgames.py, tbmon.py, hostruns.py, statscmp.py, descriptor.py, stages/validated.py), stage module; NOT server.exe (`WHOLEGAMES_KEY_SERVER = False`, MC 2026-11-20 23:46 via lead-0630) |
| narrowing | x64 source fingerprint, compiler, major count, narrowing-baseline.tsv, stage module |
| census | x64 source fingerprint, compiler, save-width-census.py, stage module |
| model | x64 source fingerprint, compiler, proxy/gen/*.py, proxy/model/*, stage module |
| purity | x64 DLL, x64 source fingerprint, compiler, purity tools, proxy/src, proxy/include, proxy/gen, proxy/model, shell/tools/memcap.py, stage module |

Where each part is read [C, code]: DLL / server hashes from the build outputs `x64_hashes` and `i0_dll_sha256`; the
source fingerprint is the hex of lock's `x64.fingerprint` ("v1 <hex> N files"); the compiler is the `clang-cl`,
`lld-link` and `MSVC (VCToolsVersion)` lines of the x64 DLL build log named by build_all.json's `dll` step (build.py
prints them under "Tool versions"; seen in a 2026-11-20 build log as `clang-cl: clang version 19.1.7`), or a build
output `compiler` if the tier build (5012) adds one. Tool sources are hashed with CRLF read as LF: this repository
checks out with `core.autocrlf=true` (git warns "LF will be replaced by CRLF" on commit, 2026-11-21), so a working-tree
hash and a `git cat-file` hash of the same file agree only with that normalisation (a test proves it).
Not in any key [I]: c5h and the game's own files outside the repository; the descriptor's database hash and the
reference save hash stand for the game data.

**Carry.** A carried stage returns ok with the original's output keys (minus `seconds` / `wall_seconds`) plus tierlib's
`carried` dict (`line`: "carried from <tag> <work> (<ended>), x64_dll <sha16> equal, ...; covers <games>"). Files a
later stage reads (purity's `purity_tsv`, `report`: pack, instance and hi_journey read `purity_tsv`) must exist with the
recorded SHA-256; they are copied to `<work>/purity-carried/` and the outputs name the copies, else the stage runs fresh.

**Hotfix rules** (study section 6 column H): savetest fresh = run_d10 `--games tiny,standard --allow-subset` (through a
wrapped ctx that adds the two options and cuts the descriptor's reference_counts to the games played, so the original
stage's own count check runs on those games only). wholegames fresh = the original with
`C5_RELEASE_WHOLEGAMES_GAMES=tc` (restored after); its "passed on a subset" stop with subset ['tc'] and every game
identical becomes ok with `tier_subset` (games, D221 why) and `tier_pass: true`; any other stop stays a stop. A record
carries its `scope` (games covered); a tier needing more games than a record covers runs fresh (a hotfix's tiny+standard
record does not carry into the full tier).

**Import.** `python -I tools/release/tiers/store.py import <work> [--results DIR] [--tools-commit SHA] [--dry-run]`
records the ok stages of a finished take's state.json. It skips (and says why): a stage not ok, a carried one, a
wholegames whose parity is not "passed" (or a tier pass), a stage whose build (or lock) outputs changed after it ran
(its recorded `deps_sha` no longer matches), any part it cannot read, a purity file missing. Tool sources come from git
at the build's `tools_commit`; a build with `tools_dirty: true` is skipped unless `--tools-commit` is given. [I] The
later stages are assumed to have run on the same checkout as the build stage (the state does not record a per-stage
tools commit). Dry run on a real take of 2026-11-20 (C:/c5-64bit-work/0590/lead5/run, read-only): model and purity
would be recorded, the others skipped as pending [L 2026-11-21].

**Tests** (hermetic): `tools/release/tests/test_tier_store.py` (23), `tools/release/tests/test_tier_carried.py` (18):
one changed byte in the x64 DLL runs savetest and wholegames fresh; a changed server.exe runs savetest fresh and carries
wholegames; a missing hash in the base record, a deleted or changed carried file, a changed tool source, a record of
another stage, a compiler change, a store inside the repository.

## 7. The hotfix build: DLLs adopted from the base take (ticket 5012) [C: code and tests, 2026-11-21]

`tools/release/tiers/stages/build.py` shadows `build` in the tier. Adoption when the lock's x64 and i0 commits equal
the base build's `x64_commit`/`i0_commit`, the major count is equal, and the base build's recorded inputs (`inputs.own`
of its state entry) equal `stages/build.py` `inputs()` on `ia`, `ia_patch`, `ia_patch_sha256`, `major_civs`,
`shell_config`, `purity_allowed_drops` (`jobs` ignored). Otherwise the original stage runs unchanged, outputs
`adopted={}` and `adoption.why`.

- **What is adopted** `[C]`: `<base x64_out>/deps` (build_all.json, stubs/, the top-level files of dll/: what
  `server/build_server.py --deps` reads, lines 355-373 and 528-534 at 42dd5e04d), checked against the base layout's
  `x64_hashes` (`bin/CvGameCore_Expansion2.dll`, and every other `bin/*.dll` found in `deps/stubs/`: lua51_x64,
  CvGameDatabase_x64, CvLocalization_x64); the 32-bit and -ia DLLs from the base layout (`i0_dll`, `i0ia_dll`) against
  `i0_dll_sha256`/`i0ia_dll_sha256`. The `.lib` import libraries, PDBs, build_all.json and the other files of `dll/`
  and `stubs/` have no hash record in the base take. Since 2026-11-21 (61876ea9e, the MC's review point 2(e)) each of
  them is hashed after the copy and checked equal to the base's file as it is now (a copy that differs is a refusal),
  and listed in the outputs `adopted_unrecorded` {path: SHA-256}; the adoption line names both groups: "checked
  against its SHA-256 records: ..." and "copied with no record in the base take ...: N file(s) .lib, .pdb, ...".
  This proves the copy, not that the base's file is unchanged since the base take: only the DLLs carry a record `[C]`.
  (Replaced: "copied unchecked".)
- **Refusal** (MC answer to lead-0630, 2026-11-20 23:46) `[C]`: a missing file or another SHA-256 is a stop (exit 10)
  naming the file, the expected and the found hash, before anything is built; never a silent rebuild.
- **Then** `[C]`: `build_server.py --deps` builds server and proxy (step 1 skipped); the original `run()` is called with
  the adopted builds offered as finished-build records (its resume convention), so the compositor, layout, candidate
  locks and outputs are the original's. The laid-out x64 DLL must be byte-equal to the adopted one. Extra outputs:
  `adopted` {path in the work folder: SHA-256}, `adoption` {from_take, from_ended, line}, and `compiler` (the base
  build's, `store.compiler_of`) so ticket 5011's carried stages still find the compiler of an adopted DLL.
- **Tests** `tools/release/tests/test_tier_build.py` (12, 330 s on 2026-11-21; 13 since 61876ea9e: the
  unrecorded files named apart, and a copy that differs from the base file refused, 97 s under load; each runs a base take of the fake fork; a test fails with "this repository moved" when a tracked file of this checkout is edited while it runs, as the original build stage checks):
  adoption, x64/i0/stubs hash mismatches and missing files refused with nothing built, changed x64/i0 commit, changed
  instruments patch and a failed base build running the full build, the output keys equal to the original's.
- **Not verified** `[I]`: a real base take's `deps/dll/` folder size (only its top-level files are copied); the real
  build_server.py with `--deps` on an adopted folder (C: low, no heavy build on 2026-11-21: ticket 5015's dry run).

## 8. The area-to-checks map and the suites runner (ticket 5014) [C: code and tests; L: three real suites, 2026-11-21]

Files: `tools/suites/areas.json` (the map), `tools/suites/run_suites.py` (the runner), `tools/suites/skip_allowlist.json`
(empty), `tools/release/tiers/stages/suites.py` (the hotfix tier's stage), tests
`tools/release/tests/test_tier_suites.py` (34 tests, 2026-11-21).

- **Map** `[C]`: 34 areas of path globs (`**` crosses folders), each naming suites and the tier stages (`live`) a change
  there makes count; an area with neither names why (`note`). Every tracked file of the team branch (4b1f3b797) is in
  at least one area; the test fails on an unmapped file. `always`: the safety scan
  (`tools/harness/tests/test_safety_static.py`) and the export gate.
- **Export gate** `[C]`: the D168/D174 gate is `tools/export/check.py --rev HEAD --since BASE` (ticket 4700; how the MC
  runs it: `upstream-export.md` section 2). There is no other export check of `tools/`. It needs a diff range, so the
  runner runs it only with `--diff`.
- **Suites** `[C]` read from the files, not from runs: 40 suites; 25 need nothing here (pytest over a folder, or the
  export gate's exit code), 13 need a native build (CMake/ctest under `ipc/`, `models/`, `options/`, `render/`; the
  script-style `proxy/tests`, `server/tests` outside `run_all.py`, `render/tests` Python scripts, `shell/tests` (mixed),
  `shell/frame/tests`, `tools/x64-dll/tests`, `tools/harness/native/tests`, `server/src/remote/test`), 1 needs game files
  (`tools/exebuild/tests`: the golden EXE copies and local tables) and 1 needs a game (`server/tests/render/run_all.py`).
  The runner runs only the first kind and lists the others as `not-run` with the command that runs them.
- **Inventory reconciled** 2026-11-21 (section 9): ticket 0627's `test-health-on-main.md` (main 1ebd32ca2) is in
  `areas.json` → `inventory.reconcile_0627`. (Replaced: "not yet reconciled".)
- **A skip counts as failed** `[C]`: pytest runs with `-rs`; every `SKIPPED` line fails the suite unless
  `skip_allowlist.json` has an entry for that suite whose `match` is in the line, with a reason. Since 2026-11-21 the allowlist
  has two `harness` entries (section 9); every other skip still fails.
- **Times** `[L]` 2026-11-21 00:10, workstation, while other agents' suites ran: `safety-static` 55 passed in 41.7 s and
  44.7 s; `proxy-gen` 33 passed in 8.7 s; `export-gate` on `357dc271e~1..357dc271e` PASS in 36.9 s (it reports the merge
  base and counts only new findings). The runner's own overhead is under a second. These are the only `measured_s` in
  the map; the hotfix stage's 15-minute budget is checked against recorded times before anything runs and by the
  clock while it runs (a suite cut by the budget is `over-budget`, a stop, never a pass).
- **A change of `tools/release/` selects `release-tiers`** (the whole `tools/release/tests` folder): 100 min under
  load in 0627's sweep. In the hotfix tier it runs through its substitute `release-hotfix` (section 9).
- **Stage** `[C]`: compares the base take's build `tools_commit` with this take's (a dirty build: the base against the
  working tree), runs the runner once through `ctx.run(..., heavy=True)` (quietlock.py run), fails on a failed suite,
  stops over budget, and stops when the base take records no `tools_commit` or the commit is not in the repository.

## 9. The suites by tier: the MC's review points 2(a)-(d) (lead-0630, 2026-11-21) [C: code and tests; L: as dated]

Commit 61876ea9e on the team branch. Files: `tools/suites/run_suites.py`, `tools/suites/areas.json`,
`tools/suites/skip_allowlist.json`, `tools/release/tiers/stages/suites.py`, tests in `test_tier_suites.py`.

- **Every suite names its tier and its minutes** (epic rule 3) `[C]`: `tier` is `hotfix`, `intermediate` or `full`
  (the map is refused without it); `measured_s` with `measured` (when, where, under what load) or `measured` saying why
  there is no time yet. The rule written into `areas.json` → `tiers.rule`: a suite that needs nothing is `hotfix` when
  measured at 8 min or less (half the stage's 15-min budget) or not measured yet (the clock still stops it), else
  `intermediate`. A test holds the rule.
- **`--tier T`** `[C]`: a selected suite of a higher tier runs in tier T only through its `hotfix_substitute` (status
  `replaced`); without one it is `over-tier`: exit 4 like over budget, and the hotfix stage STOPS naming it ("raise the
  take or run it outside the tier (--suites X)"), never leaves it out silently. Suites the runner does not run (build,
  game) stay `not-run` in every tier. The stage now calls the runner with `--tier hotfix --game-files`.
- **2(b) the release split** `[C]`: `release-tiers` (the whole `tools/release/tests`, intermediate; 100 min under load
  in 0627's sweep, about 50 min alone per release-builds.md 4.2) has the substitute `release-hotfix`:
  `test_take_release.py` and every `test_tier_*.py` (the e2e test included; a test keeps the list equal to the folder's
  `test_tier_*.py`). Times: see section 10. **Corrected 2026-11-23 (MC #10980, ticket 5024 take 3):** release-tiers is
  a full-tier suite (MC #10917); its `hotfix_substitute` is now a list, `release-hotfix` (the hotfix tier's part:
  `test_take_release.py` and the M1 `test_tier_*.py` files, 311 tests) and `release-intermediate` (M2's six
  intermediate-tier files: intermediate, linkwrap, modpack, parallel, scenes, sweep; 163 tests). A member stands in
  only in tiers at or above its own, so the hotfix tier runs release-hotfix alone. The test now keeps every
  `test_tier_*.py` in exactly one of the two. Was: release-hotfix held every `test_tier_*.py` and grew to 474 tests,
  which timed out at 900 s in take 3 (validation-tiers-m2.md 8.4c).
- **2(a) harness** `[C]`: `harness` runs with `--ignore=tools/harness/tests/test_safety_static.py` (the safety scan is
  the always-suite `safety-static`; it ran twice before). Two allow-list entries with reasons: the live opt-in skip
  ("live stand-in tests are opt-in: set C5_HARNESS_LIVE=1", nine files; the runner scrubs C5_HARNESS_LIVE as
  run_tests.py does) and the superseded `test_the_stub_deny_list_refuses_alt_enter` ("W2's deny.py has landed").
  0627 counted 12 skips on the workstation; the other skip reasons in the folder depend on the machine (Inno Setup,
  the SDK debugger, an audio endpoint, vp_lua.py) and stay failures until a run shows which occur `[I]`. `harness` is
  17 min (0627) and so `intermediate`: a hotfix that changes `tools/harness/` stops in the suites stage.
- **2(c) exebuild-tables** `[C]`: runnable with `--game-files` (kind pytest, needs `game-files`); its tests FAIL on a
  missing golden EXE or table (test_exebuild.py's docstring), so running it is fail-loud. Decided with 0627: the hotfix
  take runs on the workstation, which holds both, so the stage passes `--game-files`; 52 passed in 8 min (0627, under
  load) = `hotfix`. 0627's first red test (`shell/tests/test_model_programs_table.py`, the one shell test that reads the
  exe tables' evidence as text) needs no build: suite `shell-tables`, hotfix, in the `exebuild` and `shell` areas
  (23 passed in 0.15 s, 2026-11-21 01:06 `[L]`).
- **2(d) 0627 reconciled** `[C]`: times of 0627 section 3 taken as `measured_s` (dated, "under load"): `harness`
  1020 s, `queue` 2100 s (intermediate), `export-tests` 690 s (intermediate), `installer` 480 s, `exebuild-tables`
  480 s, `release-tiers` 6000 s, `shell-tests` 3685 s (build). `ops/tests/test_hooks.py` is a script (pytest stops with
  SystemExit: 0): `ops` ignores it and `ops-hooks` runs it by exit code. `perf/turnstudy/budget` tests are scripts:
  `perf-budget`, `perf-budget-stock` exit suites. `perf/` is never one pytest session (0627 section 2: two module-name
  collisions): `perf-tools` split into one suite per folder, a test holds it. Ticket 0632 merged (main e0e01445b,
  2026-11-21 01:18): `test_join_ring.py` 4 of 4 alone; `shell-tests` notes that test_lockstep also needs
  `C5_FRAME_BUILD`.
- **What of 0627's sweep `run_suites.py` covers, and what not** (0627's draft "commit the sweep as a tool", MC #8941
  -3): it covers every pytest folder of 0627 section 3 that needs no build (as suites with their commands and times),
  the two script groups that need nothing (`ops-hooks`, `perf-budget*`), the per-folder rule for `perf/`, skips as
  failures, and the selection by changed areas. It does NOT run: `shell/tests` and `shell/frame/tests` with their
  reader builds (0627 section 6), the script-style `proxy/tests`, `proxy/tools` and `server/tests` files (section 4:
  each builds its programs), `tools/x64-dll/tests`, the C++ ctest suites, `server/tests/render/run_all.py`; these are
  `needs: build|game` with their command in `how` (the intermediate tier, M2). It does not write junit XML or group
  failures (0627's `triage.py`), and it does not run under `quietlock.py run` by itself (the stage does; by hand, wrap
  it).

## 10. The end-to-end hotfix take test and the measured suite times (lead-0630, 2026-11-21)

- **End to end** `[C]` `tools/release/tests/test_tier_e2e.py` (6 tests, 2.3 s): `take_tier.main()` with the real
  command line (`--tier hotfix --base-take --work --fork --descriptor --tools --stages-dir --tier-stages-dir`), every
  stage of `plans.HOTFIX` a fake that logs its call, the real `tierreport`. It shows: the 17 stages run once in the
  plan's order and the state reaches `hotfix`; the report lists the run stages with minutes and the carried ones
  (narrowing, census, model, purity, wholegames, hi_journey's stock route) apart with the base take's name and never in
  the run count; a fresh savetest's `fresh_why` and `tier_subset`; a resumed take runs nothing again and reads the base
  from the state; a raise in `tierraise` stops (exit 10) before `build` and the report command says "NOT reached:
  tierraise is stop"; a failed savetest ends the take before `wholegames`; take_release's own plans hold no tier stage.
  Not covered: the real stages (each has its own tests) and their outputs' exact shapes on a real take (5015).
- **Measured** `[L]`: see the lines below, each dated.
  - 2026-11-21 01:22-01:29 EST, workstation, `quietlock.py run --who lead-0630` (no take), a scratch checkout of
    61876ea9e, `run_suites.py --suites release-hotfix`: **release-hotfix 231 passed in 361.8 s (6.0 min)**
    (test_take_release.py 68 + the ten test_tier_*.py files 163), safety-static 55 passed in 39.3 s, export-gate not
    run (no range); runner total 401 s. DIAGNOSTIC (MC-B #8949): 0626's rehearsal chain, W32's render suite and 0632's
    shell test ran beside it (`quietlock.py check` at 01:05: three heavy jobs registered). So a hotfix that changes
    `tools/release/` needs about 6 + 0.7 + 0.6 min of the stage's 15 [L, under load].
  - 2026-11-21 03:34-03:46 EST, workstation, plain pytest in the lead worktree (no quietlock; team branch aae902d6c =
    c7b151dbb merged with main 404e0f78d): **test_take_release.py + test_tier_*.py 234 passed in 670.8 s (11.2 min)**;
    safety-static 55 passed in 37.6 s. DIAGNOSTIC: 0626's rehearsal chain was still running. Nearly twice the 01:22
    figure for 3 more tests: the load, not the tests, moved it [I]; the quiet-machine figure is still owed (about
    06:00). At 11 min under load the release-hotfix suite alone uses most of the hotfix stage's 15 min.
  - 2026-11-21 08:50-09:26 EST, workstation, `quietlock.py run --who lead-0630` (a registered job, not a hold: rule 16
    caps a hold at 30 min), scratch checkout of da4e0f4a0, `run_suites.py --suites harness,release-tiers --tier full`
    (MC's yes #9113; 0626's chain had ended). Near-quiet: the only other registered job was an idle game of impl-0638
    on c5h-new-a. **harness 2216 passed, 1 failed, 30 skipped (all allow-listed) in 662.6 s (11.0 min)** without the
    safety scan; **release-tiers (the whole tools/release/tests) 594 passed, 0 skipped in 1437.6 s (24.0 min)**;
    safety-static 55 in 33.5 s; runner total 2133.6 s [L]. So 0627's 17 and 100 min were mostly load: under load the
    folder took 4x as long. `harness` stays intermediate (11 min fits the hotfix's 15 only on a quiet machine).
  - The one red: `tools/harness/tests/test_pool_host_alias.py::test_only_in_memory_readers_in_tools` fails on main
    since epic 0504's merge (e17f07180): `tools/x64-dll/pgo/check_profile.py` reads USERNAME/COMPUTERNAME as in-memory,
    masked search terms (as export/check.py does) and is missing from the test's allowed set. Not the tier's file;
    reported to the MC (thread 0630) for its owner. Until fixed, every `harness` suite run is red [C].
  - Skip allow-list `[C]`: the 30 skips are 29 `-rs` lines (one is `SKIPPED [2]`): 28 "live stand-in tests are opt-in"
    and 1 "W2's deny.py has landed"; both entries of `tools/suites/skip_allowlist.json` (hand-over 2a) cover them, so
    the list is complete for this suite; no machine-dependent skip was seen. The runner counted allowed skips by line
    (29 of 30); since this section's commit it sums pytest's `[N]` (`run_suites.skip_count`, a test in
    test_tier_suites.py).
  - 2026-11-21 09:35-09:51 EST, the same conditions, team branch f22fda5d2 with main cf8449ff8 (the allowed-reader fix):
    **harness 2217 passed, 0 failed, 30 skipped (30 allow-listed) in 662.4 s (11.0 min); release-hotfix 238 passed in
    259.6 s (4.3 min)**; safety-static 55 in 35.4 s [L]. The release-hotfix figure replaces the diagnostic 6.0 min: a
    hotfix that changes `tools/release/` needs about 4.3 + 0.6 min (+ export-gate 0.6) of the stage's 15 on a quiet
    machine.
  - Still not measured: the suites marked "not measured yet" in `areas.json` (the hotfix stage stops them by the clock).

## 11. The stock route is never played on the installer-installed layout (2026-11-21 03:21, desk #9063; MC #9064/#9065)

- **Finding** `[L]`, ticket 0626's rehearsal, c5h-new-b, EXE build 26577625, 2026-11-21 03:21: release-basic PRODUCT on
  the installed layout passed (534 s); release-basic STOCK on the installed layout failed at step 8 ('judge in_game'
  deadline, no box, no crash). Cause `[C]`: on an installed layout the stock route is vpproxy.ini mode=off, which loads
  the installed fallback, the player's own VP 32-bit DLL; it has no Lua channel, and every judge reads state through
  the channel. mode=off set that way also leaves config.ini in the D33 form (`[DisableSystems]`): a deep red map. A
  design gap of the stage (5013 planned the fresh stock route on the installed instance), not of the product.
- **Ruling** (MC #9064 for the candidate, #9065 for the tier): the installed layout plays the PRODUCT route only; the
  stock route, when fresh, is played on the stage-laid layout with the instance-aware DLL.
- **Tier stage since commit "0630: hi_journey ... stage-laid stock"** `[C]`: `installed_env()` builds the environment
  of every installed play and raises `InstalledStockRefused` ("cannot be judged") for any route list other than
  `product`. A fresh stock route is played after the removal of step 8 (the instance is clean again), by the original
  stage with `C5_RELEASE_HI_ROUTES=stock`, `C5_RELEASE_HI_INSTANCE=<same instance>` and no `C5_RELEASE_HI_INSTALLED`
  (it claims, lays the instance-aware DLL, plays, restores by hash). When the removal or the product run did not end
  ok, the stock route is not played (`stock.mode` "not run" with the reason). The stock input `fallback_dll` is now
  `stock_dll`: the build's `i0ia_dll_sha256` (the DLL the stage-laid route loads); a base without it runs stock fresh.
  (Replaced: "`stock` runs the original again with C5_RELEASE_HI_ROUTES=stock on the same installed instance".)
- **Tests** `tools/release/tests/test_tier_hi_journey.py`: the installed stock refused loudly; a fresh stock route
  played after the removal without `C5_RELEASE_HI_INSTALLED`; no stock route when the removal fails; the stock-input
  tests on `stock_dll`.
- **Cost** `[I]`: a fresh stock route now costs a stage-laid layout and restore on top of the installer's removal
  (a few minutes); run 1 of 5015 measures it.

## 12. The dry runs' base: ticket 0626's rehearsal take, read 2026-11-21 09:31 (MC #9113)

Read-only, `C:/c5-64bit-work/0626/std/run` (tag Release-5.4.8, descriptor Release-5.4.8-i3-rehearsal.json, tier
validated, `--for-art`); nothing written there.
- **Import** `[C]`: `store.py import <run> --results C:/c5-64bit-work/5015/results --dry-run`: 6 would be recorded
  (savetest, wholegames, narrowing, census, model, purity), 0 skipped. The real import is run 1's first step.
- **Inputs** `[C]`: `carry` = `pins` (so run 2's DLL change can only be shown as a raise, not carried past);
  build `tools_commit` a4c1c8428 (clean) is on `origin/task-0626-beta-rehearsal-take`, not on main: the store reads the
  stage sources from git at that commit, so that branch must not be deleted (D206) before the dry runs end. The build
  records x64_hashes (11 files), i0_dll_sha256 and i0ia_dll_sha256; there is no `stock_dll` record, so the tier plays
  the stock route fresh (as planned). The base's `report` stage is `pending`: the take never reached its report.
- **Its D162 line is NO** `[C]`: the base's hi_journey ran in two driver runs. Run 1 (01:36-01:44) played release-basic,
  product and stock, both exit 0, then a STOP during release-large's launched game ended the run (ticket 0634). Run 2
  (`--only hi_journey`, 01:51-02:11) played release-large, product and stock, both exit 0 (21 steps). The state keeps
  only run 2's journeys, so the stage says "verdict passed ... for the owner: NO, refused by D162: release-basic was not
  played". All four plays were on the STAGE-LAID layout; the installed-layout product passes of 03:04-03:29 (c5h-new-b)
  were 0626's own runs outside this state.
- **What that means for a tier run on this base** `[I, from the code of 5011/5013]`: nothing of the base's hi_journey is
  carried (its status is `stop`, so the store has no record; the product route is never carried by design; no
  `stock_dll` record). The tier's D162 line therefore comes only from its own hi_journey stage: both journeys, the
  product route on the installer-installed layout and the stock route fresh on the stage-laid layout, must pass in
  that one stage run. The base's NO neither blocks nor helps it. A dry run may build on this base because it is not a
  release; a real hotfix must name a base that reached its report with D162 YES.
- **The base acceptance check** `[C: code and 4 tests, 2026-11-21 09:35; MC #9116]`: `take_tier.base_unaccepted()`
  refuses (exit 2, nothing run, no state written) a base take that reached no tier, whose `report` stage is not `ok`,
  that has no D162 line or whose D162 line is not `allowed`, naming every reason. `--base-unaccepted` (dry runs only)
  takes it anyway: a LOUD line, the reasons recorded in `config.base_unaccepted`, and a resume needs no second flag.
  On 0626's take it gives all three reasons (no tier reached, report pending, D162 NO), so both 5015 runs pass
  `--base-unaccepted`. Tests in test_tier_e2e.py (9 with the 6 before).

## 13. A DLL hotfix on the first beta's base is an intermediate take (MC #9147, #9160; 2026-11-21 11:10)

- `[C]` tiers/stages/tierraise.py `pin_of`: with `--carry pins` the raise key `dll_pin` is the x64 and i0 commits of
  the lock stage, so any DLL change on a pins-based base raises; with `--carry series` it is the carry mode and the tag
  commit, so a DLL fix as a series row stays a hotfix.
- The 0.1b pins force `--carry pins` (impl-0626 #9158: carry.tsv ends at batch 5 and lacks 0504's danger package and
  0573's hidden embarks). RULING (MC #9160): the candidate stays on pins. Consequence: on the first beta's base a DLL
  hotfix is an intermediate take until ticket 0639 (series rows for the 0.1b pins, backlog p7) is done.
- M1's acceptance point (f) (one measured run WITH a DLL change) is therefore NOT VERIFIED: neither 0626's rehearsal
  (pins) nor the candidate (pins) can give it. 5015 run 2 is a raise test only (it stops at tierraise before build).
  It is measured under ticket 0639's acceptance point 3.
- **Updated 2026-11-23 (ticket 0639):** carry.tsv now holds the 0.1b pins as rows (the danger package, hidden embarks
  and a `merge` row for the PCH conflict; release-builds.md 5.5); `--carry series` on Release-5.4.8 gives the pins'
  trees exactly. The first `--carry series` take after a pins-based base is intermediate once (the carry mode is part
  of the raise key); a DLL fix as a series row on that take is a hotfix [L: take B's tierraise, 2026-11-23 21:33, not
  raised]. The measured minutes: release-builds.md 5.6 (M1 point (f) now measured: about 174 min serial with the
  journeys estimated, against the study's 65-75).
  (Replaced: "the 0.1b pins force `--carry pins`".)

## 14. Dry run 1 (ticket 5015), 2026-11-21, c5h-new-a on F: (EXE build 26577625, mods form) [L]

Base: 0626's rehearsal take with `--base-unaccepted` (section 12); checkout of the team branch c1b6952b7; `--carry pins`.
Every minute here is an UPPER BOUND: F: stalled 35 of 61 minutes 12:42-13:42 (MC #9339), and three other games ran
from F: copies at the same time (MC-B #9317).
- Stages 13:12:40-13:24:13: survey 2 s, branches skipped (pins), testdata 1.9 min, lock 1 s, tierraise 1 s (no raise),
  build 4.8 min (DLLs adopted), narrowing, census, model carried (0 s each), purity FRESH 1.5 min (its tool sources
  differ from the base's: tools commit a4c1c8428 vs c1b6952b7), pack 0 s, offline 3.2 min.
- **suites STOPPED (exit 10), by design** `[C]`: the diff base..head is the base's tools commit to today's branch, a
  day of main plus the epic, so the area map selected exebuild-tables, installer, release-hotfix, proxy-gen,
  shell-tables, safety-static and export-gate, whose recorded times add up to 1312 s, over the 900 s budget. The
  stop named the suites and the way out. A real hotfix's diff is small; this one is not.
- Run outside the tier as the stop advises (13:24-13:44, `run_suites.py --suites ... --game-files`): all passed:
  exebuild-tables 52 in 533.9 s, installer 37 in 258.4 s, release-hotfix 238 in 330.5 s, proxy-gen 33 in 7.1 s,
  shell-tables 23 in 1.5 s, safety-static 55 in 54.5 s; 1185.7 s. Under that load, exebuild-tables alone uses 59 % of the
  budget: [I] it reads the golden EXE copies on F: (F:/c5-64bit-work/golden), so F:'s stalls fall on it. A hotfix that
  selects exebuild-tables does not fit the 15 min while the golden copies are on F:.
- The take resumed with `--from savetest` at 13:44 (later stages do not depend on suites): savetest FRESH 13.1 min
  (server.exe differs from the base: c80ec989 vs 0b5f9d95, as expected from a newer tools commit); wholegames CARRIED
  (x64 DLL, other x64 binaries, i0 DLL, test DB, tool sources and stage module equal); tierreport ok; the take ends
  "hotfix tier not reached: suites is stop" (exit 10), as it must.
- hi_journey, attempt 1 (13:57-14:03): installer built 16 s, installed 12 s, then every launch failed at once:
  `tools/harness/native/bin/bg_helper32.exe is missing`. A fresh checkout has no built harness binaries (git-ignored
  build output). The stage played the stock route after the removal, also unlaunchable, restored the instance by hash
  and verified it (rc 0). OPERATOR RULE: a take from a scratch checkout sets `C5_HARNESS_BIN` to a folder with the
  built binaries (the main checkout's `tools/harness/native/bin`) or builds tools/harness/native first.
- hi_journey, attempt 2 (`--only hi_journey`, 14:04-14:33, with C5_HARNESS_BIN): install ok; the product replay of
  release-basic ran 712 s and ended "has no window yet" (exit 1); release-large was refused at once ("a session is
  active", exit 2); then the harness stop of the game (pid 38872) timed out after 15 s (psutil TimeoutExpired) and the
  stage, correctly, removed nothing, kept the claim and named the instance unfit. The replay failure is ticket 0648's
  last-step window race (fixed on main 115566aa6 at 14:08, after this checkout c1b6952b7) [L, MC-B #9452]; the slow
  stop is [I] F:'s stalls. Restored by hand 14:36-14:39 by the stage's own steps (frontend restore; the two settings
  files from the stage's kept copies by hash; the journey saves removed; the installer's removal exit 0; set-dll
  --original; verify --quick rc 0; provision snapshot: no differences). One residue: the Community Patch DLL is the
  instance's first DLL (3eaa3290) where it was ff0f6e86 before the run (set-dll --original restores the FIRST DLL, not
  the one before the run); no copy of ff0f6e86 was on the instance; the F: copy is retired by ticket 0646 the same day.
  Finding for the stage [I]: its step 8 should restore with `set-dll --previous` or by the hash it recorded before
  `set-dll-player`, not `--original`.
- Run 1's minutes (all upper bounds, F:): stages up to offline 11.5 min; savetest 13.1; hi_journey 29.6 (failed); the
  suites outside the tier 19.8.

## 15. Dry run 2 (ticket 5015), 2026-11-21 14:56-16:21, c5h-new-a re-created on C: (ticket 0646) [L]

Checkout of the team branch 5e116ca33 (with main's 0648 fix of the replay race), the same base and `--base-unaccepted`,
`C5_HARNESS_BIN` set. C: latency for the whole window from diskwatch: 1019 records, 0 above 50 ms (max read 7.1 ms,
max write 4.7 ms), so C: did not stall; F: in run 1's window had 707 of 1044 records above 50 ms (max 1.09 s): run 1's
minutes are upper bounds only. The machine was shared (other agents' games and builds, CPU 95 % at 16:22): these
minutes are the tier's on a busy workstation, not a quiet one.
- **Raise test (2a), 14:56:40-14:59:39, 3.0 min** `[L]`: deps/dll.lock's x64 commit changed in the scratch checkout
  only (1b87dd185 -> its parent 9a3ca174b, never committed); tierraise stopped the take (exit 10) before build:
  "raised to the intermediate tier: dll_pin changed", naming both commits. Survey, testdata (2.8 min) and lock ran;
  nothing was built and no game started. This is the only DLL-change path a pins base allows (section 13).
- **The repeat of run 1 (2b)**: survey 0.1, testdata 1.1, lock 0.0, tierraise 0.0 (no raise), build 8.2 (DLLs
  adopted; server and proxy built), narrowing/census/model carried 0.0, purity fresh 2.6, pack 0.0, offline 6.7,
  suites STOP by design as in run 1 (not rerun outside: run 1 measured them, 19.8 min upper bound; 4.3 + 0.6 min for
  release-hotfix and the safety scan on a quiet machine, section 10), savetest fresh 14.4 (server.exe changed),
  wholegames carried 0.0, **hi_journey 47.7 min: PASSED, D162 yes in one stage run**: installer 25 s, install 8 s,
  product release-basic 557 s and release-large 475 s on the installer-installed layout, removal 28 s (tree outside the
  run outputs unchanged), stock fresh on the stage-laid layout release-basic 560 s and release-large 409 s; the
  stock route's game crashed (0xC0000005, no box) at release-basic's quit step after its replay had passed (dialogwatch
  #9562), the moment of ticket 0484's known exit crash of the -ia test DLL [I: same step and DLL as 0626's 01:44 case;
  no dump read]. The restore check after the run: only run outputs differ, all hashes equal, the owner's tree unchanged.
- **Against the study** (40-50 min for a hotfix without a DLL change): 81.0 min from survey to tierreport without the
  suites, about 85-100 min with them. Where the time went [I, from the stage minutes]: (1) hi_journey 47.7 min, of which
  the stock route is 22 min: it ran fresh because this base has no ok hi_journey record; on an accepted base with
  `stock_dll` it is carried (D221) and the stage is about 26 min. (2) savetest 14.4 min fresh because server.exe
  differs; the study counted it carried for a fix outside the server. (3) build 8.2 and offline 6.7 min are the server
  and proxy builds and the offline checks, which the study priced at about 10 min together. So a hotfix touching
  neither the server nor the DLL, on an accepted base, is about 1 + 8 + 3 + 7 + 26 + suites (<= 15) = 50-60 min on a
  busy machine; one that changes server.exe adds the 14 min savetest. Not verified: a quiet-machine figure.



## 16. After the dry runs: the DLL restore, the stock route's exit crash, what a hotfix costs (2026-11-21 16:30; MC #9582)

- **The DLL restore by hash** `[C: code and 3 tests]`: the tier hi_journey stage now records the instance's game DLL
  BEFORE it changes anything (`_dll_before`: the manifest's `dll.path`, its SHA-256, a copy under
  `<work>/hi_journey/kept/dll-before/<sha16>/`) and the removal puts exactly that DLL back (`provision.py set-dll
  --dll <copy>`, then the file is hashed again); a mismatch or an unrecorded DLL is a removal error, which fails the
  stage loudly and keeps the claim. A manifest without `dll.path` fails the stage before anything is changed.
  Replaces `set-dll --original`, which put back the instance's FIRST DLL (5015 run 1, section 14). Outputs carry
  `dll_before` {path, sha256}. Tests (test_tier_hi_journey.py, 30 in all): a DLL changed by an earlier set-dll comes
  back byte-equal; a set-dll that leaves another DLL fails with "after set-dll" and KEPT; no dll.path changes nothing.
  The same fault in the frozen `tools/release/stages/hi_journey.py` is in ticket 0650's scope (MC #9582).
- **The stock route's exit crash in run 2** `[L]` 2026-11-21 16:11:11, c5h-new-a on C:, pid 32108, 0xC0000005, no box
  (dialogwatch #9562; evidence `<instance>/run/crash/20261121_161111_pid32108`): the stock route of release-basic, on
  the stage-laid layout with the build's instance-aware DLL `i0ship-f33fc46a-ia`. Its replay had already passed (63
  steps, exit 0 at 16:11:17, the crash 6 s earlier at the journey's final quit). Read as known cause **K24**
  (known-crash-causes.md: the `-ia` test DLL ends 0xC0000005 about 1 s after the game's exit) because the DLL is the
  `-ia` test DLL, the moment is the quit after the last step, and 0626's rehearsal showed the same at 01:44 [I: no dump
  read for this pid]. What the tier does with it: the journey PASSED and counts as passed; the crash is neither a
  failure nor hidden: the tier report carries one line per such crash (route, journey, time, signature, "K24, after
  the replay passed"). Built 2026-11-21 (fourth lead) `[C: code and 4 tests; not yet seen on a live take]`: after the
  play (and after the stage-laid stock route), the tier hi_journey waits `CRASH_GRACE_S` = 10 s once (the watcher's
  marker comes after its next poll; `C5_TIER_CRASH_GRACE_S` overrides) and matches the pid of every PASSED,
  non-carried row (`row_passed`: exit 0, every checkpoint ok, no `crash`) to the instance's crash evidence since the
  stage started: dialogwatch markers of kind `crash` (`run/dialog_open.json`, `run/evidence/dialog_stale_*.json`,
  key `game_pid`) and crash.collect folders `run/crash/<stamp>_pid<pid>`. Outputs `crashes_after_pass` (route,
  journey, pid, when, signature, code, evidence, `known_cause`, `line`); the stage report and tierreport's section
  "Crashes after a passed journey (reported, not counted as failures)" print one line each ("None: ..." when the stage
  played and none crashed). The verdict and the journey verdicts are untouched. Known cause: K24 when the route is
  stock and the exit code 0xC0000005 [I: route and code, not a dump]; anything else reads "no known cause: file it
  (COMMON rule 29)". A crash DURING a replay stays the original stage's failure (0533, `journey_verdict`).
- **What a hotfix costs** `[L: 5015 run 2 minutes on a busy workstation, C:, 2026-11-21; I: the carried parts]`:

| A hotfix that changes | Carried | Fresh | Minutes |
|---|---|---|---|
| neither server.exe nor a DLL (proxy, shell, tools, Lua), on an accepted base | DLLs adopted; narrowing, census, model, wholegames, savetest, the stock route | survey, testdata, lock, tierraise (1), build of server and proxy (8), purity (3), offline (7), suites (up to 15), the product route of both journeys with install and removal (26) | about 50-60 |
| server.exe | as above, but savetest runs | + savetest Tiny and Standard (14) | about 65-75 |
| the DLL pin (`--carry pins`, as the first beta) | nothing: tierraise stops before build (3 min) | the take is intermediate | the intermediate tier's |
| a DLL change as a carry-series row (`--carry series`) | the stock route unless the 32-bit DLL changed | savetest Tiny + Standard, wholegames Tiny Continents, the DLL builds | measured 2026-11-23/24 (ticket 0639, release-builds.md 5.6): 124.7 min of stages without the journeys + 49 `[I]` = about 174 serial on a busy workstation; all three DLLs rebuilt (52.7), census/model fresh (17.2), wholegames 29.1. Replaced: "not measured (M1 point f; ticket 0639)" |

  Against the owner's 1 hour (D220): only the first row fits, and only with the stock route carried. The owner decides at M1
  whether 1 hour is a rule or a target for the carried case (MC #9582).
- **The quiet run (run 3, 2026-11-22 02:30-04:22)** `[L]`: workstation empty of other games, builds and test runs
  (MC-B #10045, #10053); C: latency 02:30-03:01 from diskwatch: 378 records, 0 above 50 ms, max read 3.77 ms, max write
  0.46 ms, median 0.20 ms; CPU median 10 %. Base `C:/c5-64bit-work/5015/run2b` (`--base-unaccepted`), descriptor
  re-made (SHA-256 1d0ed18e...), checkout 3c0cade10 (team branch with main 3d2c9d80d), work
  `C:/c5-64bit-work/5015/run3`. Minutes per stage (the state): testdata 1.2, build 3.1 (DLLs adopted, server, proxy
  and compositor built here: other repository commit), narrowing/census/model carried, purity 1.2 (fresh: tool
  sources changed), pack 0.0, offline 2.2, **suites 15.0 FAILED** (the diff 5e116ca33..3c0cade10 was a day of main and
  filled the 15 min budget; two suites failed, neither in the tier's code: main's d10 test_pin_defaults, deps/i0.lock's
  release line naming the old i0 build, fixed on main 2dc4ff97f; perf-budget-stock, which refuses a checkout without
  built harness natives, mapped needs=build since), savetest 7.9 (fresh, Tiny + Standard; server.exe new),
  wholegames carried (from the 0626 import; x64 DLL equal), hi_journey 21.2 (product route on the installed layout
  fresh, stock route CARRIED from run2b: stock DLL, EXE build, harness, traces and saves equal; D162 yes). The stages
  after the suites ran by `--only` (savetest, wholegames 02:53-03:01) and the journeys in MC-B's quiet window 04:00-04:22.
  Sum of the stages: 51.8 min, of which 15.0 the failed suites. The gate's line said `GATE FAIL ... at suites
  (failed)`, every other stage PASS, CARRIED or SKIPPED (first live use of the line); "Crashes after a passed journey:
  None". The instance came back by hash (tree differs only in run outputs; DLLs, config.ini, GraphicsSettingsDX11.ini,
  EXE, the owner's tree and the Steam install equal).
- **Quiet figures against the busy ones**: build 3.1 vs 8, savetest 7.9 vs 14, offline 2.2 vs 7, the product route
  with install and removal 21.2 vs 26. For the first beta's gate (base = the candidate, same commit, whole
  adoption) the expected quiet time is about 60-65 min `[I]`: prefix 1, build about 1, offline 2, suites about 1.5
  (safety scan + export gate on an empty diff), savetest 8, wholegames one fresh game (not measured, about 10), product
  route 21, stock route fresh (the candidate's own -ia DLL) about 20; on a busy machine the 90-105 min of section 17.

- **The base take's folder is gone (2026-11-21 ~17:50; MC clean-up of C:/c5-64bit-work/0626)** `[C: code read; L:
  folders listed 18:40]`: dry runs 1 and 2 used `--base-take C:/c5-64bit-work/0626/std/run` and the descriptor
  `C:/c5-64bit-work/0626/Release-5.4.8-i3-rehearsal.json`; both are deleted, so take_tier refuses that base (no
  state.json). Replacement proposed to the MC (thread 0630 #9689): base = `C:/c5-64bit-work/5015/run2b` (dry run 2's
  take: same pins, its own copies of the x64, i0 and i0ia DLLs, build ok, both journeys passed on both routes, a
  tierraise record) with `--base-unaccepted`; the descriptor re-made by `tools/release/evidence/0626/make_descriptor.py`
  and checked against SHA-256 1d0ed18e... (ticket 0626). What the loss does to carried results: the store records
  imported from 0626 (`C:/c5-64bit-work/5015/results`) hold hashes and lines only and still carry, except purity's,
  whose two files were under 0626: `store.carry` finds them gone and the stage runs fresh (designed, about 3 min).
  The candidate take on main reads nothing of an earlier take (no carry in `take_release.py` or
  `tools/release/stages/`); the carry by hash lives only in `tools/release/tiers/` until M1.

## 17. First real use: the first beta's gate (D232, 2026-11-21 21:40; thread 0630 #9787, #9795)

D232 (the owner, one time): the first beta's candidate goes to the owner after the hotfix tier alone; the usual plan runs after it
on the same build. The shape proposed to the MC `[C: code read; I: times]`:

- **Base = the candidate's own take, stopped after pack** (main's driver, `--only` stage by stage up to pack), its
  passed stages imported into the store (`store.py import`), then `take_tier.py --tier hotfix --base-take <cand>/run
  --base-unaccepted` with the candidate's descriptor, fork and `--carry pins`; the D162 line by the team branch's
  `take_release.py --art-check --work <tier work>`; afterwards the usual plan resumes in `<cand>/run` without
  `--only`. A tier take on an earlier take's DLLs (5015/run2b) would test other DLL bytes than the owner gets.
- **Carried**: the three DLLs (adopted by SHA-256), narrowing, census, model, purity (store). **Fresh**: server,
  proxy and shell build, offline, suites (safety scan + export gate when base and head share the tools commit),
  savetest Tiny + Standard, wholegames Tiny Continents, both D162 routes (stock cannot carry: new harness hash and the
  candidate's own -ia DLL). Expected about 90-105 min on a busy machine, not 60; one clean 26577625 instance (mods
  form) for about 50 min and up to 4 headless places for about 30 min.
- **Not covered against the usual plan**: 3 of 4 whole games and savetest's Huge game; the instance stage, refsave,
  drift and fixchecks; EXE build 4390913 and the modpack form; the 43-civ variant; suites outside the changed areas.
- **Open point "exact bytes"**: the tier rebuilds server.exe, the proxy and the shell; `/Brepro` makes builds
  byte-identical from one checkout path (research/05-graphics-composition/model-reader-verification.md V8), not shown
  across work folders. Proposed: adopt the whole x64 and proxy bin from the base by SHA-256 when the base build's
  tools_commit equals this take's and both are clean (MC's answer pending).
- **Answered by the MC (#9800, 2026-11-21 21:36)**: YES to the whole adoption; shape (a)-(d) accepted; 90-105 min
  accepted. Built the same evening `[C: code and tests]`: `tiers/stages/build.py` `same_tools()` + `adopt_bins()`:
  on the base build's repository commit with clean trees on both sides the tier builds NOTHING; it adopts the base's
  `bin/`, `proxy/bin/` (each recorded file against `x64_hashes`, the rest copied and checked equal) and, for i2/i3,
  the compositor (against `composite_sha256`); after the original lays them out, `x64_hashes` must equal the base's
  key by key, else a stop. `adoption.bins` says which way and why. Tests (test_tier_build.py WholeBinAdoption, 5):
  nothing built and the layout hashes equal; a changed base server.exe stops; another commit or a dirty base builds the
  server here; the i2 compositor adopted, not built. Consequence: the tier's build stage takes about 1 min, not 8.
  Corrected 2026-11-22 (ticket 0664, section 18): as built that evening the whole adoption left out the base build's
  generated code `gen/` and `gen-exebuild/`, so the offline stage's rt checks failed 4 of 111 on its first live use;
  the adoption brings both since 0664.
- **The gate's line (MC #9800 (6))** `[C: code and 3 tests]`: tierreport writes `gate` at the top of
  `tier-report.md` and says it at the end of the take: `GATE PASS (hotfix tier, <tag>): <stage> PASS|CARRIED|SKIPPED,
  ...` or `GATE FAIL (...) at <stage> (<status>): <why>; first failing checkpoint: <journey> <route>: checkpoint
  <name>. Nothing is handed over. Stages: ...`. A hi_journey that gated for the owner fails the gate when its D162 check is
  not allowed. A take that stopped before tierreport gets the line from `python -I tools/release/tiers/stages/
  tierreport.py <work>`.
- **The installer of the early hand-over (MC #9800 (5))** `[C: tools/installer/build_installer.py docstring and
  code; tiers/stages/hi_journey.py build_installer]`: the tier's product route plays `build_installer.py --work
  <tier work> --test-build` (standard variant only, name `-TEST`, welcome text marked test; D162 check skipped; VP's Lua
  fixes from the default Community-Patch-DLL clone, which holds 475bd4997, 14d16fa3d, aa7a29b38 [L 2026-11-21]). A
  real installer needs `take_release.py --art-check` to pass on its work folder: on `<cand>/run` it cannot before the
  usual plan's hi_journey; on the tier's work folder it passes after the gate (the team branch's art-check reads the
  tier's hi_journey). Staged files carry a fixed time, so the same work folders give the same payload bytes
  (build_installer docstring; installer-notes 12). The pack manifest has no time stamp, so `<cand>/run` and the tier's
  pack are the same pack exactly when their `manifest.json` files hash equal.

## 18. Whole adoption and the generated code; what each later stage reads from the build folder (ticket 0664, 2026-11-22)

- **Found** `[L]` by the MC in the first beta's gate, 2026-11-22 05:52-05:53 (base `C:/c5-64bit-work/cand1/run`,
  main 06d9e820d; log `cand1/tier/logs/offline-rt.log`): after a WHOLE adoption the tier's
  `build/x64-<commit8>/` held `bin/`, `deps/` and `proxy/` but not `gen/` and `gen-exebuild/`, and the offline
  stage's `proxy/tools/test_proxy_rt.py --build <x64_out>` failed 4 of 111 checks ("borrowed: the build's method-key
  table has keys (0)", "loader: vtable slots from the generated code [None None None]", "shell loader: proxy,
  stand-in and SetDLLIFace slot present", "guard: loader and proxy present"). The MC's one-time workaround: both
  folders copied from the base's build folder (25 files, list `cand1/tier-gen-copied.sha256`), the tier rerun
  `--from offline`. Not caught earlier: the hermetic tests checked only the adoption itself, and quiet run 3 built the
  server (another commit), so whole adoption had never run live.
- **What rt reads under `--build`** `[C: test_proxy_rt.py at 0e5b19d42]`: `bin/` (the X64_FILES it lays out),
  `proxy/bin/CvGameCore_Expansion2.dll`, `gen/common/vp_ids.gen.cpp` (the method keys), `gen/common/vp_ids.gen.h`
  (`kAppSchemaHash`), `gen/proxy/proxy_ifaces_*.gen.cpp` (vtable slots), `gen/proxy/proxy_hook_defaults.gen.cpp`. It
  does NOT read the build's `gen-exebuild/`: it emits its own EXE table header into its scratch (`exebuild_gen`).
- **The fix** `[C: code and tests]`: `tools/release/tiers/stages/build.py` `GEN_DIRS = ('gen', 'gen-exebuild')`;
  `adopt_bins()` checks, before anything is copied, that the base build has each with at least one file (else a stop
  "the base build has no generated code ..."), copies both beside `bin/` and `proxy/bin/`, and records each file under
  `adopted_unrecorded` with its copy checked equal to the base's file (`_copy_equal`; the base take keeps no hash
  record of generated code). The base's tree is copied rather than regenerated: it is the code the adopted server and
  proxy were compiled from (whole adoption requires the same repository commit anyway). A partial record of a whole
  adoption whose folder lacks `gen/` (made before the fix) is not resumed but adopted again. The `ADOPTION: WHOLE`
  line and `adoption.bins` now name the generated code.
- **Tests** (`tools/release/tests/test_tier_build.py` WholeBinAdoption, 4 new, 25 in the file): every generated file
  adopted, byte-equal and under `adopted_unrecorded`; a base without `gen/` is a stop with nothing built; a partial
  record without `gen/` is adopted again; and the OFFLINE STAGE RUN on the adopted tree (plan prefix + build +
  offline) with a fake rt that requires exactly the paths `test_proxy_rt.py` reads under `build_dir` (taken from its
  source by regex, so a new read there is picked up without editing the test). Negative controls 2026-11-22 08:37:
  with `GEN_DIRS = ()` all four fail, the offline one naming the same four paths as the live failure; without the
  resume guard the resume test fails.
- **Every other stage of the hotfix plan** `[C: code read at 0e5b19d42]`, for a file read from the tier's build
  folder that adoption does not bring: none found. narrowing reads the record's `narrowing_gate`; census, model,
  purity read `x64_src` (the base take's source tree: the adopted record names the base's path, read in place,
  so the base's `src/` must stay intact for the tier) and purity `x64_bin`; pack reads `x64_layout`; offline
  `hutest`/`scenarios` read `bin/`, `proxy/bin/` and `deps/stubs/` (Lua source, FireWorks.lib; brought by
  `adopt_x64`'s copy of `stubs/`) and regenerate their own code from `proxy/model`; offline `verify`
  (run_proxy_game.py) reads only `--bin` and `--proxy`; suites read the repository and the build records'
  `tools_commit`; savetest and wholegames read `x64_bin` and `i0_dll`; hi_journey reads `x64_layout`, `proxy_dll`,
  `i0ia_dll` and, through `build_installer.py`, `x64_out`'s `build-summary.json` (found by `rglob`, in
  `deps/dll/`, which `adopt_x64` copies); tierraise reads the BASE's `x64_out`; the results store reads only
  `x64_hashes` and the record. Folders of the base's build that are still not adopted and that no stage reads:
  `server/` (its `gen/`, objects, `build_server.log`), `render/`, `zlib/`, `proxy/obj`.

## 19. The first beta's usual plan after the gate: the old journey stage has no composite (MC, 2026-11-22 11:48) [L]

The first beta's candidate (main 06d9e820d, C:/c5-64bit-work/cand1) passed the hotfix tier's gate at 08:16 and went to
the owner at 08:20 (D232); the usual plan then ran on the same build in `cand1/run` (`take_release.py --tier validated`
without `--only`, 08:18-11:47). Times: savetest 08:21-08:43, drift and fixchecks to 08:47, refsave, instance (arm-tiny
265 s, arm-huge) to 09:05, **wholegames 09:05-11:09 (7,417 s, every game passed: tc 445 Diplomatic, sf 401 Science,
sp 363 Cultural, x64 and x86 each)**, hi_journey 11:09-11:47. Verdict: **validated tier not reached**: `hi_journey`
STOP, release-large failed on the product route.

Cause [L]: the validated plan's `hi_journey` (tools/release/stages/hi_journey.py) lays the product with
set-proxy / set-dll / pack-on and **never lays `shell_composite.dll`** (no step or log mentions it), so its product
route is not the shipped product (the server's world is not composited) and the harness takes PrintWindow pictures on
it (`capture.composite_active` false; trace: release-basic-product 68 and release-large-product 12 pictures, all
`printwindow`; the gate's tier stage on the installer-laid layout: 26 of 26 `composite`). At release-large's checkpoint
`attacked` (step 11, a right-click attack, 4 s wait) the PrintWindow top band (rows 0-39) holds the world as well as the
top bar, the fight animates under it, and three takes disagreed with the reference and each other ("top band 38
levels from the reference (printwindow, 2 retakes)", exit 3). The picture itself is a normal game frame (the attack,
"Promote a unit"); the stock route passed all five checkpoints. So: a harness limit of an out-of-date stage, not a
product fault. D162 for the hand-over rests on the gate's `hi_journey` on the installer-laid layout (D221: installed layout,
stock carried), which passed both journeys. Fix: ticket 0671 (the validated plan's journeys on the installer-laid
layout as the tier's are, or at least with the composite; and a band test that ignores the world). One release-basic
stock exit crash at 11:29 was 0484's known -ia exit crash (handled).

The same top-band failure stopped the second candidate's gate at 13:44 on the STOCK route of release-large
(cand2/tier3, checkpoint `attacked`, "top band 29 levels", PrintWindow, 2 retakes) while its product route on the
installed layout passed all five checkpoints: the band, not the layout, is what every gate's stock route meets.

**Fixed by ticket 0671 (2026-11-22, branch task-0671-hi-journey-composite)** `[C]` code and tests; the live run of
part (3) is the ticket's (its log says whether it ran):
- **The band** (`tools/hi/capture.py`): the top bar only, client rows 0-23 and 480 client columns from each side.
  On this run's pictures the attacked checkpoint's band difference falls from 38.3 (cand1 product) and 29.4 (cand2
  stock) to 0.0, and probe 4501's UI-less frames rise from 55.8 to 159.6 levels. Measurements and the regression test:
  [human-input-testing.md](human-input-testing.md) 4.7a.
- **The validated plan's journeys** (`tools/release/stages/hi_journey.py`, `installed_path_why_not`,
  `_run_tier_path`): with the default tools and neither `C5_RELEASE_HI_INSTALLED` nor `C5_RELEASE_HI_ROUTES` set,
  `take_release.py --tier validated` hands the stage to the tier stage of section 5 (installer test build from this
  take's build and pack, a clean instance of EXE build 26577625, the product route on the installed layout with the
  composite, the removal, the stock route fresh on the stage-laid layout: a validated take has no base take, so
  nothing is carried). D162 is the tier's gate (its art_check equals the original's when nothing is carried); a stop
  names `take_release.py`, not `take_tier.py`. The stage's inputs gain `path: tier stage (0671)`, so a result recorded
  on the old layout is not reused as one of the new path. `C5_RELEASE_HI_STAGE_LAID=1` plays the old layout
  (diagnostic only: no composite, PrintWindow). Tests: `tools/release/tests/test_hi_journey.py` class `TierPath`
  (5 tests; the older fixtures run with fake tools and keep the stage-laid path).
- **Cost** `[L]` from the tier stage's own step seconds on the two candidates (cand1/tier, cand2/tier3 reports):
  installer 12-14 s, claim 6 s (cand1's 4,260 s was the operator's own claim held on the instance: runbook 1.12, 70 min lost), player_state, listings,
  install and removal together 35-45 s, product route 1,206-1,600 s, stage-laid stock route 1,125-1,165 s: about
  40-46 min, as the stage-laid plan took before (38 min in cand1/run).
- **Carry after the merge** `[C]`: `tools/hi/capture.py` and `tools/release/stages/hi_journey.py` are in the tier
  stage's HARNESS_PARTS, so the first tier take after this merge plays its stock route fresh (stock inputs changed;
  about 20 min more than a carried route) and carries again from then on.
- **Part (3), first live run: incomplete** `[L]` (2026-11-22 14:29:59-14:42, c5h-new-a on C:, EXE build 26577625, mods
  form; work folder `C:/c5-64bit-work/0671/take`, a copy of cand1/run's state, layout, pack and inputs; installer
  `VP64-Patch-x64-1b87dd18-p06d9e82-for-VP5.4.8-TEST`, std, config i3). The new path ran as designed up to the
  second journey: installer build 12 s, install 6 s, product route of **release-basic passed 6 of 6 checkpoints on the
  installed layout in 541 s**, and its trace holds **67 `composite` pictures and one `printwindow`**: step 57 (trace
  step 59, the 30 s wait on the leader screen of an in-game load), where `capture.composite_active` is false because
  the game has no `server.exe` child between the old game's server and the new one's (no `fallback` key: no dump
  failed). Every checkpoint picture was `composite`. Then the driver process ended at 14:42:11 while release-large's
  product launch waited for the front end: no traceback, no `end` line in `take-run.log`, so it was ended from
  outside (`[I]`: the shell that ran it in the foreground), leaving the game running and the installed layout on the
  instance until the workstation restart about 15:30.
- **Recovery** `[L]` (2026-11-22 15:40-15:44, by hash, no game launched): the journeys' three saves removed (copies
  in `take/hi_journey-run1-1429/recovery-removed-saves`; that run's outputs and logs were moved to
  `take/hi_journey-run1-1429` and `take/logs-run1-1429` before the second run), GraphicsSettingsDX11.ini and
  UserSettings.ini back from `kept/` (f1d9cf01, 85f9d806), `frontend.py restore` (MainMenu.lua was still the patched
  copy), the installer's removal exit 0, the tree before vs after the removal 0 differences outside the run outputs
  (128 under Logs/cache), set-dll back to the DLL before the run (ff0f6e86), `provision.py verify --quick` exit 0,
  claim released. Commands: runbook 3.4a (written by MC-B at merge; runbook.md is outside ticket touches, MC #9976).
- **Part (3) PASSED** `[L]` (2026-11-22 15:55:23-16:44:05, 49 min, the same command started in the background
  after the owner's playtest, MC-B GO #10396; c5h-new-a, EXE 26577625 stamp 0x6AF3837D, 15,466,264 B; take
  `C:/c5-64bit-work/0671/take`, report `take/hi_journey/report.md`): verdict **passed on the installer-installed
  layout, for the hand-over (D162): yes**, take_release exit 0.
  - Product route on the installed layout: release-basic's first try stopped at step 10 on the known flake "city
    screen did not auto-open after founding" (human-input-testing.md 8.5); the stage's one rerun passed 6/6 in 551 s.
    release-large passed 5/5 (`attacked` included) in 439 s.
  - Picture sources (the traces, acceptance (1) as read below): product release-basic rerun 62 step + 6 checkpoint
    pictures, all `composite`; release-large 21 + 5, all `composite`; the stopped first try 8 `composite` and 1
    `printwindow` (its step 6, the front end's load screen before the game, no `server.exe` child). Stock routes:
    62 + 6 and 21 + 5, all `printwindow` (as designed), and the stock `attacked` checkpoint passed with the new band
    (the failure that stopped the second candidate's gate).
  - Removal after the product route: installer removal, tree check, set-dll back, `verify --quick`, release, all exit
    0; the stock route on the stage-laid layout then restored by hash (set-proxy --original, set-dll back, frontend
    restore, verify exit 0) and released c5h-new-a at 16:43:53. Comparison product vs stock: release-basic same 6,
    release-large same 5.
  - Stock route's test pack: `pack.py` exit 1 ("purity.tsv" missing) because the take COPY has no `purity/` folder
    (`scripts/make_take.py` copied only layout, pack, inputs); the stage went on without a pack, which the stock
    route (mode=off, the 32-bit DLL) does not use. A take copy for this stage should also copy `purity/` `[L]`.
  - One crash without a box, **K24**: pid 28156, 0xC0000005 at 16:34:19, the release-basic STOCK game at the
    journey's final quit after its replay passed (exit 0 at 16:34:25), with the `-ia` test DLL i0ship-f33fc46a-ia
    (6c8cd5f4). No crash.txt and no WER dump (no box), so module and offset cannot be compared: the class rests on
    route, code, moment and DLL `[I]`, as in section 16. Evidence `run/crash/20261122_163419_pid28156` on the
    instance (dialogwatch note #10426).
- **Acceptance (1) as read since 2026-11-22 (the MC, #10378, answering #10373):** "every product picture of a drawn
  world, and every checkpoint, composite". A picture taken while the game has no `server.exe` child (the load screen
  of an in-game load, no world drawn) may be PrintWindow; a run's report counts both kinds. Replaces the ticket's
  first wording "every product picture 'composite'".

## 20. The x64 DLL in tier I: adopted by its build key (MC #10515, 2026-11-22 18:3x; lead-0630, ticket 5024)

**The finding** `[C]` (byte comparison of the two DLLs, `C:/c5-64bit-work/0630/b-scratch/dll_diff.py` and
`dll_strings.py`, 2026-11-22 18:20). The first live tier-I build (5024's step 0: `take_tier.py Release-5.4.8 --tier
intermediate --base-take C:/c5-64bit-work/cand2/tier3`, from the clean team head 28bef5de0) built the x64 DLL of the
unchanged pin 1b87dd185 fresh in 735 s (link place wait 0 s; compositor 94 s beside it) and stopped LOUD at
18:16:58: the DLL hashed 3C1A64AE..., the base's 4C5DA20E... (built by cand1/run from 06d9e820d; cand2/tier3 adopted
it), with tools/x64-dll/, server/build_server.py and the compiler lines unchanged. The two files have the same size
(23,725,056 B) and differ in 1,333 bytes in 134 runs: the PE TimeDateStamp (/Brepro: a hash of the output) and
single bytes of RIP-relative .rdata references; the ONLY printable string in one file and not the other is the time
`10:18:33` against `23:01:26`. Cause: the fork's startup lines (CvDllContext.cpp, CvGlobals.cpp) use `__DATE__` and
`__TIME__`, and `tools/x64-dll/build.py` fixes them with SOURCE_DATE_EPOCH = **the commit time of this repository's
HEAD** unless the caller sets it (issue 0115): 06d9e820d was committed 2026-11-22 05:18:33 EST (10:18:33 UTC),
28bef5de0 18:01:26 EST (23:01:26 UTC). So the x64 release link repeats byte for byte only from the same repository
commit; the earlier rule (MC #10412: always build x64 in tier I, LOUD on another hash of an unchanged pin) would have
stopped every tier-I take, and no store record keyed on the x64 DLL's bytes carries across repository commits.

**The rule** (MC #10515, decided over a SOURCE_DATE_EPOCH-from-the-base fix that would have put the base's build date
into a candidate's DLL: "no shipped DLL may carry a false build date"; the rejected diff is kept in the lead's scratch
`epoch-fix-rejected.diff`, never committed): in a tier other than hotfix, with a base take, the x64 DLL is ADOPTED by
SHA-256 from the base, as the 32-bit DLLs are (D127 as carried by D221), when its build KEY is unchanged:
- **the pin**: the lock's x64 commit (deps/dll.lock's x64 row), with the major count and the build inputs
  (ADOPT_INPUTS);
- **the build script and its inputs**: the git tree id of `tools/x64-dll/` at the repository commit that BUILT the
  base's DLL (followed through adoptions: x64_build.adopted_from, then each take's config.base_take; cand2/tier3 ->
  cand1/run -> 06d9e820d), against this checkout's (uncommitted changes there: unknown);
- **the toolchain**: clang-cl, lld-link and MSVC VCToolsVersion in `store.compiler_of`'s form (this machine's read
  from LLVM's `--version` lines and vcvars64; the base's from its dll step's log).

Same key: the base's build_all output is adopted into `<work>/build/x64-<pin>` (every DLL checked against the base
layout's hashes), `server/build_server.py --deps` builds the server and the proxy against it, the compositor is built,
and the laid-out DLL must equal the adopted one; a different DLL there is the only LOUD stop left ("same x64 build key,
different bytes"), with the normalised hash as a diagnostic. A part changed: the x64 DLL is built fresh with the
default SOURCE_DATE_EPOCH, a different hash is expected, and it counts as a CHANGED DLL (savetest, fixchecks,
wholegames and instance run, their store keys holding the DLL's bytes). A part cannot be read: built fresh, counted as
changed, said in `adoption.ways.x64`. `outputs.x64_key` records the key, both sides' parts and why. **The normalised
hash** (`normalised_sha`: the PE TimeDateStamp and every `__DATE__`/`__TIME__`-shaped string masked) is a diagnostic
only, never a gate.

Code `[C]`: `tools/release/tiers/stages/build.py` (THE x64 BUILD KEY, `x64_key`, `dll_origin`, `tools_tree`,
`current_toolchain`, `adopt_x64_here`, `normalised_sha`, `run_fresh_x64`); config `x64_toolchain` is a test hook.
Tests: `tools/release/tests/test_tier_build.py` class IntermediateAdoption (10): same key adopts (server and proxy by
`--deps`, no build_all, the laid DLL the base's); the pin, the toolchain or the tools/x64-dll/ tree changed: built,
counted as changed; toolchain unknown, a dirty base tree, an adoption whose base names no base take: key unknown,
built; the adoption chain followed to the take that built the DLL; the normalised hash masks only the date. Replaced:
the `x64_tools_changed` narrowing of the LOUD stop (validation-tiers-m2.md section 3, corrected there).

**Pitfall for anyone comparing x64 DLLs of one pin** `[C]`: two release builds of one pin from two repository commits
differ in their build time string and everything after it in .rdata; compare from one commit, or with the normalised
hash, never by the plain SHA-256 alone.

## 21. The scenes gate after a library change (MC #10544, 2026-11-22; ticket 5024, worker W4, merged 48f80cfff)

**Why** `[L]` (5024's live check, 2026-11-22 19:15-19:23): at 19:11:02 ticket 4749 re-recorded 8 kit bundles in the
master `F:/c5-64bit-work/scene-library` (C1MOUNTAIN, C2ANATURAL, C3AGARRISON, C3BGARRISON2, C4PORT, C5AIR, C6CARRIER,
C7INTERCEPT; format 10, unit ground shadows; new bundle ids; the old ones moved to the master's `retired/`). The 17:00
base set (`Release-5.4.8__1e7bf6918695834c`, drawn by the base's player at 33c3fcb44, a format-9 reader) covers the old
ids. The gate as first built stopped on "a bundle the base set has no picture of" and on "a base-set shot the run did
not make", so every take after a re-record would stop; and across a format bump no new base set can be drawn by the
base's player. The live check's own player (dde0d762e, format 9) refused the 8 new bundles (`NewerVersion`, 17
refusals) and drew the other 8: 65 shots, all `equal`.

**The rule** `[C]` (code and hermetic tests: `tools/release/tiers/stages/scenes.py`, `tierreport.py`;
`tools/release/tests/test_tier_scenes.py` class LibraryChange, 12 tests; 13 of 13 mutation negatives failed as they
must; not yet live):
- **(C) like for like.** A base-set bundle no longer in the library copy is copied at the stage's start from the
  master's `retired/<id>/` to `<work>/scenes/retired/bundles/<id>/`: every file hashed while read from the master and
  again from the copy (the list in `<work>/scenes/retired/copy-record.json`), the bundle's scene.json SHA-256 checked
  against the one the base set drew (the set manifest's new `retired` record, else the master index file whose bundle
  hash equals the manifest's `library`; none: copy-equals-source only, recorded as `retired_index`). A second scenerun
  draws the copies (`--library <work>/scenes/retired --p1 off`) and compares them with the base pictures. Nothing is
  read from F: while drawing (F: stalled 35 of 61 min on 2026-11-22). A copy whose master listing is unchanged is
  hashed again and kept.
- **(A) first sight.** A library bundle the base set does not cover is drawn, listed (outputs `first_sight`; tier
  report section "First-sight scene bundles"), not compared, and its pictures join this take's set.
- **Stops:** differs (the accept file as before); P1; nothing left to compare; LOUD "the retire rule was broken" (a
  base-set bundle in neither the copy nor `retired/`); the master not reachable; a retired bundle below the player's
  read floor (`kOldestReadFormatVersion` of `server/src/render/srv_scene_bundle.h` at the take's commit, 9 today, or
  the player's own `OlderVersion`); a library bundle refused as `NewerVersion` (rebuild the candidate from a newer
  main); a copy that differs from its source; a retired bundle whose scene.json is not the one the base set drew.
- **Settings:** the master from config `scene_master`, else the copy's `scenelib-manifest.json` `master`, else
  `C5_SCENELIB_MASTER` (outputs `master`, `master_from`). Carry part `retired` (`none` when nothing is retired; a base
  result recorded before this change reads as `none`).
- **Correction** `[C]`: a set manifest's `library` is NOT the SHA-256 of an index.json file but a hash of {bundle id:
  scene_json_sha256} (scenes.py `live_parts` and the bootstrap). Of the master's index files only
  `index-before-4749.json` gives the 17:00 set's value `4cc7b6f5...`.
- **Sizes** `[C, du]`: the 8 retired kit bundles are about 2.8 GB (C1MOUNTAIN 351 MB, format 9, at the floor).
- **Design choice** `[I]`: the retired run uses `--p1 off`: a retired bundle's dumps came from an older server, so P1
  against them says nothing about the candidate.

## 22. The fourth beta candidate's take (ticket 0725, D248, 2026-11-24) [L]

A hotfix tier over the first candidate's take `C:/c5-64bit-work/cand1/run` (`--base-unaccepted`, as beta 3), from one
clean checkout of main at the MC's CUT dc259f0b1 (`C:/c5-64bit-work/cand6/wt`, a detached git worktree), descriptor
`cand6/Release-5.4.8-beta4.json` = cand5's beta 3 descriptor with only `notes` changed, `--carry pins --for-owner
--jobs 4`, `C5_TIER_EXE_BUILD=C5_TIER_BASE_EXE_BUILD=26577625`, HI instance c5h-new-a (mods form, build 26577625),
pool and job owner `0725`. Scripts: `tools/release/evidence/0725/` (`prep.sh`, `beta4_take.sh`, `suites_by_hand.sh`,
`installer.sh`).
- **Flag rename** `[C]` (help at dc259f0b1): `--for-art`, `--not-for-art` and `--art-check` are hidden aliases of
  `--for-owner`, `--not-for-owner` and `--owner-check` since ticket 0708; the old names still work and print a note.
- **Pins unchanged since the base** `[C]` (`git diff 06d9e820d dc259f0b1 -- deps/`: only 9079's `stress-43` lines in
  `dll.lock`), so the build stage adopted the DLLs by hash and built only server, proxy and composite: no LTO link
  (D121 place not taken). x64-server 159 s, shell 93 s; build stage 21:30:49-21:35:18.
- Stage times (2026-11-24, busy workstation): prep and survey to tierraise 21:28:22-21:30:49 (game copy about 90 s,
  export preflight 40 s); model fresh 183 s (tool_sources changed); purity fresh 83 s; offline 21:39:46-21:43:10
  (rt 93 s, hutest 31, scenarios 44, verify 36).
- **The suites STOP again** (21:43:11, the runbook 1.12 TRAP): the hotfix selection's recorded times 1312 s over
  the 900 s budget, the same seven suites as beta 3. By hand: `suites_by_hand.sh` = `run_suites.py --diff
  06d9e820d..dc259f0b1 --tier intermediate --lanes 3` through `quietlock.py run`, 21:43:30-22:09:05 (1535 s): 24
  passed, 1 failed, 14 not run. The failure: perf-tests 1 of 329, `test_turnstudy.py::test_real_files_load` (arm
  `S4s5032` added by 5032 at 0a23171fd without the test's set): failed on the cut, test-only, fixed on main after the
  cut (53c153d1c, MC); it ships nothing and did not hold the take.
- **New since beta 3** `[L]`: `exebuild-tables` is NOT RUN by a plain `run_suites.py` (it ran in beta 3's by-hand run):
  it now needs `--game-files` (golden EXE copies on F: and the local exe tables). Run it by itself:
  `run_suites.py --suites exebuild-tables --game-files --tier intermediate --out <dir>`.
- **The installer suite takes the focus** (MC #13407, ticket 0728): `tools/installer/tests` puts an Inno "Setup" window
  in the foreground at each test install; on the workstation the owner could not type during it (21:43-21:48). Do not
  start it on the workstation in a take until 0728 is fixed; the tier's journeys install through
  `run_installer.py` on a hidden desktop and are not affected `[C: run_installer.py docstring]`.
- **The resume** (`TIER_FROM=savetest`, 22:09:13-23:26:12, 77 min): savetest fresh (new server.exe) 596 s PASS;
  wholegames fresh (tool_sources changed) 22:19:10-22:42:54 PASS (Tiny Continents, x64 = x86; the x64 game ended
  Diplomatic T445 in 598 s); hi_journey 22:43-23:26 PASS, both routes on c5h-new-a (26577625): product (installer-laid)
  release-basic 542 s, release-large 441 s, removal 4 s; stock release-basic 524 s, release-large about 7 min.
  One crash after a passed journey `[L]`: stock release-basic at its final quit, pid 47144, 0xC0000005, no box,
  23:16:44, 6 s before the replay's end (65/65 steps, exit 0): read by the tier as K24 (section 16), reported in the
  tier report's "Crashes after a passed journey", not a failure. The dialog watcher's marker ends any
  `wait_for.py --instance` at once (exit 8) until the stage's next launch moves it.
- **The result**: tier report first line GATE FAIL at suites, only for the budget STOP (every other stage PASS or
  CARRIED, as beta 3's take); `take_release.py Release-5.4.8 --owner-check --work cand6/tier` exit 0 ("yes, both
  journeys passed on this build, product and stock"; played on server 6c75d7fa..., x64 DLL 4c5da20e..., proxy
  48524398..., -ia DLL 6c8cd5f4...). Installer `build_installer.py --work cand6/tier --out
  <Desktop>/c5-64bit-patch-deliverables/0.1b4-std-only --release-name 0.1b4-std-only` 22 s:
  `VP64-Patch-0.1b4-std-only-for-VP5.4.8.exe`, 10,152,549 B, SHA-256
  84980836b35f589b13d936854ecae77afe1d080a7731104119a4bbe99b423308 (23:26:42). Wall clock from the CUT checkout to the
  installer: 21:27-23:26, about 2 h, of which 26 min suites by hand.

## 23. Beta 4's intermediate tier on the same build (ticket 0734, D245 part 2, D221, 2026-11-24/25) [L]

The rest of the release plan over the build of installer 0.1b4-std-only (SHA-256 84980836..., cut dc259f0b1), run after
the owner had started playing it. `take_tier.py Release-5.4.8 --tier intermediate --base-take C:/c5-64bit-work/cand6/tier
--base-unaccepted` (the hotfix take of section 22: its gate line is FAIL only for the suites budget STOP, D162 yes),
from the CUT checkout `cand6/wt` (detached, clean, dc259f0b1), work `C:/c5-64bit-work/0734/tier`, store `cand1/results`,
descriptor `cand6/Release-5.4.8-beta4.json`, `--carry pins --for-owner --jobs 4 --places headless=4,game=3,gpu=1`,
`C5_TIER_EXE_BUILD=C5_TIER_BASE_EXE_BUILD=26577625`, `C5_DOCUMENTS` in the work folder. Instances (MC-B #13722):
c5h-new-a (HI journeys, then the sweep), c5h-a (oldexe, 4390913), c5h-d (instance stage, 4390913, on F:), c5h-new-mpk
(modpack, named save `C5H-T0734-MPK-SRC` put and removed by SHA-256 5bb63d47...). Scripts `tools/release/evidence/0734/`.
Wall clock on a busy workstation (other agents' builds beside the barrier), not a D106 timing claim.

**The result.** Every stage that ran passed; the tier report's gate line is `GATE FAIL ... at suites (pending)` only
because the tier's own suites stage was not used (below); `take_release.py --owner-check` on the tier: yes (product fresh,
stock carried). No game crashed (no crash folder on any of the four instances after 02:20; the tier report's "Crashes
after a passed journey": none), so K24 (the `-ia` DLL's exit crash, section 16) did not occur in this take: the stock
route, where K24 occurs, was carried, not played.

**Same build** `[L]`: the intermediate tier always builds server, proxy and compositor (build.py `run_fresh_x64`, no
whole adoption above the hotfix). Rebuilt at the same commit, server.exe (6c75d7fa...) and the proxy DLL (48524398...)
are byte-equal to the installer's payload and to cand6/tier; the pack equals cand6/tier's file for file; the DLLs were
adopted (x64 by its build key, 32-bit and -ia by SHA-256). Only shell_composite.dll differs (installer fc01cafa..., this
take 009000e2...): 77 of 1,950,208 bytes, at the PE time stamp (0x108) and the debug directory, the build stamp [I].
(validation-tiers-m2.md 8.5 item 3 corrected with this.)

| Part | When (EST) | Result |
|---|---|---|
| light stages, `--only` each | 2026-11-24 23:56:21-00:04:59 | survey, lock, tierraise (export preflight 50 s) PASS; branches skipped (`--carry pins`); testdata PASS (snapshot exit 1 = the accepted DLC map-script failures); build 413 s (server 258 s, compositor 120 s, through quietlock); narrowing, census CARRIED from cand1/run; model, purity CARRIED from cand6/tier; pack PASS |
| wait for the owner to stop playing | 00:05-01:36 | BARRIER GO MC-B #13722 (MC #13711) |
| suites barrier by hand, run 1 | 01:36:13-01:37:09 | export-gate PASS (50 s), safety-static PASS; the 33 other suites dropped by the run_suites trap below |
| suites by hand, run 2 | 01:38:01-02:11:26 | 32 PASSED, 2 failed: perf-tests 1 of 329 failed twice, `test_turnstudy.py::test_real_files_load` (arm S4s5032): failed on the cut, test-only, fixed on main 53c153d1c after the cut (MC #13420, as in 0725); queue TIMEOUT at 900 s with 62 of 111 tests done, all passing, beside two builds started after the hold and the bootstrap |
| queue alone (run 3) | 02:12:05-02:24:02 | PASSED 111/111 in 716 s: the timeout was load, not a hang |
| scenes bootstrap of cand6/tier, beside the suites | 01:36:14-02:02:35 | PASS: player built at dc259f0b1, 16 bundles, 137 shots -> `cand1/results/scene-baselines/Release-5.4.8__42a3abf1d0ebd380` |
| `--from offline` | 02:24:27-04:12:54 | exit 1, on fixchecks only (below) |
| one resume: `--only fixchecks`, `--only wholegames`, `--only tierreport` | 04:13:21-05:02:57 | all ok |

Stages of the game part (each PASS):

| Stage | Start-end | Wall | Result |
|---|---|---|---|
| offline | 02:24:28-02:29:56 | 5.5 min | rt 235/235 (114 s), hutest 55 checks 0 failed (62 s), scenarios, verify |
| scenes | 02:24:28-02:56:09 | 31.7 min | sceneshot build 255 s, scenerun 1,643 s: 137 equal, 0 differ, 16 bundles, 0 first sight; this take's set `Release-5.4.8__f76c7511e1557182` |
| instance (Tiny arm) | 02:24:28-02:29:37 | 5.2 min | c5h-d: 183,496 verified, 0 mismatches |
| oldexe | 02:24:28-02:40:36 | 16.1 min | c5h-a (4390913): release-basic product route, 63 steps, 656 s, every checkpoint ok |
| modpack | 02:29:38-02:34:54 | 5.3 min | c5h-new-mpk: test installer built, install 9 s, play 164 s, removal 21 s |
| savetest | 02:29:58-03:01:12 | 31.2 min | fresh (the stored record covered tiny and standard, the tier needs huge too): D10 save identity PASS, tiny 1,376 s, standard 1,478 s, huge 1,859 s; 10 round trips IDENTICAL, 11 IDENTICAL UNDER EXCEPTIONS |
| sweep | 02:34:55-03:48:03 | 73.1 min | c5h-new-a, HI-KIT-MEDIEVAL-LARGE-T200 product route: phase verdict PASS, 0 findings, 13 sessions, 1 relaunch, 8 reloads; 4 of 5 screens; unit_panel failed "plot 47,47 is off screen (predicted -7663,-5704)", the sweeper step defect of take 7 (validation-tiers-m2.md 8.4i), not the product |
| hi_journey | 02:56:10-04:12:54 | 76.7 min, of which 03:00-03:51 waiting for c5h-new-a (held by the sweep, claim retried every 120 s) | c5h-new-a, installer-laid product route fresh: release-basic 592 s, release-large 444 s; stock CARRIED from cand6/tier (stock DLL, EXE build, harness, journeys equal); D162 yes |
| fixchecks, try 1 | 03:01:12-03:03:07 | 1.9 min | FAILED on 0084 (below); 0096 passed |
| fixchecks, the resume | 04:13:23-04:15:47 | 2.4 min | 0084 PASS (75 s), 0103 PASS (new-off vs new-on IDENTICAL, 69 s); 0096 kept from try 1 |
| wholegames | 04:15:50-05:02:56 | 47.1 min | four games on local1-4, x64 = x86 in every one (parity identical): tc Tiny Continents Diplomatic T445, mp Small Pangaea Diplomatic T325, sp Standard Pangaea Cultural T363, sf Standard Fractal Science T401 (local4 1,395 s, local1 1,652, local3 2,508, local2 2,827) |

**Fix check 0084's failure: a flake of the test's locker, not of the product** `[L]`. In try 1 the fixed arm kept
every row (12,426 = control, no "row dropped" line; fixed-nolock equals control), but the test's own lock thread
(Python sqlite3, `BEGIN EXCLUSIVE`/`COMMIT` on the run's `cache/stats.db`) raised `OperationalError: disk I/O error`
after 16 windows (11.4 s locked), and `0084_lock_test.py` counts any locker error as a setup FAIL. By hand, outside the
driver, the same command passed twice (03:04-03:06, fixed arm 28.7 s and 29.0 s against 35.1 s in the take, locker
error None), and the resume passed it. Root cause of the I/O error not shown [I: a race of the locker with the server's
own file handling under load]. Try 1's evidence: `C:/c5-64bit-work/0734/fixchecks-try1/`.

**What it cost.** After the failed stage the side-by-side driver started nothing new, so wholegames, waiting for
fixchecks' headless place, did not start at 03:03 (it would have ended about 03:50, before hi_journey). The resume ran
it 04:15-05:03: about 50 min. The tier's measured time without the wait for the GO: light 9 min + barrier 48 min + game
stages 1 h 48 min + resume 50 min = about 3 h 35 min (the tier report's 306.6 min counts from the first light stage and
includes the 91 min wait).

**Traps found** `[C]`/`[L]` (runbook 1.12 has the commands):
- `run_suites.py --suites S,T --range A..B` selects by the DIFF A..B: `select()` tests `a.diff` (set from `--range`)
  before `a.suites`, so only the always-suites ran. Give `--range` only with `--all`; export-gate runs with `--range`
  as the always-suite.
- The tier's suites stage runs `--all`, which holds the installer suite (0728): on the workstation while the owner may
  use it, run the barrier by hand without it; the tier's `suites` then stays pending and the report reads GATE FAIL.
- A base take without a scenes stage needs `scenes.py bootstrap` first (26 min), else scenes STOPs.
- A failed stage stops new starts as a STOP does; resume what is left with `--only` per stage (`--from` reruns every
  later stage).
- `C5_DOCUMENTS` must name an own folder for offline's hutest (MC-B 2026-11-25, before 0737).

Not run, by name: the installer suite (tools/installer/tests, ticket 0728); release-tiers (replaced in tier I by
release-hotfix + release-intermediate, as the tier does); the suites that need a native build or a game (harness-native,
x64dll-tests, x64dll-cvgamedb, proxy-tests, server-tests, server-render, server-remote, render-ctest, render-tests-py,
shell-tests, shell-frame, ipc-ctest, models-ctest, options-ctest, perf-budget-stock: the tier lists them as not run, their
areas' live checks are its game stages); the 43-civ variant (`--major-civs 43`; the installer is std-only).

## 24. Beta 4 hotfix 1: a server-only hotfix on a released cut (ticket 0751, 2026-11-25) [L]

The fourth beta cut dc259f0b1 plus only the server/ part of main 960cad0df (0742's route S re-entry fix: 7 files, all
under `server/`), branch `beta4-hotfix1` @482ce16be, taken as section 22 from one clean checkout
(`C:/c5-64bit-work/cand7/wt`, detached), descriptor `cand7/Release-5.4.8-beta4h1.json` = beta 4's with only `notes`
changed (asserted in `prep.sh`), hotfix tier over `cand1/run` (`--base-unaccepted --carry pins --for-owner --jobs 4
--gate-wait 60`), pool and job owner `0751`, `C5_RELEASE_TICKET/ADDRESS` and `C5_DOCUMENTS` (the take's own folder)
set as 0734's `env.sh`. Scripts: `tools/release/evidence/0751/`.
- **Units and rule A from the take's checkout** (step 0): `render/build.py` 105 s, `build_units_tests.py` 46 s, the
  four walkopen cases 4/4, the whole `test_scene_units.exe` 104/104 (175 s), 03:48-03:54. `run_all.py --only
  render-host --only scenebundle-py --only offline-equals-live` on the pure_vp copy (4390913), 04:27-05:04 in MC-B's
  rule A turn, beside the tier's journeys: builds c5render 45 s, rt 97, sb 337, sceneshot 58; render-host 227/227
  (855 s), scenebundle-py 31/31 (171 s), offline-equals-live 17/17 (664 s); no 0xC0000142.
- **Tier run 1** 03:50:54-04:02:46: build stage ADOPTION of the DLLs by hash (x64 4c5da20e... = beta 4's), x64-server
  216 s, shell 101 s, no LTO link; narrowing and census carried from cand1/run, model and purity carried from
  cand6/tier (inputs equal), pack, offline ok; suites STOP on its budget before running anything (the same 1312 s
  selection as sections 22, beta 3).
- **The suites by hand for a server-only hotfix** `[L]`: the tools tree of 482ce16be equals dc259f0b1's (the diff names
  only `server/`), where 0734 had passed 34 no-game suites at 02:24 the same night; so only the hotfix selection minus
  the installer suite ran: `run_suites.py --suites exebuild-tables,release-hotfix,safety-static,proxy-gen,shell-tables
  --lanes 3 --rerun-failed 1 --tier intermediate --game-files` through quietlock, 04:03-04:22 (1140 s; release-hotfix
  327 tests 1140 s beside the tier's savetest, exebuild-tables 62 in 568 s); export-gate (`check.py --rev 482ce16be
  --since 06d9e820d`) PASS in prep. With `--suites`, do not pass `--range` (it then selects by diff, 0734's trap).
- **Run 2** (`TIER_FROM=savetest`) 04:03:27-05:06:51: savetest fresh 663 s PASS; wholegames CARRIED from cand6/tier
  (its inputs name no server.exe: x64 DLL, other x64 bins, i0 DLL, test db, tool sources equal); hi_journey 52 min on
  c5h-new-b (claimed and released by the stage itself): product release-basic stopped at step 10 on the known
  city-screen flake (human-input-testing.md 8.5) and passed on the stage's one designed rerun (611 s), product
  release-large 450 s, stock release-basic 551 s, stock release-large 400 s. One crash after a passed journey: stock
  release-basic at its final quit, pid 36144, 0xC0000005, no box, 04:57:00, 6 s before the replay's exit: K24, as
  section 22. The three product runs' vpproxy.log say `purity: loaded (pack=1)` (140 method keys, 29 Lua names; the
  installer layout carries purity.tsv, unlike product_path.py, MC-B #13999) and no server log names
  `route-s-watchdog` or DISARMED.
- **Result**: gate line FAIL only for the suites budget STOP (as 22); `--owner-check` exit 0 (played on server
  58b0f449..., x64 DLL 4c5da20e..., proxy 5cea6db8..., -ia DLL 6c8cd5f4...). The proxy and shell were rebuilt from
  source equal to beta 4's and hash differently from beta 4's (proxy 48524398...) `[I: the builds are not
  byte-reproducible; not examined]`. Installer `0.1b4h1-std-only`: 10,157,334 B, SHA-256
  99a406bdca3a6b3a1136cf003ead4c125879357959d72fb0e970c86db8500a31, 05:07:29. Wall clock from the checkout to the
  installer: 03:45-05:07, 82 min (the tier's 76 min, with units, suites and rule A beside it).
