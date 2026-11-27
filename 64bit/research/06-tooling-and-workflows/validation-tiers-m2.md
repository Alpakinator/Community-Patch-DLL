# Validation tiers, milestone M2: the intermediate tier (epic 0630, D220, D221)

The build of the intermediate tier (at most 4 hours). The hotfix tier (M1) and everything before 2026-11-22 are in
[validation-tiers.md](validation-tiers.md) (held by ticket 0671 until it merges; M2 writes here, MC #10384). The design
is the Architect's study `research/07-architecture-decisions/architect-2026-11-20-validation-tiers.md` (sections 5, 6
column I, 8 steps 2, 5, 6, 7); the release owner's guide is `docs/RELEASE-TIERS.md`. Marks: `[C]` read in code or a
test, `[L]` a live run, `[I]` inference. Started 2026-11-22 by lead-0630 (account B).

## 0. Status and split (lead-0630, 2026-11-22 15:55; updated 17:50)

**M2 merged into main at 65d887411 on 2026-11-23 at 11:56 (MC #11598).** Its measured take is in 8.4i. Open after M2:
flakes 0695 (fixed on its branch 2026-11-24, 6.5) and 0706, item 1 of 8.5 (the DLL compiles side by side, never run live) and item 4 (the 43-civ take).

| Child | What | Who | State |
|---|---|---|---|
| 5017 | stages declare what they hold and run side by side (step 2a) | W1 | done 16:35 (hermetic; live in 5024) |
| 5018 | the three DLL compiles side by side within D121 (step 2b) | W2 | done 16:35 (hermetic; live in 5024) |
| 5019 | the scene library's regression against the last release's pictures as a gate (step 5) | W3 | done 16:35 (hermetic; bootstrap and live in 5024) |
| 5020 | phase.py's plan for one sweep, the tier stage `sweep` | W1 | done 17:20 (hermetic; live in 5024) |
| 5021 | the modpack-form smoke | W2 | done by 17:20 (hermetic; live in 5024) |
| 5022 | the intermediate plan and its carry rules | lead (+ W2's helper refactor) | done 17:35 (hermetic; section 7) |
| 5023 | the 43-civ variant, the EXE-4390913 journey, the instance Tiny arm in tier I | W3 | done 17:35 (hermetic; live in 5024) |
| 5024 | one measured intermediate take against 3-3.5 h; step 7's missing times | lead, after the 0.1b2 playtest | done 2026-11-23 10:29: take 7 + one resume, GATE PASS, 3 h 8 min 12 s wall clock (8.4i) |

Rulings of the MC for M2 (#10389, 2026-11-22 15:5x): the 43-civ variant and the 4390913 journey are in M2; the D118
failure case and the ledger's soak arm go to M3. A product route that fails once on a NAMED known flake (the class of
human-input-testing.md 8.5) and passes its one retake may still carry the stock route, flake and retake recorded in the
report; any other product failure runs the stock route fresh.

## 1. Facts found while splitting M2 (lead-0630 and two read-only explorers, 2026-11-22 15:45-16:00) [C]

- **The driver is serial.** `take_release.Driver._run` runs the plan's stages one after another in one process; the
  tier's `TierDriver` only changes where a stage module is looked up. Two tier stages change `os.environ` around their
  calls (`tiers/stages/hi_journey.py` lines 512-521 and 649-656: `C5_RELEASE_HI_*`, `C5_POOL_OWNER`;
  `tiers/stages/wholegames.py` lines 56-63: the games setting), as does the original `stages/hi_journey.py` (lines
  1107-1114). Threads would leak these into the other stages' child processes (subprocess with `env=None` inherits the
  process environment), so stages that run side by side need their own processes (5017).
- **The quiet-machine lock does not forbid parallel jobs of one take.** `quietlock.py run` refuses only while a hold is
  taken (`take`); several registered jobs may run at once (perf/quietlock/quietlock.py docstring). The memory floor (8 GB
  free commit) applies to every gated start.
- **D121 is enforced by `perf/quietlock/linkplace.py`** (one whole-program link place; a link waits in place up to 60
  min). `tools/x64-dll/build.py` takes it for the x64 release link. The 32-bit builds (`build_vp_clang.py --config
  release` in the fork: `-flto`, `lld-link /LTCG`, link 250-330 s recorded in research/03) do not take it; issue 4252's
  `build_x86.py` wrapped the WHOLE 32-bit build in the place, which serialises the compiles too. `build_vp_clang.py`
  has only `--config`; its link is one `subprocess.run(command)` whose command contains `lld-link.exe @<response
  file>` (fork file lines 419-450), so a wrapper can take the place around that call alone (5018).
- **The scene library** (offline-scene-harness.md sections 17 and 26; runbook 2.3, 2.4, 4.2-4.4):
  `server/tools/scenerun/scenerun.py run` already compares each shot with a `baseline.json` (equal, within tolerance,
  differs; exit 7 = regression) and `--baseline DIR` overrides the folder. Baselines today live outside git under
  `F:/c5-64bit-work/scene-library/baselines/<machine>__<adapter>/<bundle_id>/`, keyed by bundle id only: there is no
  baseline per release or per build. 16 bundles; P1 (replay equals recording) takes 637-900 s for the 16; a full
  scenerun of all 16 with pictures has no recorded time. The player (`sceneshot.exe`) is not built by the release
  driver; it links the server's render sources and `c5render.lib` (the build's `render/`, which the hotfix's whole
  adoption does not copy). Bundles are adapter-bound (another adapter exits 7, NoDevice). Note: the study's "the scene
  library's regression ... `offline-scene-harness.md 4`" (section 4.5) points to the wrong section; the regression design
  is section 17.
- **phase.py** (`tools/hi/phase/phase.py plan`, lines 381-460) plans a sweep per save and ALWAYS both routes (line 416);
  `--only sweep --situations era-medieval` gives two items and a subset plan; `absent` can drop only a product sweep.
  No tier plan exists. The mid-game save is `HI-KIT-MEDIEVAL-LARGE-T200` (kit.json 461-484; era-medieval on i2-rc1:
  product 52.2 min, stock 51.7 min). Sweeps run through `tools/hi/sweep.py run` (lines 994-1005).
- **The modpack form**: profile `modpack548` (`tools/instances/provision.py` 156-171); instance `c5h-new-mpk`
  (26577625); the installer supports it (records `form` = modpack); mods-form saves do not load there, so the smoke uses
  the turn-19 autosave as `tools/release/evidence/0626/s20_install_run.sh` does (the only smoke today, an evidence
  script with fixed paths). Both journey stages refuse a non-mods profile (`tiers/stages/hi_journey.py` 687-691;
  `stages/hi_journey.py` 268-270, 1328-1329).

## 2. Stages that run side by side (ticket 5017)

Built 2026-11-22 16:00-16:45 by W1 of lead-0630 (account B), branch `task-5017-parallel-stages`. Hermetic tests only
(`tools/release/tests/test_tier_parallel.py`, 25 tests, about 60 s); no live take has run it yet [C for the code and
the faked runs; nothing [L]].

**How to use it.** `python -I tools/release/tiers/take_tier.py <tag> --tier hotfix --base-take WORK --places
headless=N,game=N,gpu=N[,cpu_build=N] ...`. The places are MC-B's grant; `cpu_build` is 1 unless given; every other
kind is 0 unless given. Recorded in the state's `config.places`; a resume without `--places` reuses them; `--places
off` drops the record and runs serially. Without `--places` (and none recorded) `take_tier.py` builds the same
`TierDriver` as before and nothing of this section runs (test `test_without_places_the_serial_driver_runs_unchanged`
proves the parallel driver is not called, no `parallel/` folder, no `places` key). [C]

**Declarations** (`tools/release/tiers/plans.py`): `HOLDS {stage: {kind: n | 'all'}}` with kinds `headless`, `game`,
`gpu`, `cpu_build`; `AFTER {stage: [stages]}` for order beyond DEPENDS/USES (empty for hotfix). Hotfix values
(2026-11-22, [I] until 5024 measures): build, census, purity `cpu_build=1`; offline `headless=1`; savetest
`headless='all'` (run_d10 runs `--jobs` games at once); hi_journey `game=1, gpu=1`; narrowing, model, pack, suites,
wholegames hold nothing. Undeclared stages (survey, branches, testdata, lock, tierraise, tierreport) are barriers: they
run alone in the driver's own process, after every earlier stage and before every later one. A take whose grant cannot
hold a stage of this run (a hold above the grant, `'all'` of a kind granted 0, an unknown kind) is refused with exit 2
before anything runs. [C]

**Scheduling** (`tools/release/tiers/parallel.py`, `waits_for`, `ParallelMixin._start_ready`): a stage starts when the
stages it waits for ended ok/skipped in this run and its holds fit the free places; plan order breaks ties, and a stage
waiting for places keeps those kinds from later stages (no starvation). It waits for its module's DEPENDS and USES
that are earlier in the plan and run in this take: USES count too, because they are part of the stage's recorded
inputs (`Driver.inputs_of`), so starting before them would change `inputs_sha` and the resume would run it again.
Consequence for the hotfix plan: wholegames (remote testbeds) waits for savetest only because of `USES =
['savetest']` in `stages/wholegames.py` line 50 (the outputs are hashed, never read) [C]; dropping that USES in the
tier module would let the two overlap. Stages not run in this take (`--from`, `--only`) are not waited for; the
stage's own DEPENDS check refuses it as serially (exit 2, same message). [C]

**Processes and the one writer.** A declared stage runs in `python -I -u take_tier.py <the driver's arguments>
--stage-worker <stage>` (`StageWorkerDriver.run_one`): it loads the state once, runs `parallel.run_stage` (a copy of
`take_release.Driver._run`'s loop body: stop file, module, DEPENDS check, `inputs_of`, skip on unchanged
`inputs_sha`, `again`, `resume_outputs`, StopRequested -> pending, GateRefused -> pending + exit 3, exception ->
failed with `logs/<stage>-traceback.log`; keep the two in step when take_release.py changes), saves its own copy of the
state to `<work>/parallel/<stage>.state.json` (so `ctx.partial` parts survive a crash) and writes the finished entry
to `<work>/parallel/<stage>.json`; its pid and creation time go to `<stage>.pid`. Only the parent writes
`state.json`; it merges `status, outputs, stop, error, inputs, inputs_sha, started, ended` and adds `held` (the places
the stage held, `'alone'` for a barrier). The parent's saves retry on PermissionError (Windows refuses `os.replace`
onto a file another process holds open while a child reads state.json at its start) [I: reasoned from Windows file
sharing, not reproduced]. Child output: `<work>/logs/<stage>-worker.log`, echoed by the parent as `[<stage>] ...`. [C]

**Ends.** A stop, failure, refusal or error of one stage: nothing new starts, running children end on their own (never
killed), and the take ends with the exit code of the first stage *in plan order* that did not pass, repeating that
child's outcome line (`take_release: STOP/FAILED/REFUSED/error ...`) unprefixed as the last line before the D162 line.
The STOP file: children check it (at start and in every `ctx.run`); the parent starts nothing new and prints the serial
stop line for the next stage it would have started. A child that exits without a result fails its stage ("its stage
worker exited N without a result"), its recorded parts kept for the resume. A resume after a killed driver merges what
the children left (a finished entry: "ended (ok) while no driver ran"; an unfinished one: its parts) and refuses (exit
2) while a child of the dead driver still runs. The D162 summary, `reached` and the end-of-run checks are the serial
driver's (copied from `Driver._run`'s end). [C]

**tierreport** (`side_by_side` in the JSON, section "Stages side by side" in the .md, only when `config.places`):
each stage's start, end and places held, every pair that overlapped (seconds, from the state's second-resolution
clock), the wall clock (first start to last end, measured from the state) beside the stage minutes added up. [C]

**Tests and negative controls** (2026-11-22): overlap of two independent stages in two processes; holds that do not
fit wait (and fit with `cpu_build=3`: they overlap); a DEPENDS and a USES wait (and without the dependency they
overlap); an env var set in one stage is not seen by the other (in one process, without `--places`, it leaks: the
control); failure, stop file, gate refusal, crashed child, first bad stage in plan order; resume after `kill` of the
driver mid-parallel (one child killed with its part recorded, one finishing while no driver ran; the guard refuses
while it still runs); a chain plan gives the serial order and the same statuses, outputs and inputs_sha; `--from`,
`--only`, places recorded/reused/`off`; refused grants and syntax; a cycle in AFTER ends with exit 2, not a hang.
Mutations of ten checks in parallel.py (barrier, child process, fit, deps, nothing-new, adoption, partial merge,
first-bad order, alive guard, grant check) each made a test fail; restored. [C]

**Not verified** [I]: a live take with `--places` (real stages, quietlock jobs side by side, the memory floor of 8 GB
free commit for several gated starts at once); the hold values; the PermissionError retry under real contention;
children's echo volume on a long take.

## 3. The three DLL compiles side by side (ticket 5018)

W2 (worker-5018, account B), 2026-11-22. Code: `tools/release/tiers/linkwrap.py` (new) and
`tools/release/tiers/stages/build.py` (functions `side_by_side_on`, `wrapped_vp_clang`, `side_by_side`,
`run_fresh_x64`, `why_x86_not_adoptable`, `x64_pin_equal`). Tests: `tools/release/tests/test_tier_linkwrap.py`,
`tools/release/tests/test_tier_build.py` classes `SideBySide`, `IntermediateAdoption`. Nothing live yet: the measured
run is ticket 5024's.

**3.1 Facts about the fork's 32-bit build script** (`build_vp_clang.py`, the clone of the fork next to this
repository, read 2026-11-22) [C]:
- `PROJECT_DIR = Path().resolve()`: the script builds the current folder; `--config` is its only option.
- `update_commit_id.bat`, `clang.cpp` and the precompiled header are each one `subprocess.run(<string>,
  capture_output=True)`; the **175 .cpp compiles are `subprocess.Popen` all at once** (`TaskMan.spawn`, no job
  limit, so one 32-bit build already starts 175 `clang-cl -flto` processes); the link (`link_dll`) is ONE
  `subprocess.run('"<VS90COMNTOOLS>\vsvars32.bat">NUL && lld-link.exe @"<build dir>\link"', capture_output=True)`;
  a failure of any step is `sys.exit(1)`.
- The string command is given to CreateProcess without `shell=True`; Windows runs the `.bat` through the command
  interpreter, so the process that `place.add_pid` records for the link is that interpreter, which lives exactly as
  long as `lld-link` (it runs `... && lld-link`). [C] from the code, [I] for the lifetime.

**3.2 linkwrap.py** [C, hermetic tests]: `python -I linkwrap.py [--what T] [--out DIR] [--record FILE]
[--link-wait-min M] [--allow-no-link] [--] <build_vp_clang.py> [its args]` runs the script unchanged
(`runpy.run_path`, run_name `__main__`, `sys.argv` = script + args, the caller's folder, the script's folder first on
`sys.path`) with `subprocess.run` replaced for the run: a command naming `lld-link` (regex `LINK_RE`, a word
`lld-link[.ext]`) takes `linkplace.acquire(what, out=...)`, runs the same command with `Popen`, `add_pid`, releases,
returns a `CompletedProcess` (capture_output, input, timeout, check honoured); every other call goes to the real
`subprocess.run`. Exit codes: the script's own; 75 = link-place timeout (one line `linkwrap: LINK PLACE TIMEOUT: ...`
with the holders, "nothing was linked", `linkplace.py status`, "run the build again"); 76 = the script exited 0 without
any lld-link call (the fork's link changed shape; D121 could not be kept). The timeout is raised inside the script as a
`BaseException` so a script's `except Exception` cannot link without the place. `linkplace` is imported only at the
first link. `--record` writes per link `waited_s`, `link_s`, `rc`, `place_taken`, `memory_waited`.

**3.3 The tier build stage** [C, hermetic tests]:
- **Who builds side by side**: every tier but hotfix by default; config `build_side_by_side` True/False overrides.
  The hotfix tier keeps M1's one-after-another full build when it cannot adopt (its tests unchanged).
- **The order of work**: the sources are prepared one after another as the original prepares them (a stop there,
  e.g. the instruments patch not applying, now comes before any build; serially it came after the builds before it);
  then x64 (`orig.build_x64`), the compositor (`orig.build_composite`, i2/i3), i0 and -ia (`orig.build_i0`, the tool
  command replaced by the linkwrap command through a ctx wrapper) start at once, one thread each, every one through
  `ctx.run` (own log, quietlock or gated, explicit environment). After all end, the records are written with
  `ctx.partial` from the stage's thread in the original's order and the original `run()` is called with them as
  finished builds: it builds nothing and lays out. A failure fails the stage after the others ended, naming the first
  in the original's order and appending the others; the finished builds stay recorded, so a rerun builds only what
  failed (tested). A build tool that is not `[python, ..., <script>.py]` cannot be wrapped: the stage says so and runs
  the original serially (`parallel_builds.used` False).
- **Same result as serial**: a test runs the serial and the side-by-side path into the same work folder: outputs
  equal key by key (the measured `seconds` and `parallel_builds` aside), layout files byte-equal (README aside), the
  candidate locks equal (their build time aside), the stage inputs equal.
- **Recorded**: `outputs.parallel_builds` = {used, how, wall_s, resumed, x86, builds: {x64|composite|i0|i0ia: start,
  end, wall_s, seconds, status, link_wait_s, link_s, link_place_taken, ...}}. x64's link wait and time come from
  `tools/x64-dll/build.py`'s `build-summary.json` (`link_place.waited_s`, `timings_s.link`); not verified that
  build_server.py leaves that file under `<out>/deps/dll/` in a real build [I] (the fake does).
- **The compositor joins the set**: 91 s on 2026-11-17 (release-driver.md, i2-rc1 take; x64 1,216 s, i0 412 s on a
  busy machine), no whole-program link (shell/build.cmd, render/build.py take no place), its own output folder.
- **Memory**: two 32-bit builds at once start 350 `clang-cl -flto` processes beside the x64 compile (`--jobs 6`). The
  quiet lock's 8 GB floor is checked at each start only, and linkplace's at each link. Config
  `build_x86_one_after_another` runs the two 32-bit builds in one thread, one after the other, beside x64, if the
  measured run shows the commit running short. Not measured [I].
- **A tier other than hotfix with a base take** (MC #10412, 2026-11-22): the 32-bit DLLs adopted by SHA-256 from the
  base (the hotfix's `adopt_i0`) when the i0 commit, the major count and the build inputs are unchanged, else built
  side by side; x64 adopted by its build key or built (corrected 2026-11-22, validation-tiers.md 20), server, proxy and compositor
  always built; `outputs.adoption.ways` names
  each DLL's way. The rule itself is in section 7 (lead).
- **CORRECTED 2026-11-22 18:40 (MC #10515; was: "The x64 hash stop narrowed", `x64_tools_changed`, the LOUD stop on an equal pin whose fresh DLL hashed otherwise unless tools or compiler changed).** The x64 DLL holds `__DATE__`/`__TIME__` fixed to this repository's HEAD commit time, so a fresh build from another commit always differs: the rule is now the x64 BUILD KEY (pin + `tools/x64-dll/` tree at the commit that built the base DLL + toolchain): same key, the x64 DLL is adopted by hash; otherwise built and counted as changed; LOUD only for same key and different bytes. The rule, the measurement and the tests: [validation-tiers.md](validation-tiers.md) section 20.
- **Decision 3** (lead-0630, 2026-11-22): a stop while preparing the sources (e.g. the instruments patch not
  applying) comes before any build in the side-by-side path; serially the builds before it ran first. Accepted.
- **Decision 1** (lead-0630, 2026-11-22): the hotfix tier's no-adoption fallback stays one build after another (M1).

**3.4 Not verified, for the measured run (5024)**: the real overlap and the minutes saved; the real link-place waits
(three whole-program links on one place: x64 8-11 min, each 32-bit 250-330 s); peak commit with three compiles; that
the place's holder shows the linkwrap process and the linker's interpreter (`python perf/quietlock/linkplace.py
status` during a link).

## 4. The scene library's regression as a gate (ticket 5019)

Written 2026-11-22 16:20 EST by W3 of lead-0630 (account B, branch `task-5019-scene-gate`). Code
`tools/release/tiers/stages/scenes.py` (stage `scenes`, DEPENDS `build`), tests `tools/release/tests/test_tier_scenes.py`
(24 tests, hermetic: faked sceneshot build, scenelib check, scenerun and quietlock, a small git repository, the adapter
set by replacing `scenes.machine_adapters`). Nothing under `server/` is edited; `plans.py` is unchanged (the stage
enters a plan with ticket 5022). Nothing below ran live (the 0.1b2 playtest rule: no build, GPU or lock slot).

**4.1 What the stage does `[C]` (code and tests of this branch).**
- **The reference** is a *baseline set*: `<results>/scene-baselines/<set key>/` (the results store's root, `store.root_of`,
  outside git), in scenerun's own layout (`<bundle>/baseline.json`, `<bundle>/<shot>.png`; scenerun reads it with
  `--baseline DIR`) plus `manifest.json` (format `c5-scene-baseline-set`: take, made, made_by, server/compositor SHA-256,
  commit, player SHA-256, adapter, library and asset-root hashes, bundles, shots, accepted). Set key
  `<tag>__<sha16>` over tag, server.exe SHA-256, compositor SHA-256, the repository commit the player was built from and
  the adapter ("description driver version") (`set_key()`).
- **The base set** is looked up as (1) the folder of the base take's own key on this adapter, then (2) the set the base
  take's scenes outputs name (`baseline_dir`; a carried base names the set it was carried with). A manifest of another
  adapter or driver is not used. None: STOP whose unblock is one command, `scenes.py bootstrap` (4.4).
- **Carried** (tierlib's convention) when the base take's scenes stage is ok and its recorded `parts` equal this take's
  six parts: `server_exe` (build `x64_hashes` bin/server.exe), `compositor` (`composite_sha256`; "none (shell_config
  thin)" for a build without one, unreadable when shell_config is i2/i3 and the hash is absent), `player_sources` (the
  content hash of `PLAYER_SOURCES`, store.LiveSource over the repository: server/src, the sceneshot folder,
  build_scenebundle_tests.py, shell/frame and shell/common, render/ sources and CMake files, exebuild/include, options/,
  models/ sources, tools/x64-dll/build.py), `library` (the C: copy's index.json: bundle id -> scene.json SHA-256),
  `asset_root` (`asset-root.txt` SHA-256) and `adapter`; and the base's set is still there with its manifest. The carried
  result names the base's set (`baseline_dir`), has no minutes of its own, and its line lists the six hashes.
- **Fresh**, in this order: the accept file is valid; the bundles' adapter (each scene.json `adapter.description`) is an
  adapter of this machine (`perf/ledger/record.py machine_facts` `gpus`), else STOP; every part readable, else STOP;
  the base set found, else STOP with the bootstrap command; the repository clean at the build's `tools_commit`, else
  STOP; `build_sceneshot.py --out <work>/scenes/sceneshot --smoke [--c5render <x64_out>/render]` through `quietlock.py
  run` (`--c5render` only when the build's `render/` holds both `c5render.lib` and `src/units/c5r_models.lib`; after a
  whole adoption it does not, and build_sceneshot builds c5render itself); `scenelib.py check --copy <library>` (light;
  a failure is a STOP naming `scenelib.py sync`); `scenerun.py --library <copy> run --player <exe> --out
  <work>/scenes/run --issue <who> --baseline <base set> --no-baseline-write --no-record --asset-root <root>` through
  `quietlock.py run`.
- **The verdict** from `<run>/report.json` (`judge()`): `differs` not in the accept file -> STOP listing each shot
  (bundle, shot, verdict, pixels; the first 12 in the line, all in `outputs.regressions`); a bundle the base set has no
  picture of (`uncovered`), a base shot the run did not make (`missing`), a shot with another status (`not judged`), a
  shot without a picture, or a bundle drawn on another adapter (`adapter`/`same_adapter` of the report) -> STOP;
  `new` shots of a covered bundle pass and are listed (`new_shots`). Exit 6 (P1Differs, rule A broken) -> STOP; exit 5
  whose output names NoDevice -> the adapter STOP; exit 7 without a `differs` shot -> failed; any other non-zero exit
  -> failed.
- **On a pass** this take's pictures become a set under its own key, written after the verdict to `<key>.partial` and
  renamed (an existing folder of the key is kept as `<key>.old-<time>[-n]`, a free name); nothing is written on a stop.
- **The accept file** `<work>/scenes/accept.json` = `{"accept": [{"bundle", "shot", "reason", "who", "date":
  "YYYY-MM-DD"}]}`; its SHA-256 is in the stage's inputs (an edit runs it again). Accepted shots pass, are listed in
  `outputs.accepted` and the set's manifest, and through `outputs.tier_subset` in the tier report's "Reduced checks";
  their pictures become the new reference. Entries that matched no differing shot are listed (`accept_unused`).
- **Judged again without the GPU**: `<work>/scenes/run-inputs.json` records the parts, the base set and the player's
  SHA-256 of a finished scenerun (exit 0 or 7); a later run with the same three (only the accept file changed) judges
  that report again (`rejudged`), with no build and no draw.
- **Settings**: library copy `C5_SCENELIB_COPY` (default `C:/c5-64bit-work/scene-library`, scenelib's), asset root
  `C5_SCENE_ASSET_ROOT` (default `C:/c5-64bit-work/scene-asset-root/mods548-vt`); both recorded in the inputs.
- **Holds** (for 5017's map): the GPU, `'gpu': 1`, fresh only; a carried result holds nothing.

**4.2 Where the settled design was changed, and why `[C]`/`[I]`.**
- *The player's SHA-256 is not a carry part.* The release driver does not build the player; the stage builds it from
  source every fresh take, and an lld-link/MSVC binary is not byte-repeatable (link time stamp, PDB GUID) `[I: not
  measured for sceneshot]`, so its hash would differ on every take and never carry. The carry uses the content of the
  player's sources instead; the binary's SHA-256 is recorded in the set's manifest and the outputs. The set key uses the
  repository commit for the same reason (the bootstrap must name the base's folder before it builds anything).
- *`--no-record`.* With recording, scenerun records every picture in Visual Checks and then exits 8 while any picture of
  the issue is undescribed (scenerun.py `cmd_run`, SEVERITY order): a gate run would end Undescribed until a person
  described about 128 pictures. The gate keeps its pictures in the run folder and the set instead.
- *Exit 6 is a STOP, not a failure*: P1 (offline equals live) runs with `--adapter bundle`; a difference means the
  library was not recorded again after a renderer change (rule A) and needs a person, not a rerun.
- *`new` is not a pass by itself.* scenerun calls a shot without an earlier picture `new` and does not fail it. A baseline
  is keyed by bundle id, and a bundle id carries its recording time (offline-scene-harness.md 26), so after every
  re-record (0670, 0672: 2026-11-22) all its shots would be `new` and the gate would compare nothing. So a bundle the
  base set does not cover is a STOP whose unblock is the bootstrap with `--force` (the base take's player draws the
  library as it is now).
- *The adapter includes the driver.* A base set made on another driver version is not used (the bootstrap makes it again
  on this driver), so a driver change is never reported as a renderer regression. `machine_facts` reads the display
  class's `DriverDesc`; the player's `adapter` is DXGI's description; they are taken to be the same string for the NVIDIA
  card `[I]`.
- *The carry compares with the base take's state*, not the results store (`store.py` is not in this ticket's touches;
  the store's KEYS table has no `scenes` entry).
- *Accepted shots reach the tier report through `tier_subset`* (its "Reduced checks" section), because `tierreport.py` is
  not in this ticket's touches; a section of its own for accepted shots is a small later change there.

**4.3 Expected time of a fresh run `[I]`** (none measured with this stage): the sceneshot build 4-9 min (c5render cold
3-4 min, runbook 1.6; the server build 167 s on a busy workstation, runbook 2.10; build_sceneshot's own time is NOT
VERIFIED, runbook 4.2), `scenelib.py check` about 1 s (runbook 4.3: 0.85 s), scenerun of the 16 bundles with pictures
13-20 min (P1 of the 16 took 637-900 s, runbook 2.4; 0514's pass of 7 bundles with pictures took 5.7-6.3 min on a busy
machine, 30-35 s per Tiny bundle and 53 s for the huge one, offline-scene-harness.md 26; plus the named cameras of the
kit bundles). About 20-30 min in all, GPU held; writing the set copies 16 x 8 PNGs (seconds). The bootstrap costs the
same plus a worktree of the base commit.

**4.4 The bootstrap `[C]`.** `python -I tools/release/tiers/stages/scenes.py bootstrap --base-take WORK [--results DIR]
[--work DIR] [--who T] [--force]`: computes the base's set key on this adapter (refuses, exit 2, a base build without a
clean `tools_commit`, a library whose adapter is not here, a missing asset root); if a complete set is there, exit 0
without running; else `git worktree add --detach <work>/src <base tools_commit>`, the base's own
`build_sceneshot.py --smoke` in it (cwd the checkout, through `quietlock.py run`, `--c5render` from the base build when
present), `scenelib.py check`, this repository's `scenerun.py ... --baseline <key>.partial --set-baseline --p1 off
--no-record --asset-root <root>` through `quietlock.py run`, checks every library bundle has a baseline.json, writes the
manifest, renames to the key, and removes the worktree (also on failure). Default work folder
`C:/c5-64bit-work/3500/scene-bootstrap/<key>`. Tested with fakes and a real git worktree (`test_bootstrap_*`).

**4.5 Tests and negative controls `[C]`, 2026-11-22.** `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q
tools/release/tests/test_tier_scenes.py`: 24 passed in 40 s. Cases: equal -> ok and the take's set written;
`--c5render` used only with both libraries; regression -> stop naming B1 1002 and B2 1001 with pixels, no set written;
accept file -> ok, listed in outputs, manifest and the tier report, judged again without a build; an accept entry for
another shot accepts nothing; an edited accept file changes the inputs; an invalid accept file stops; no base set ->
stop with the bootstrap command, nothing built; bootstrap -> the set, then the gate passes; bootstrap refuses a dirty
base; carried when all six parts equal (no scenerun, no build, no minutes), fresh when server.exe, the compositor, the
player's sources, the asset root, the library or the driver changed; a carry needs the base's set; an unreadable part is
never carried; another adapter (machine, report, NoDevice) stops; exit 6 stops, exit 4 fails; exit 7 without a
differing shot fails; build failure fails; scenelib failure stops; an uncovered bundle stops with `--force`; a missing
base shot stops, a new shot passes listed; the repository not at the build's commit or dirty stops; a base set on
another driver is not used; a set made again three times in one second keeps both earlier ones. Negative controls (`C:/c5-64bit-work/0630/5019-scratch/negctl.py`, each check broken in a
copy of the stage, its test run, the file put back by its bytes): the adapter pre-check, accept matching by shot, the
carry's part comparison, the uncovered check, the report's adapter check, the commit check, the missing-shot check, no
set on a stop, a carry without the base set, the free `.old` name: each made its test fail (10 of 10). Found by the
whole tier suite (16:10-16:20): the first version renamed an earlier set to `.old-<second>` only, and two passes of one
key in one second (a judged-again run is fast) failed on the rename; fixed by `keep_old()`.
**The suite map:** `tools/suites/areas.json` `release-hotfix` `targets` must list `tools/release/tests/test_tier_scenes.py`
(test_tier_suites.py `RealMap::test_release_folder_is_intermediate_with_a_hotfix_substitute` asserts every
`test_tier_*.py` on disk is listed); areas.json is not in this ticket's touches, so that one line is the lead's.

**4.6 Not verified.** No live run: the real build_sceneshot/scenelib/scenerun command lines are taken from their
docstrings and the runbook (4.2-4.4), not run by this stage; the 20-30 min is `[I]`; `machine_facts`' adapter name
equal to DXGI's description `[I]`; the player not byte-repeatable `[I]`; the generated EXE tables (setting
`exe_tables`) are outside the repository and not in `player_sources` `[I]`; whether the bundles of the next release
still replay with P1 on the take's renderer (rule A) is the library owner's, not this stage's.

## 5. One sweep (ticket 5020)

Built 2026-11-22 16:27-17:00 by W1 of lead-0630 (account B), branch `task-5020-one-sweep`. Hermetic tests only; no
live sweep through the stage yet [C for the code and the faked runs; nothing [L]].

**The plan** (`tools/hi/phase/phase.py plan --candidate ID --tier intermediate --out DIR [--kit F] [--kept DIR]
[--replace]`, `make_tier_plan`, `TIER_PLANS`): exactly one item, `sweep:era-medieval:product`, on the kit's
`HI-KIT-MEDIEVAL-LARGE-T200` (its `situations` list names every kit situation on that save: border-contact,
era-medieval, events, mid-game, world-congress with the committed kit, 2026-11-22). The stock sweep is in
`not_planned` (rule 2). Active kept traces are added as `kept:` items (none today: `kept/index.json` is empty).
`plan.tier = "intermediate"`, `subset = true` (so a tier verdict can never serve as `--previous` for a full phase).
Refused with exit 2: `--tier` with `--charters/--situations/--only`, an unknown tier, a kit whose `era-medieval` is
missing, has no SHA-256 or names another save. [C]

**The verdict of a tier plan** (`verdict`): clauses 1, 2 and 5 as for any plan. Clause 3 (kept traces) is `off`
while the plan has no kept or journeys item and the kept set has no active trace; clause 4 (coverage) is `off` for
every tier plan (one sweep can never account for the whole catalogue: the coverage report is the full phase's). The
verdict carries `tier` and `clauses_off`, the page says "off". A full plan's plan.json and verdict.json keep their
shape (no `tier`, no `off` keys; `tools/harness/tests/test_hi_phase.py` 144 passed unchanged). Without a comparison,
any finding of the sweep that has not been through the reproduction filter fails clause 2.

**Stock too** (lead-0630's ruling, 2026-11-22 17:0x): `phase.py verdict DIR --compare <compare.json>` (a tier plan
only; a full plan is refused with exit 2, it classes with `campaign.py stock`; an unreadable file or one without
`rows` is exit 2). Each compare row's `findings` (the product finding signatures at that control and mark, written by
`sweep.py compare`) give the finding its class; a signature on two rows with two classes keeps the stricter one. An
open finding classed `stock too` leaves clause 2 and is listed in its notes ("stock too ... not a product fault");
`patch only`, a row without a class (`unclassified`) or a finding no row names still fails. So a comparison whose
findings are ALL stock too passes clause 2. The verdict records `stock_compare {file, sha256, stock_too, classes}`.
Only `process_ended` and `box` outcomes are compared by `sweep.py compare`, so findings of other kinds (wrong_state,
input_ignored, hang, wrong_picture) still fail the tier. [C]

**The stage** (`tools/release/tiers/stages/sweep.py`, DEPENDS build, pack; plans.HOLDS `{'game': 1}`): plan,
check the kit save on disk by SHA-256 (else STOP, no pass), build the installer's test build into
`<work>/sweep/installer`, claim (owner `<who>-sweep`; C5_RELEASE_SWEEP_INSTANCE, else C5_RELEASE_HI_INSTANCE, else
the pool's 26577625), refuse (STOP) an instance of another EXE build, with an installer record or proxy layout, or a
non-mods profile, lay the player's DLL, list, install, list, check the installed layout against the build's hashes
(the original hi_journey's `installed_layout`), place the save in `ModdedSaves/single` (an existing file kept and put
back by hash), `sweep.py ... run --route product --launch` (gated, timeout C5_RELEASE_SWEEP_TIMEOUT_MIN default 180),
stop the game (a box: claim kept, nothing removed), `phase.py record`, `phase.py verdict`, then the tier
hi_journey's `_remove` (removal, tree compare, the DLL before back by SHA-256, verify --quick, release). Only when the
verdict is FAIL, the product sweep wrote its sweep.json and the removal passed: the stock sweep on the same instance
with the build's `i0ia_dll` laid by set-dll (the default path, no vpproxy.ini: PHASE.md 3.1's stock route), the DLL
before back, verify, release, `sweep.py compare`, then `phase.py verdict --compare`: PASS there makes the stage ok
with `stock_too` in its outputs; else failed with the counts by class. The take's STOP file is copied to the sweep's
`<out>/STOP` by a thread of the stage every C5_RELEASE_SWEEP_STOP_POLL_S (default 15 s) while sweep.py runs, so the
sweeper stops before its next action; the record's `ctx.run` then sees the stop file, nothing is recorded, the removal
runs and the take ends with STOP (exit 10). Reused from
`tiers/stages/hi_journey.py` by import: `claim`, `release`, `tree_diff`, `_dll_before`, `_put_dll_back`, `_remove`,
`_Env`, `_timed`, `harness_hash`, `exe_build_value`; from the original: `installed_layout`, `box`, `running`,
`stop_game`, `last_json`, `body`, `custom_tools`, `settings`. NOT reused: hi_journey's `build_installer`, whose output
folder is fixed to `<work>/hi_journey/installer` (two stages that may run side by side must not share it); the
same command is run into the stage's own folder. Settings: C5_RELEASE_SWEEP_INSTANCE, _BY, _EXE_BUILD,
_TIMEOUT_MIN, _STOP_POLL_S, _KIT (another kit.json), _KEPT (another kept folder). [C]

**The import wrapper** (PHASE.md step 3, ticket 0473; read 2026-11-22) [C]: the wrapper (1113) is the harness's
`steam_api.dll` in the instance's `install\` folder, recorded in `install\c5_wrapper.json`; the patch's installer writes
only under My Games (MODS, VP64, the record), so an installer-installed layout never includes it. `tools/harness/
launch.py default_route` launches an instance that has `c5_wrapper.json` by the "import" route, which refuses an
outdated wrapper (0473's MC review: exit 4 until `native/build.bat` and `wrapper.py install`); an instance without it
launches by the "early" route (focus_guard injected at the loader break, children guarded too) and needs no wrapper.
So the stage, before the product sweep, runs `wrapper.py install <install>` when `c5_wrapper.json` is there (replaces
an outdated wrapper, leaves the real DLL; compiles the wrapper once with clang-cl when the built one is older than its
sources: a small native build inside the take) and does nothing otherwise; the outputs say which (`wrapper`). A
failed update fails the stage before the sweep (the removal runs). The updated wrapper stays on the instance, as in
PHASE.md (it is the current harness, not the take's layout).

**Tests** (2026-11-22, second round 16:58-17:10): `tools/hi/phase/tests/test_phase_tier.py` 22 passed in 1.6 s;
`tools/release/tests/test_tier_sweep.py` 23 passed in 85 s (fake pool, provision, installer, treehash, cli,
dialogwatch, wrapper and sweep.py; the REAL phase.py plan/record/verdict over a fake kit). `tools/hi/phase/tests/` is
added to the `hi` suite by the lead at the merge. Negative controls: 20 mutations (save hash, one-item plan, EXE build,
profile, installed layout, box without a running game, stock only after a written product sweep, the save put back,
the tools guard, the tier's filter refusal, the tier's save check, clauses 3 and 4 off, stock too only, the stricter
class wins, --compare on a tier plan only, the STOP copier, the wrapper update when the record is there, a failed
wrapper update, the verdict with the comparison) each made a test fail; restored. Two checks were found redundant by
a mutation and are explained, not counted: the box check is also in hi_journey's `stop_game` (the stage's own check
matters when a box stays after the game process ended: test added), and a `check_stop` after the sweep duplicated
the one in the record's `ctx.run` (removed). [C]

**Not verified** [I]: a live sweep through the stage (the sweeper on an installer-installed i2 layout, its Lua channel
and the judge, the 52-min figure on this route); `wrapper.py install` on a live instance from inside the take; the
stock sweep on the stage-laid `i0ia_dll` default path; a STOP during a live sweep; the stock sweep itself does not
take a STOP into account beyond its copier (a stop there ends the comparison, recorded as an error, and the stage
fails).

## 6. The modpack-form smoke (ticket 5021)

W2 (worker-5018, account B), 2026-11-22. Code: `tools/release/tiers/stages/modpack.py` (new; `DEPENDS` build,
pack; holds `{'game': 1}` from `plans.HOLDS`). Tests: `tools/release/tests/test_tier_modpack.py` (in `tools/suites/areas.json`
release-hotfix). Nothing live yet: the live run is listed in 6.4.

**6.1 What the stage does** [C, hermetic tests]: the steps of `tools/release/evidence/0626/s20_install_run.sh`
(install, play, remove) as a tier stage, on the harness's own commands:
1. the installer's TEST build into `<work>/modpack/installer` (the hi_journey stage builds into
   `<work>/hi_journey/installer`; both stages may run side by side, so each has its own folder);
2. `pool.py claim --profile modpack548` (plus `--exe-build 26577625` for the pool's choice, or `--name
   <C5_RELEASE_MODPACK_INSTANCE>`) as owner
   `<hi_journey owner>-modpack` (one owner holds one claim file), through the tier hi_journey's `claim` with
   `profile=` (ticket 5022). Corrected 2026-11-22 (5022; was: "with `--name`, pool.py refuses another profile
   unless `--profile` is given"): `pool.py claim` checks the profile and the EXE build only for its own choice
   or when `--profile`/`--exe-build` is given (`tools/harness/pool.py` claim(), `want = profile or (None if
   name else DEFAULT_PROFILE)`); a bare `--name` claims any profile and build, and the stage checks both in
   the manifest [C];
3. refusals (skipped, released): another EXE build or profile in the manifest, an install record, a provision.py
   proxy layout;
4. `Assets/DLC` listed BEFORE `provision.py set-dll --dll <player DLL>` (in the modpack form the instance's DLL lives
   under `Assets/DLC/VP_MODPACK`; listing it after set-dll would make the DLL put back at the end a difference), then
   the My Games listing, `run_installer.py install`, both listings again; the install record must say
   `form = modpack` (the installer found VP under Assets/DLC);
5. play: the save `C5_RELEASE_MODPACK_SAVE` (default `Saves\single\auto\AutoSave_0019 BC-3240.Civ5Save` under My
   Games, 0626's turn-19 autosave) copied as `Saves\single\C5H-<who>-MPK-T0019.Civ5Save`;
   `frontend.py -i I prepare --mods "" --load <copy> --restore-first` (no mods: the modpack form);
   `cli.py -i I launch` (gated; `C5_FRAME_CAP_AFTER=shell_composite.dll` in its environment when the build has the
   compositor, as 0626 did, 0620 C.7.3); `frontend.py ingame` polled (exit 1 with `[load_screen_ready]` -> one
   `frontend.py dismiss`; 8 -> a box), at most 600 s; `lua.py --state InGame --json "return Game.GetGameTurn()"`;
   `turns.py end`; `turns.py wait <turn> --timeout 900`; the turn again (must be higher); `dialogwatch.py marker`;
   `turns.py ready --settle 10`; `lua.py "UI.ExitGame()"`; the EXE gone within 180 s (`cli.py status --json`); no
   crash (the original hi_journey's `ended_crash`, the tier's `crash_evidence`), no box; `cli.py stop
   --human-turn-hook tools.harness.turns:stop_readiness` (the front end back). The copy of the save is deleted.
6. removal (always): the tier hi_journey's `_remove` (installer removal, My Games compared with the run outputs
   apart, the DLL before put back by SHA-256, `verify --quick`, release), then `Assets/DLC` compared with the listing
   from step 4 with NOTHING apart. A difference re-takes the claim (`_remove` had released it) and fails the stage.
- Failures: a box is never pressed: the game is left running, nothing is removed, the claim is kept (D29, D155); a
  game that ended is checked for a crash and the front end restored (`frontend.py restore`); a running game is
  stopped by the harness (a refused stop leaves it running, COMMON 22).
- Settings: `C5_RELEASE_MODPACK_INSTANCE`, `_PROFILE` (modpack548), `_EXE_BUILD` (26577625), `_SAVE`,
  `_LOAD_TIMEOUT_S` (600), `_EXIT_WAIT_S` (180), `_POLL_S` (5); ticket, address and owner from the hi_journey
  settings (`C5_RELEASE_HI_TICKET`, `_ADDRESS`, `_BY`).
- Reused, not copied: from the tier hi_journey `_dll_before`, `_remove`, `tree_diff`, `release`, `crash_evidence`,
  `exe_build_value`, `harness_hash`, `_Env`, `tool`; from the original `box`, `ended_crash`, `running`, `stop_game`,
  `read_record`, `owner`, `body`, `json_of`; since ticket 5022 also `claim(..., profile=, note=)` and
  `build_installer(..., dest=<work>/modpack/installer)` (the sweep stage likewise into <work>/sweep/installer):
  no private copy is left.

**6.2 Not verified** [I]: that `frontend.py ingame` reports `[load_screen_ready]` on the modpack form's load screen
(the stage also ends the wait when the game is gone); that the default save's file name is on c5h-new-mpk today
(0626's script, 2026-11-21; the instance moved to `%USERPROFILE%\c5-instances` on 2026-11-21, capacity-series.md);
the install record's form value on a live install (`form=modpack` per installer-notes); that the DLC tree comes back
identical after removal since ticket 0623 (0620 C.7.3 saw the compositor's log left in `VP_MODPACK\Logs`; 0623 moved
it to My Games' Logs). If it does not, the stage fails naming the rows: that is the check, not a false failure.

**6.3 Times**: plans.py's 7 min estimate ([L] 0626: install 1, play 5, removal 1) stands until the live run.

**6.4 The live run** (by START-REQUEST, one game place, workstation): `take_tier.py ... --only modpack` on a take with
build and pack ok, with `C5_RELEASE_MODPACK_INSTANCE=c5h-new-mpk C5_RELEASE_HI_TICKET=<the ticket> C5_RELEASE_HI_ADDRESS=<address>`;
expected 8-12 min (installer build 1-2, install 8-25 s, launch and load 1-3, one turn under 1, exit and stop 1,
removal and verify 1-2).

**6.5 The hermetic tests' load-limit flake (ticket 0695, impl-0695 on account B, 2026-11-24)** [C, measured]: the
stage's load clock (`modpack.py`, `t0`) starts BEFORE `cli.py launch`, so in `test_tier_modpack.py` the load window
holds four fake-tool process starts (launch through the fake quietlock, which is two Pythons, then `ingame`, `dismiss`,
`ingame`), each a fresh `python -I`. The tests set `C5_RELEASE_MODPACK_LOAD_TIMEOUT_S=3`, so every test that loads the
fake save raced Python start-up against 3 s. Not specific to `test_turn_that_does_not_advance_fails`; that one was hit
in 5024 take 5. The stage's own `play.load_s` for that test (a scratch probe runs the test in a loop and reads
`play.load_s` from the stage entry before tearDown; each copy through `quietlock.py run`, the workstation already
running 7 other heavy jobs):

| code | concurrent copies x runs | passed | load_s median / p90 / max |
|---|---|---|---|
| any | 1 x 3 | 3 | 0.7 / 0.7 / 0.7 |
| before (limit 3 s) | 12 x 10 | 120 | 1.9 / 2.5 / 3.4 |
| before (limit 3 s) | 24 x 8 | 178 of 192 | 3.1 / 3.6 / 4.1; all 14 failures were the ticket's error |
| after (limit 120 s) | 24 x 8 | 192 | 3.5 / 4.3 / 5.4 |

A run with `load_s` above the limit can still pass: the limit is checked only at the top of the loop, so the last
`ingame` may start just before it and end after it. The fix (test only, no stage change): a loading test's limit is
120 s, a hang guard, because the fake reaches its loaded state by the file its `dismiss` writes and the loop ends at the
first `ingame` after it; only `test_load_failure` (FAKE_LOAD=never, the negative control) waits the limit out, and it
sets 3 s for itself and asserts the error names the limit and `[front_end]`. `test_turn_that_does_not_advance_fails`
now also asserts `loaded`, `turn_from` 19 and `turn_to` 19, so it names the step it fails at. The exit wait (2 s) is not a race: a passing fake stops at `UI.ExitGame()`, before the first `running` check,
and `FAKE_EXIT_HANGS` fails at any limit.

Acceptance [L] 2026-11-24 02:08-02:21 EST, workstation beside about 7 other heavy jobs (a scratch script:
R rounds of 6 concurrent `pytest -q tools/release/tests/test_tier_modpack.py` copies, each through `quietlock.py run`):
after the fix 30 of 30 copies passed all 17 tests (127-173 s a copy), so the no-advance test passed 30 of 30 and the
negative control 30 of 30. The same load on the old file (02:21-02:27, 2 rounds) failed 4 of 12 copies: three
`test_turn_that_does_not_advance_fails` with the ticket's error and one `Pass.test_pass` with the same `did not load
... within 3 s`. Lesson for hermetic stage tests: a stage limit whose clock covers fake-tool process starts is a
hang guard; set it far above start-up time and give the one test that waits it out its own short value.

## 7. The intermediate plan (tickets 5022, 5023)

Lead-0630, 2026-11-22 16:00-16:40, team branch `epic-0630-validation-tiers` `[C: code and hermetic tests]`.

- **The plan** (`tools/release/tiers/plans.py` INTERMEDIATE; `take_tier.py <tag> --tier intermediate --base-take
  <work>`): survey, branches, testdata, lock, tierraise, build, narrowing, census, model, purity, pack, offline, suites,
  scenes, savetest, fixchecks, wholegames, instance, hi_journey, oldexe, modpack, sweep, tierreport. Not in it: drift,
  refsave, the fixchecks' oldsave (R1, R2, E3), the D118 failure case and the ledger's soak arm (M3, MC #10389). A stage
  whose module is not built yet (scenes, instance, oldexe, modpack, sweep until their children merge) stops the take
  "not built yet (issue N)", never a silent pass. `plans.INTERMEDIATE_MINUTES` names each stage's minutes, what it
  displaces from today's validated plan and the source (rule 3); summed serially they are about 400 min, so the 4 h
  budget holds only with the stages side by side (5017) `[I]`.
- **Build rule of tier I** (MC #10412, 2026-11-22 16:0x, confirming the lead's question of 16:1x): the 32-bit DLLs (i0,
  i0ia) are ADOPTED by SHA-256 from the base when their commits and build inputs are unchanged and BUILT (side by side,
  5018) when they changed; server, proxy and compositor are always built. The x64 DLL: corrected 2026-11-22 18:40 by
  MC #10515 (was: "ALWAYS built fresh; an unchanged x64 pin whose fresh DLL hashes differently is a LOUD stop"): it is
  ADOPTED by its build key (pin, tools/x64-dll/ tree, toolchain) or built and counted as changed; the x64 link repeats
  byte for byte only from one repository commit (its `__TIME__`), [validation-tiers.md](validation-tiers.md) 20.
  Why: the 32-bit build is not repeatable (`__DATE__`, `__FILE__`, link time) and the store keys savetest and the whole
  games on the 32-bit DLL's bytes, so a rebuilt 32-bit DLL could never carry them (cand1's four whole games took
  7,417 s). Column I's "32-bit test builds: yes" reads "built when changed, never silently reused with a different
  hash". The build stage's code is ticket 5018's.
- **tierraise in tier I** (`tiers/stages/tierraise.py`): the same four comparisons as the hotfix, recorded as
  `changed`; never a stop (`raised` stays empty). A changed VP release or EXE build adds `ask_full` ("the natural
  moment to ask for the full tier", D220) and one NOTE line. The hotfix's raise names the new command in its unblock
  (`take_tier.py <tag> --tier intermediate --base-take <the same base>`).
- **The export gate as a minute-1 preflight** (carry-in of runbook 1.12's trap of the second beta's hotfix): tierraise
  runs `tools/export/check.py --rev <HEAD> --since <the base build's tools_commit>` in every tier (config
  `export_preflight`, set by take_tier.py unless `--no-export-preflight`, recorded); a failure is a STOP with the gate's
  lines before the build; a base without a tools_commit: not run, said (the suites stage stops on that). The suites
  stage still runs the gate.
- **Suites in tier I** (`tiers/stages/suites.py`, `tools/suites/run_suites.py`): `run_suites.py --all --range
  <base>..<head> --lanes 3 --tier intermediate --budget 2700` (45 min, study column I). New in the runner: `--lanes N`
  (suites side by side, longest measured first, results in the selection's order; with --budget the estimate is the
  longest lane) and `--range` (the export gate's range without selecting by diff). The suites that need a native build
  or a game stay "not run" with their `how` (15 of the 51 in `areas.json` on 2026-11-22, among them shell-tests at 61 min): tier I does
  not build them; whether tier F does is M3's. Two suites side by side could share files or ports: not seen in the
  hermetic tests; 5024 measures it `[I]`.
- **fixchecks in tier I** (`tiers/stages/fixchecks.py`): 0096, 0084, 0103 with the original's commands and pass lines,
  not oldsave (R2); carried from the store while `store.KEYS['fixchecks']` (x64 DLL, server.exe, the other x64 bin
  files, the test database, the three scripts, the 0096 Lua, run_server.py, the tier module) is byte-identical; skipped
  in the hotfix tier (E2). A check that the original no longer names fails the stage.
- **The stock route after a named flake** (MC #10389): the original stage already plays a journey once more when it
  stopped on a NAMED known flake of `tools/hi/journeys/journeys.json` (`known_flakes`, `play_with_rerun`; HI 8.5) and
  keeps the rerun's row with `first_run` and `known_flake`. The tier's `carry_decision` looks at that row, so a passed
  rerun carries stock; it now says so: `product_flakes` in the carried dict and "the product route passed on its one
  rerun after a known flake: <journey>: <why>" in the carried line. Any other product failure plays stock fresh.
- **Carry-in: `C5_RELEASE_INSTANCE` in a release shell** (runbook 1.12): `test_tier_hi_journey.py` now clears it and
  `C5_RELEASE_INSTANCE_WAIT` in its setUp (34 passed with `C5_RELEASE_INSTANCE=c5h-new-a` set, 2026-11-22 16:10; the
  live failure needs that instance to run a game, not reproduced here `[I]`).
- **Tests**: `tools/release/tests/test_tier_intermediate.py` (plan, minutes, take_tier's choice, tierraise in both
  tiers, the preflight's pass, stop and off, fixchecks fresh/carried/one server byte, a failed check, the hotfix skip);
  `test_tier_suites.py` Lanes and IntermediateStage; `test_tier_hi_journey.py` the two flake tests. Negative controls:
  with the tier-I branch of tierraise disabled 2 tests fail; one lane against two lanes; the flake rerun that failed.

### 7.1 5023: the 43-civ variant, the EXE-4390913 journey, the Tiny-only instance stage (W3, 2026-11-22 16:40-17:30)

Branch `task-5023-43civ-oldexe` (from the team head e89952866). `[C: code and hermetic tests]`; nothing ran live
(the 0.1b2 playtest rule).

- **oldexe** (`tiers/stages/oldexe.py`): the tier's hi_journey module (loaded once under its own module name) run with
  `C5_RELEASE_HI_EXE_BUILD=4390913`, `C5_RELEASE_HI_JOURNEYS=release-basic`, the claim owner `C5_RELEASE_OLDEXE_BY`
  (else `<who>-release-oldexe`), the instance `C5_RELEASE_OLDEXE_INSTANCE` (else the pool's choice by `--exe-build
  4390913`); `C5_RELEASE_HI_INSTANCE` and `C5_RELEASE_INSTANCE` are cleared for the call (they name hi_journey's
  26577625 instances; runbook 1.12's trap). It uses the same installer test build, clean-instance checks, install,
  product play, removal and DLL put back by hash as hi_journey, all reused, nothing copied. Two small changes to
  `tiers/stages/hi_journey.py` make that possible: (1) `ctx.product_only`: after a passed product run, no stock route is
  played or carried (`stock: {mode: not played}`; the journey verdict is the original's product-only one); (2) the
  installer folder comes from `ctx.dir('hi_journey')`. `oldexe.OldExeCtx` maps every `ctx.dir('hi_journey', ...)` of the
  tier module and the original to `<work>/oldexe/`, so it never writes over hi_journey's report, installer or evidence,
  even side by side. Attribute writes (the removal's `check_stop`) go through to the driver's Ctx. A take with --for-art: a
  journey that did not pass is a STOP; otherwise the verdict is reported as hi_journey's is (D162 is about the
  26577625 journeys only). A 43-civ take skips it ("played once per release, in the standard take": the lead to
  confirm). Holds `{'game': 1}`; about 20 min `[I]` (plans.INTERMEDIATE_MINUTES) plus the installer build and the
  install/removal, a few minutes each.
- **instance** (`tiers/stages/instance.py`): Tiny arm only, no refsave. The original cannot be called for it: its run
  reads `ctx.outputs('refsave')` first and plays `for key in ('tiny', 'huge')` in one loop, so neither a ctx wrapper nor
  its run() can drop the huge arm. The tier module re-uses the original's helpers as they are (claim, owner, manifest,
  mods_check, fix_modinfo, make_test_pack, new_dir, count_log, arm_verdict, _restore, _cmd, _rm, TINY_SAVE, TURNS)
  and has its own copy of the orchestration (about 70 lines) with one arm. DEPENDS `['build']`, USES pack, purity.
  Carried from the results store: `store.KEYS['instance']` = x64_dll, server_exe, x64_bin_other, proxy_dll,
  i0ia_dll, purity_list, major_civs, tool_sources (arm.py, stages/validated.py, stages/_modinfo.py, the tier module),
  stage_module (the original instance.py). New store parts: `proxy_dll` (x64_hashes proxy/bin), `i0ia_dll`
  (i0ia_dll_sha256), `purity_list` (the SHA-256 of purity's `purity_tsv`, checked against `purity_tsv_sha256` when a
  carried purity recorded one). Scope: the arms that passed (`games` key); tier I needs `tiny`, so a validated take's
  import (tiny and huge) covers it. server.exe and the other x64 bin files are in the key beyond the lead's four
  (the route runs the server: fail loud). Whether the fresh proxy DLL repeats byte for byte (and so whether instance
  ever carries in tier I) is NOT VERIFIED `[I]`; 5024 will show it. Holds `{'game': 1}`; about 7 min `[L]` arm-tiny
  265 s on cand1, plus lay-out and restore.
- **The 43-civ variant**: `plans.stages(tier, major)`. The intermediate tier with more than 22 majors runs
  `INTERMEDIATE_M43` = survey, branches, testdata, lock, tierraise, build, narrowing, model, purity (pack needs them),
  pack, wholegames, hi_journey, tierreport. This is the study's column-I row "build, pack, both m43 journeys, the
  30-major game if the DLL changed". Minutes `INTERMEDIATE_M43_MINUTES` (build 38, wholegames 127, hi_journey 42 `[I]`
  study 6). The hotfix plan is the same for every count. `take_tier.py` now builds the plan from the take's count (after
  `build_config`, so a resumed work folder's count counts) and refuses a `--base-take` of another count (exit 2;
  `base_major()` reads the base state's `config.major_civs`, 22 when absent). `tiers/stages/wholegames.py`: in tier I
  with N > 22 only the original's `MAJOR_GAMES` below N (l30, the 30-major Large game; `C5_RELEASE_WHOLEGAMES_GAMES=l30`
  for the call), passed as the hotfix's subset is (`subset_pass(res, subset)`), carried by the store while the DLLs are
  byte-identical (the key holds `major_civs`, so m43 records stay apart). hi_journey picks `release-basic-m43` and
  `release-large-m43` itself (the original's `take_major`/`chosen`), on the installed layout. `plans.HOLDS_BY_TIER`
  still gives wholegames four headless places in a 43-civ take that plays one game (it holds too much, no harm).
  The command:
  `python -I tools/release/tiers/take_tier.py <tag> --tier intermediate --major-civs 43 --base-take <the last accepted
  43-civ take's work> --results C:/c5-64bit-work/<t>/results --descriptor <d> --fork <clone> --carry <mode> --for-art
  --jobs 4 --scratch C:/c5-64bit-work/<t>/m43-scratch --who <t>` (default work `<root>/<tag>-m43-intermediate`;
  `C5_RELEASE_TESTBEDS=local1` for the one game; `C5_RELEASE_HI_INSTANCE` a clean 26577625 mods-form instance).
  Not tested live: the m43 build and pack inside the tier (5018's build and stages/pack.py take the count already `[C]`).
- **The tier report** (`tiers/stages/tierreport.py`): a section "Accepted scene differences" (bundle, shot, pixels,
  reason, who, date, the accept file) from the scenes stage's `accepted`. It appears when the plan has `scenes`, says
  "None" for none or for a carried result, and is absent from a plan without scenes. The scenes stage no longer puts
  them in `tier_subset` (5019's interim route).
- **Tests** (`test_tier_intermediate.py`, classes M43Plan, M43Arguments, WholeGames43, OldExe, InstanceTiny,
  AcceptedScenesReport, 21 tests; the whole tier set 377 passed in 11.6 min, 17:17-17:29; `test_tier_store.py`'s import fixture gained the instance entry, the proxy and -ia
  hashes; `test_tier_scenes.py` reads the new section). Negative controls
  (`C:/c5-64bit-work/0630/5023-scratch/negctl.py`, each check broken in place, its test run, the file restored by its
  bytes): base of another count accepted; the 43 plan equal to the standard; oldexe playing the stock route, writing
  into hi_journey/, keeping C5_RELEASE_INSTANCE, wanting another EXE build, not stopping a --for-art take; instance needing
  refsave, its key without the proxy, its scope ignored; the 43-civ wholegames playing all games; the report listing a
  carried accept: 12 of 12 failed their test.
- **Live checks still needed**: the oldexe stage on a clean Goldberg EXE-4390913 mods-form instance (one game place,
  about 25-30 min with the installer build and the removal); the instance stage's Tiny arm on a pool instance (about
  7-10 min); one 43-civ intermediate take (build 38, l30 127 on one local testbed, both m43 journeys 42 on a 26577625
  instance: about 3.5 h serial, about 2.2 h with the game beside the whole game) `[I]`.

## 8. The live checks and the measured intermediate take (ticket 5024)

Second B lead-0630, 2026-11-22 from 17:51. Every time below is read from the driver's log or the state file `[L]`.

**8.1 Set-up.** Base take `C:/c5-64bit-work/cand2/tier3` (Release-5.4.8, the second beta's hotfix take, GATE PASS,
D162 yes; its x64 DLL adopted from `cand1/run`), read only. Takes run from a detached checkout of the team head
(`C:/c5-64bit-work/0630/runco`), so that the lead's worktree can be edited while a stage reads the tools; the fork
clone is the lead's own (`C:/c5-64bit-work/0630/fork`: `git clone --no-checkout --reference <the DLL repository's
clone> <cand1/fork>`, origin set to the fork's URL), so the base's clone is never written. Two results stores: the
live checks' (`C:/c5-64bit-work/0630/results-live`: the scene baselines copied, cand1/run and cand2/tier3 imported:
narrowing, census, model, purity, savetest, wholegames) and the measured take's (`C:/c5-64bit-work/0630/results`: the
scene baselines only, so every stage runs fresh, the DLL-change case the 3-3.5 h budget is for). Settings: the
script `C:/c5-64bit-work/0630/b-scratch/tierI_env.sh` (owners lead-0630, `C5_HARNESS_BIN` of the run checkout,
`C5_SCENE_ASSET_ROOT`, `C5_RELEASE_HI_TICKET=0630`, `C5_RELEASE_INSTANCE` unset).

**8.2 Findings of the live checks** `[L]`, each fixed on the team branch with tests:
- `take_tier.py` refused cand2/tier3 as a base ("its report stage is missing"): `base_unaccepted` knew only the
  validated plan's `report` stage, so no tier take could be the next take's base. A take with `config.tier` is now
  accepted by its `tierreport` (28bef5de0; test_tier_e2e).
- The fresh x64 DLL of the unchanged pin differed from the base's by its `__TIME__` only: the x64 BUILD KEY rule
  (MC #10515; [validation-tiers.md](validation-tiers.md) 20; dde0d762e). With it the live build adopted the x64 DLL by
  key: step 0 (survey to pack) 18:43:42-18:52:32, build 5 min 34 s (server and proxy by `--deps`, compositor 85 s
  beside them), narrowing, census, model and purity carried from cand1/run, pack 1 s. The first try with the old rule
  built the x64 DLL in 735 s (link place wait 0 s).
- The modpack claim was refused "unknown ticket: 5024" (a child ticket not on main) and the claim helper retried it as
  "no free instance" every 120 s: a refusal no wait cures now ends the claim at once (f0c768b9e); use the epic's id
  (`C5_RELEASE_HI_TICKET=0630`).
- The modpack stage's default save (`AutoSave_0019 BC-3240`) was gone from c5h-new-mpk: the instance's autosaves had
  rolled to 0020-0031. Without `C5_RELEASE_MODPACK_SAVE` the newest numbered autosave stands in (said, recorded as
  `save_fallback`); a named missing save still fails (f0c768b9e). A measured or release take names its save (MC-B).
- The scenes stage STOPS when the repository is not at the build's commit ("the player must be built from the take's
  own sources"): a take runs from one commit from its build on; moving the run checkout between stages costs a rebuild
  or a checkout back.

**8.3 The live checks** `[L]`:

| Check | When (2026-11-22) | Wall | Instance | Result |
|---|---|---|---|---|
| modpack | 19:04:44-19:08:28 | 3 min 44 s | c5h-new-mpk (26577625, modpack548) | PASSED: installer 15 s, claim 15, DLC listing 19, install 10 (record form = modpack), play 127 (AutoSave_0031, turn 32 -> 33, clean exit, no box), removal 11, Assets/DLC tree 0 differences, DLL back by hash, verify 0, released |
| scenes | 19:15:27-19:23:35 (sceneshot build 147 s before it) | 8 min 8 s scenerun (488 s) | none (GPU) | FAILED as a check of the library's state, not of the product: the 8 unchanged bundles drew 65 shots, all `equal` to the 17:00 bootstrap set (P1 within); the 8 kit bundles re-recorded into the master at 19:11:02 (ticket 4749, format 10, unit ground shadows) were refused by this take's player (`sceneshot: FAILED 27 NewerVersion`, 17 refusals: the build is dde0d762e, a format-9 reader). Own library copy `C:/c5-64bit-work/0630/scenelib` (index.json SHA-256 db61167f...241f, synced 19:12:45-19:14, 1,226 files, 8.38 GB in 75 s; consistent: the swap ended 19:11:02) |
| oldexe | 19:28:35-19:41:26 (first try 19:25 skipped "EXE build unknown": c5h-b records none; fixed 9dcf073b0, the pool's default 4390913 assumed only when wanted) | 12 min 51 s | c5h-b (4390913, mods548) | PASSED: installer 15 s, claim 5, install 7, release-basic product route passed in 558 s (stage step 694 s), removal 22 s, restore ok, released |
| instance | 19:41:39-19:46:25 | 4 min 46 s | c5h-b | PASSED: Tiny arm, 164,856 verified, 0 mismatches, restore ok, released |
| sweep | 19:47:12-20:44:05, STOPPED by the operator at 20:43:34 (MC-B, an installer test of the owner's) | product run 3,350 s (55 min 50 s) | c5h-new-a (26577625) | PARTIAL: save HI-KIT-MEDIEVAL-LARGE-T200, 4 of 5 screens done (top_panel, city_screen, tech_tree, game_menu), unit_panel failed (a sweeper step), 13 sessions, 1 relaunch, 8 reloads, 0 findings. DEFECT: the sweeper ended on the STOP with its game running and ctx.run raised StopRequested before the stage's own stop, so the installer refused the removal (exit 7); recovered by hand (cli.py stop, runbook 3.4a's steps, My Games 0 differences outside 75 run-output rows, released 20:46:34); fixed 7f53745d7 (the game is stopped before the removal, the stop file ignored; test with an installer that refuses while the game runs, negative control fails it) |

**8.4 The first measured take: a partial figure** `[L]` (third B lead-0630). Take 1 ran 2026-11-22 20:57:18-22:09:03
(1 h 11 min 45 s) from runco 6fd1c320e. Work folder `C:/c5-64bit-work/0630/tierI-meas/work`; places
`headless=4,game=3,gpu=1`; results store `C:/c5-64bit-work/0630/results` holding the scene baselines only.
The x64 DLL and both 32-bit DLLs were adopted by key and hash from cand2/tier3; server, proxy and compositor were built.
It ENDED FAILED on the suites stage (exit 1), so its figure is partial. The times are wall clock on a busy workstation
and are not a D106 timing claim. Loads beside it: lead-0673's game on c5h-new-b, the w71/w74/w76-0042 builds, the 4808
D10 save test, 4940's sceneshot previews and impl-0681's server build (quietlock list 20:53 and 22:08).

| Stage | Start-end | Wall | Held |
|---|---|---|---|
| survey, branches, testdata, lock, tierraise (barriers) | 20:57:19-21:00:11 | 2 min 52 s (testdata 2 min 3 s, export preflight 47 s) | alone |
| build | 21:00:12-21:07:55 | 7 min 43 s (x64 adopted by key; server, proxy, compositor built) | cpu_build=1 |
| narrowing / model / offline | 21:07:55-21:15:07 | 1 s / 5 min 13 s / 7 min 11 s | -, -, headless=1 |
| census -> purity -> pack | 21:07:56-21:21:34 | 11 min 22 s, 2 min 13 s, 1 s | cpu_build=1 |
| suites | 21:07:56-21:52:57 | 45 min 1 s, FAILED (see below) | nothing |
| scenes | 21:07:56-22:09:03 | 61 min 7 s: sceneshot build 399 s, scenerun 2,341 s, retired base set 891 s; 16 bundles compared, 137 shots equal, 0 differ, 72 first sight (the 8 re-recorded kit bundles), ok | gpu=1 |
| savetest | 21:15:08-21:55:01 | 39 min 53 s (run_d10 2,393 s), ok | headless=4 |
| instance | 21:21:35-21:29:16 | 7 min 41 s, ok (c5h-b restored, verified, released) | game=1 |
| fixchecks, wholegames, hi_journey, oldexe, modpack, sweep, tierreport | not started | the failure started nothing new | |

**Why the take failed.** These are failures of the tier's set-up, not of the product. In the suites stage (3 lanes,
budget 45 min, running beside savetest, scenes, census and offline):
- harness timed out at its 900 s with 73 % done.
- release-hotfix timed out at 900 s with 31 % done.
- release-tiers was cut by the budget at 2,699 s with 35 % done.
- testbed was cut at 77 %. scenelib and wait were cut, and 14 suites were never started.
- scenario failed on 11 skips: the live-terrain Lua tests skip without `C5_LUA51_DLL`.
- Passed: safety-static 138 s, export-gate 78 s, export-tests 463 s, queue 881 s, installer 677 s, exebuild-tables 686 s,
  instances 257 s and the small suites.

**Finding `[L]`: process-heavy suites run 6-10 times slower beside the game stages.** release-tiers reached 35 % in 45 min.
Near-quiet it runs whole in 24.0 min (areas.json `measured_s` 1,438 s) and in 13.2 min (443 tests, 17:35 today), so beside
the stages it would need about 2.1 h. harness projects to about 1,230 s against 663 s near-quiet. The suites that start
many Python processes (the release, harness and testbed tests) suffer most. **Correction (2026-11-22, MC #10778):** the
study's section 6 line for the intermediate tier, "all, on three lanes (about 45 min)", does not hold when the suites run
beside the game stages on this workstation. It replaces nothing in the study's text, which is the Architect's. The tier
now runs the suites as a barrier after the build (below).

**Changes on the team branch after take 1** (each with tests and a negative control):
- `admit()` in parallel.py (439d8ba57): a stage that waits for places reserves only its own need. Take 1 lost
  21:29-22:09 of three game places: under the old rule, hi_journey waited for the GPU place while scenes drew and kept
  every game place, so oldexe, modpack and sweep waited too.
- The suites stage sets `C5_LUA51_DLL` to the take's own `<x64_out>/deps/stubs/lua51_x64.dll`. Checked: with it the
  scenario suite passed 82 of 82 in 5.6 s.
- areas.json `timeout_s_by_tier`: harness 1,800 s in the intermediate and full tiers.
- run_suites: above the hotfix tier, a `hotfix_substitute` whose suite also runs is reported `replaced` ("runs within
  release-tiers") and not run twice.
- MC #10778: in the intermediate tier the suites are a barrier right after the build. No game stage of the take runs
  beside them; other teams' games and builds may (near-quiet, not quiet: listed with each take).

**8.4b Take 2** `[L]` (from 67e9fd225; work folder `C:/c5-64bit-work/0630/tierI-meas2/work`; started 22:28:31, ENDED
FAILED 23:26:01 in the suites barrier). The barriers took 22:28:32-22:33:50 (5 min 18 s; the game copy took 148 s against 88 s
in take 1, export preflight 67 s against 47 s, while take 1's folder was being moved to F: [I]). The build ran 22:33:50-22:41:01 (431 s, adopted as in take 1). The suites
barrier ran 22:41:01-23:26:01 (45 min, the budget) with no stage of the take beside it. Beside it on the machine: 0683's
game on c5h-new-b, 4808's x64 server run, 4940's run_all scenebundle suite, 0688's composite cost arms, and the
w74/w76-0042 builders (quietlock 23:05); MC-B held B's new heavy runs from about 23:05.
- **The cause:** release-tiers was cut at 2,699 s with 43 % done, against 24 min whole near-quiet, so the machine's
  other loads alone slowed it about 2x. perf-ledger failed on 15 skips, "could not import 'duckdb'": the runner's Python
  has no duckdb.
- **The other 33 suites passed.** Times: harness 1,268 s (over the old 900 s limit; the 1,800 s tier timeout held),
  queue 695 s, installer 729 s, exebuild-tables 649 s, export-tests 323 s, tracker-server 202 s, perf-quietlock 224 s,
  testbed 196 s, instances 144 s, safety-static 82 s, export-gate 54 s, scenario 6.6 s (82 passed with the take's Lua).
  release-hotfix was reported "within release-tiers".
- **Rulings that followed:**
  - MC #10917: release-tiers is OUT of the intermediate take. areas.json makes it a full-tier suite; in the hotfix and
    intermediate tiers release-hotfix (4.3 min near-quiet) stands in; and the whole folder runs as the tier tool's own
    tests at every merge that changes tools/release.
  - MC #10921: no allow-list and no install for duckdb. perf-ledger runs with the ledger venv
    (`%USERPROFILE%/c5-ops/venvs/ledger/Scripts/python.exe`, duckdb 1.4.5) through the suite's new `python` field. A
    missing interpreter fails the suite by name and never falls back. With the venv: 284 passed in 126 s.

**8.4c Take 3** `[L]` (fourth B lead-0630; from runco faf25f07e; work folder `C:/c5-64bit-work/0630/tierI-meas3/work`;
places `headless=4,game=3,gpu=1`; started 2026-11-22 23:41:15, ENDED FAILED 2026-11-23 00:30:05 in the suites barrier,
49 min). Wall clock on a busy workstation, not a D106 timing claim. Loads registered with quietlock just after the end
(00:38): 4808's PGO-instrumented x64 DLL build and link, w77-5056's sceneshot build, w75-0042's render builders, 4940's
shell build.

| Stage | Start-end | Wall | Held |
|---|---|---|---|
| survey, branches (skipped, `--carry`), testdata, lock, tierraise | 23:41:16-23:45:20 | 4 min 4 s (game copy 157 s, export preflight 47 s) | alone |
| build | 23:45:21-23:53:49 | 8 min 28 s (x64 and both 32-bit DLLs adopted as in takes 1-2; server 342 s, composite 116 s beside it) | cpu_build=1 |
| suites (barrier) | 23:53:49-00:30:05 | 36 min 16 s, FAILED (below) | alone |
| every later stage | not started | | |

- testdata's `c5h snapshot` exited 1 with 18 of 279 build steps failed, all DLC_SP_Maps_2/_3 `WB_Script_*.lua` "no
  GetMapScriptInfo": the accepted failures of `tools/release/stages/testdata.py` `s_snapshot` (listed in
  `testdata.json` `steps.snapshot.accepted_failures`; test `test_descriptor_testdata.py`). Not a silent pass.
- **Suites:** 32 passed, among them harness 1,367 s (2,227 passed, 30 allow-listed skips; inside its 1,800 s tier
  timeout), queue 774 s, exebuild-tables 666 s, installer 658 s, export-tests 383 s, perf-quietlock 281 s, testbed
  278 s, instances 205 s, perf-ledger 138 s (the ledger venv), safety-static 97 s, export-gate 53 s, scenario 6 s.
  release-tiers `replaced` (MC #10917). Two failed:
  - **release-hotfix timed out at 900 s** with 267 of 474 tests done (56 %, all passing). Cause: the suite's list holds
    every `test_tier_*.py` file (a RealMap test enforces it), so M2's six files (tier_intermediate, linkwrap, modpack,
    parallel, scenes, sweep: 163 tests) joined it and it grew from 238 tests (M1, 260 s near-quiet) to 474; its 900 s
    and `measured_s` were M1's. In take 2 it was reported "within release-tiers" and never ran at this size. Fixed on
    the team branch: `timeout_s_by_tier` intermediate and full 1,800 s (as harness), with a RealMap test and a negative
    control. MC #10980 (2026-11-23 00:4x) then moved the six M2 files to a new intermediate-tier suite
    `release-intermediate` (163 tests); release-hotfix is back to the hotfix tier's scope (311 tests); release-tiers
    stands in as both in the intermediate tier (`hotfix_substitute` may be a list); the suites barrier's budget is
    60 min (was 45).
  - **tracker-server: 1 failed of 62**, `test_pass_statistics_and_slow_pass_warning`: a load flake in the test, not a
    product fault. Its hook makes the git part wait 1.5 s on every pass after the first, and it asserts that /healthz's
    `slowest` pass has a git part over 1.4 s; under load the unhooked first full pass (git part 1.118 s) was the slowest
    in total, so the assertion read the wrong pass. No port or file conflict. Fixed (MC #10980, one file added to
    0630's touches): the test asserts on the hooked passes (the median pass, the warning entry naming git), with a
    negative control (a hook too short to make a pass slow fails it).

**8.4d Before take 4: the solo suites and a sharing-violation fault** `[L]` (2026-11-23, runco dd52b0a2a, through
`quietlock.py run`, 3 lanes, tier intermediate, beside other agents' builds): tracker-server 62 passed in 197 s,
safety-static 55 in 122 s, release-hotfix 314 passed in 1,003 s (the last minutes beside the repro below),
release-intermediate 162 of 163 in 562 s. The failure: test_tier_parallel `test_negative_control_holds_that_fit_overlap`,
take_tier exit 1 with every stage line "ok" in the 12 lines the test prints.
- **Cause, caught:** 12 copies of test_tier_parallel, 6 processes at once (01:01:33-01:06:02), with the full driver output:
  2 copies failed with "take_release: FAILED <stage>: its stage worker exited 1 without a result", the worker's
  traceback `PermissionError: [Errno 13]` on `work/state.json`, read at take_tier.py `main` (the recorded config) and at
  take_release.py `build_config`, while the parent replaced the file. 67e9fd225 had given the retry only to the State
  class's own reads and replace. Fixed: those two reads, `--art-check`'s read, and parallel.py's reads of the stage
  results, partial states and pid files (whose `except OSError` turned a sharing violation into a failed stage) and its
  result replace now retry on PermissionError for up to 5 s (`_retry_sharing`), with a test that refuses the first two
  reads of each of the take's state and result files and a negative control without the retry (fails).
- Two other copies failed timing assertions of the same file under that 6x load: `test_a_hold_that_does_not_fit_waits`
  (narrowing did not overlap build, sleeps of 1 s) and `test_a_stop_file_ends_the_take_with_the_running_finished`
  (model still pending). Test timing under an extreme load, not a driver fault `[I]`; not changed.

**8.4e Take 4** `[L]` (from runco 5e73cabd8; work folder `C:/c5-64bit-work/0630/tierI-meas4/work`; started 2026-11-23
01:10:50, ENDED FAILED 01:56:31 in the suites barrier, 45 min 41 s; wall clock on a busy workstation, not a D106 timing
claim; beside it: 4808's PGO build, w75/w77-0042 builders, 4940's links). Barriers and build 01:10:50-01:19:04 (build
317 s, DLLs adopted). Suites barrier 01:19:04-01:56:31, 37 min 27 s (budget 60 min): 36 of 37 passed, among them
release-hotfix 1,126 s (311-test list, tier timeout 1,800 s), harness 974 s, queue 616 s, installer 573 s,
exebuild-tables 531 s, perf-quietlock 330 s, tracker-server 275 s (the fixed test held), release-intermediate 789 s. No
"without a result" (the 8.4d fix held). The failure: release-intermediate, test_tier_linkwrap
`test_negative_control_without_the_place_links_overlap`: process B started 1.5 s after A, so the 0.8 s fake compiles
did not overlap (A 37.66-38.49, B 39.11-39.98, epoch seconds). A timing assumption of the test, not a product fault.
Fixed: linkwrap's two overlap tests use 4 s compiles and 5 s links, and test_tier_parallel's six overlap and stop tests
4-6 s sleeps (the failures of 8.4d's 6x load included). The named modpack save was put before the take and removed by
hash after it; the pool's three instances were free before and after.

**8.4f One rerun of a failed suite** `[C]` (MC #11081, 2026-11-23): in the intermediate tier the suites stage calls
`run_suites.py --rerun-failed 1`. A FAILED suite (not a timeout, not over budget) runs once more; its first try's log is
kept as `<suite>.try1.log`. A pass on the rerun counts: the suite's record gets `flake` (the failing tests' names from
pytest's output, the FAILURES section up to 4,000 characters, the log), results.json `flakes`, the stage's outputs
`flakes` and one stage line `suites: FLAKE <suite>: ...`; each such test gets a ticket that fixes its timing. A second
failure fails the suite and the take (`first_try` kept). `--rerun-failed` accepts only 0 or 1. Tests: a suite that
fails once passes and is recorded; its negative control without the rerun fails; a suite failing twice fails and ran
exactly twice; `--rerun-failed 2` is refused; the stage lists a flake in its outputs and its line.

**8.4g Take 5: unmeasured under load** `[L]` (from runco d7cb98050; work folder `C:/c5-64bit-work/0630/tierI-meas5/work`;
started 2026-11-23 02:12:16, ENDED STOP (exit 10) 03:24:45: the suites barrier ran out of its 60 min budget). Not a
measurement of the tier: the workstation ran 9 heavy jobs beside the barrier (quietlock 03:13: oneoff-9079's game on
c5h-new-b, lead-0673's game on c5h-c, 0642's and impl-4983's render run_all suites, two render-host pytest runs,
w82-0042's server builders, worker-0673l's perf pacing tests). Barriers and build 02:12:16-02:24:41 (build 553 s,
DLLs adopted, composite 198 s). Barrier 02:24:42-03:24:45: suites 10-55 % slower than in take 4 (harness 1,435 s,
exebuild-tables 827 s, queue 780 s, installer 741 s). The one-rerun rule (8.4f) ran as written:
- FLAKE perf-quietlock `test_wait` (failed first, passed on its rerun): ticket 0696 (the MC's).
- FLAKE release-intermediate `test_tier_modpack Failures.test_turn_that_does_not_advance_fails` (the fake save did not
  load within its 3 s limit): ticket 0695, root cause and fix in 6.5.
- release-hotfix failed its first try in 1,565 s on `test_tier_build SideBySide.test_same_records_and_layout_as_the_serial_build`
  (1 build_vp_clang call counted where 2 were made): ticket 0694. Its rerun was CUT BY THE BUDGET after 940 s, not
  failed; perf-turnbench was cut too. The record then said "failed twice"; corrected: a rerun cut by the budget or
  its timeout reads "failed once (...); its rerun over-budget: ..." (test).
The named save was put before and removed by hash after; the three instances free before and after. MC #11172: take 6
with B's and A's new heavy starts held for its barrier hour.

**8.4h Take 6: the held barrier, and a test of our own that failed** `[L]` (fifth B lead-0630; from runco 90cfc7882,
the team branch with main merged in; work folder `C:/c5-64bit-work/0630/tierI-meas6/work`; started 2026-11-23
05:31:39 when oneoff-9079 released c5h-new-b, ENDED FAILED (exit 1) 06:02:42 in the suites barrier; no game stage ran,
no stage claimed an instance). B's and A's new heavy starts were held from BARRIER START (05:31) to BARRIER END (06:02);
running at the start were the MC's 5052T sceneshot build (since 05:29) and w81-0042's render test builders (since
05:31) (quietlock 05:31:55). Wall clock on a busy workstation, not a D106 timing claim.
- tierraise 05:33:22-05:34:00 (export preflight 38 s); build 05:34:00-05:38:06 (246 s: x64 DLL adopted by its build
  key, 32-bit DLLs adopted, server 152 s, composite 69 s).
- Suites barrier 05:38:06-06:02:42 = 24 min 36 s (take 5 under 9 jobs: 60 min and cut). Suites with the hold: harness
  661 s (take 5: 1,435 s), exebuild-tables 452 s, queue 379 s, installer 327 s, release-intermediate 321 s,
  release-hotfix 505 s + 495 s (two tries), export-tests 149 s.
- Cause of the failure, test-side: release-hotfix failed both tries on test_tier_suites
  `test_a_rerun_cut_by_the_budget_is_not_called_a_second_failure` (added in 5f4060532 after take 5). The test slept
  1.5 s in both tries under a 4 s budget, so on a quieter machine the rerun finished inside the budget and honestly
  failed twice. Fixed in 935462d45: the first try fails at once, only the rerun sleeps (120 s) past a 20 s budget, and a
  first try cut by the budget is a skip with its reason (it took over 8 s once beside other agents' loads). 18 of 18
  runs passed 6 at a time (06:12-06:14). Negative control: with the old wording ("failed twice" for any second
  outcome) the test fails. test_tier_suites 79 passed on 935462d45 (06:19-06:28).
- The first try also failed 0694's test (test_tier_build `IntermediateAdoption.test_changed_x86_built_side_by_side`,
  1 != 2 build_vp_clang calls); it passed on the rerun. Cause `[L]`: three Python processes appending to one file on
  the workstation lost 255 and 1,425 of 9,000 lines (2026-11-23 06:2x; on Windows the C runtime's append is a seek to
  the end, then a write). The side-by-side fake builders shared that call log. Fix (ticket 0694): each build writes
  its own call file, which the side-by-side fixture reads. 30 of 30 runs passed 6 at a time (06:38-06:43). Negative
  controls: `0 != 2` when the fixture does not read the own files, `4 != 2` when each build writes its call twice.
- Lesson (MC #11292): a test a lead adds to the tier gets one run under the take's own conditions (run_suites from the
  take's checkout, the tier's selection and lanes) before the take starts.
The named save was put by hash before and removed by hash after (absent).

**8.4i Take 7: the first take through the game stages, a stop in oldexe and one resume** `[L]` (fifth and sixth B
lead-0630; runco 71203dca3; work folder `C:/c5-64bit-work/0630/tierI-meas7/work`, moved to F: after the take; places
`headless=4,game=3,gpu=1`; instances c5h-new-a (HI journeys and sweep, 26577625), c5h-a (oldexe, 4390913, in place of
c5h-b: `C5_RELEASE_OLDEXE_INSTANCE=c5h-a`, MC #11292), c5h-new-mpk (modpack, named save put and removed by hash);
results store `C:/c5-64bit-work/0630/results` holding the scene baselines only, so every stage ran fresh and nothing
was carried). Wall clock on a busy workstation, not a D106 timing claim. Loads beside it: at 07:19 (quietlock) 0699's
load loop (32 busy loops, since 06:03), w81-0042's and w83-0042's scene-bundle builders, impl-0700's harness pytest,
impl-4984's game on c5h-new-b and impl-0698's render bake test; at 09:35 impl-4986's run_all and scenebundle-programs
suite and impl-0698's live-terrain build. B's and A's new heavy starts were held only for the suites barrier.

Take 7 ran 2026-11-23 07:19:48-09:33:46 (2 h 13 min 58 s) and ENDED with a STOP (exit 10) in oldexe:

| Stage | Start-end | Wall | Held | Result |
|---|---|---|---|---|
| survey, branches, testdata, lock, tierraise (barriers) | 07:19:49-07:24:49 | 5 min 0 s (testdata 236 s, tierraise 61 s) | alone | ok |
| build | 07:24:50-07:29:29 | 4 min 39 s | cpu_build=1 | ok (x64 DLL adopted by its build key, 32-bit DLLs by hash; server, proxy, compositor built) |
| suites (barrier) | 07:29:29-07:49:29 | 20 min 0 s | alone | ok, no flake (release-hotfix 464 s, release-intermediate 309 s, harness 654 s) |
| narrowing / model / offline | 07:49:29-07:53:07 | 0 s / 2 min 53 s / 3 min 38 s | -, -, headless=1 | ok |
| census -> purity -> pack | 07:49:29-08:00:13 | 8 min 9 s, 2 min 33 s, 1 s | cpu_build=1 | ok |
| scenes | 07:49:29-09:03:50 | 74 min 21 s (sceneshot build 201 s, scenerun 2,011 s, retired base set 2,225 s) | gpu=1 | ok: 16 bundles compared (8 from retired copies), 137 shots equal, 0 differ, 8 first sight |
| savetest | 07:53:07-08:30:01 | 36 min 54 s (run_d10 2,214 s) | headless=4 | ok |
| instance (Tiny arm) | 08:00:13-08:05:36 | 5 min 23 s (arm-tiny 272 s) | game=1 | ok, on c5h-a (the pool's choice, below) |
| oldexe | 08:00:13-08:19:38 | 19 min 25 s (waited 6 min 19 s for c5h-a; journey 08:07:48-08:17:46) | game=1 | **STOP**: release-basic did not pass (below) |
| modpack | 08:05:36-08:10:50 | 5 min 14 s | game=1 | ok (c5h-new-mpk: installed, loaded, one turn, left, removed) |
| sweep | 08:10:51-09:33:46 | 82 min 55 s (sweep-product 4,880 s) | game=1 | ok, phase verdict PASS, 0 findings; 4 of 5 screens, unit_panel failed as in 8.3 ("plot 47,47 is off screen"), 1 relaunch, 8 reloads |
| fixchecks, wholegames, hi_journey, tierreport | not started | after a stop the side-by-side driver starts nothing new (parallel.py); they were waiting for savetest (wholegames `USES`) and for the GPU place (scenes) | | |

**Why oldexe stopped `[L]`; cause `[I]`, ticket 0706.** release-basic on the installer-installed layout of c5h-a
(product route) passed steps 1-25 and its checkpoints founded and research, then stopped at step 28: End Turn's judge
wait `state.turn>=4` reached its 150 s deadline. The judge at the stop: turn 3, `ENDTURN_BLOCKING_UNITS`, the Shoshone
Pathfinder at (45,30) selected with 4/4 moves, no popup, no screen, no box ("A UNIT NEEDS ORDERS" in the picture).
Each turn of the journey is End Turn + 800 ms + SPACE (skips the Warrior), End Turn + 800 ms + SPACE (skips the
Pathfinder), End Turn with a wait for the next turn. In turns 0-2 every SPACE took effect (the judge after each step
read what the recording read). In turn 3 the second SPACE (recording step 27) did not. Step timing matched the
recording (7.5 s against 7.1 s). The stage had set AutoUnitCycle = 1 (the recordings' value, 0530; c5h-a is 0 as
installed; backup kept and restored). The game's Lua.log has the three turn changes and nothing else. The key is
posted (WM_KEYDOWN, 60 ms, WM_KEYUP) and was reported delivered. Not a product fault shown: the same journey passed on
c5h-b (4390913) in 558 s on 2026-11-22 (8.3). Evidence kept at `<take>/oldexe-take7-run/` and
`<take>/oldexe-take7-report.md` (the resume writes over the work folder's copy). Fix direction (MC #11436, ticket
0706): the step waits for its effect and presses once more if it did not happen, the retry counted in the record.

**The resume (MC #11436, #11445).** One resume per stage, on the terms of the suites' one-rerun rule: recorded with the
stop and its cause, the figure is take 7's time plus the resume's own, both named, and a second failure of the stage
fails the take. fixchecks, wholegames and hi_journey run there for the first time. Started 09:34:57 from the same
checkout and work folder (`C:/c5-64bit-work/0630/b-scratch/resume_take7.sh`); every ok stage was skipped on unchanged
inputs within a second. It ran 09:34:57-10:29:11 (54 min 14 s) and ENDED exit 0: **GATE PASS, the intermediate tier
reached for Release-5.4.8; D162 yes (both journeys passed, product and stock)**. Loads beside it: impl-4986's
run_all and scenebundle-programs suite, impl-0698's live-terrain build (quietlock 09:35).

| Stage (resume) | Start-end | Wall | Held | Result |
|---|---|---|---|---|
| oldexe (the one resume) | 09:35:01-09:49:57 | 14 min 56 s (replay 614 s) | game=1 | ok: release-basic on 4390913, product route, passed; c5h-a restored and released |
| fixchecks (first run) | 09:35:00-09:38:04 | 3 min 4 s | headless=1 | ok |
| hi_journey (first run) | 09:35:01-10:24:21 | 49 min 20 s (product: release-basic 631 s, release-large 473 s; stock fresh, its inputs changed since the base take: release-basic 652 s, then release-large) | game=1, gpu=1 | ok on c5h-new-a, both routes. The stock release-basic game ended 0xC0000005 at its EXIT and Yes (10:13:57, no box): K24 of known-crash-causes.md, the `-ia` test DLL's exit crash, matched by timing `[I]` (no WER report kept); recorded, not counted |
| wholegames (first run) | 09:38:05-10:29:11 | 51 min 6 s (local4 1,321 s, local1 1,670 s, local3 2,749 s, local2 3,066 s) | headless=4 | ok, 4 games |
| tierreport | 10:29:11 | 0 s | alone | GATE PASS |

**The figure.** Take 7 2 h 13 min 58 s + the resume 54 min 14 s = **3 h 8 min 12 s** wall clock, against the study's
3-3.5 h for tier I, on a busy workstation (not a D106 timing claim). Not in it: the 6 min 19 s oldexe waited for c5h-a
(inside take 7's time), the 1 min 11 s between take 7's end and the resume (operator), and the 43-civ second take
(not run, 8.5 item 4). Without the stop the game stages would have overlapped more: hi_journey waited from 08:00 for the
GPU place held by scenes until 09:03, and wholegames for savetest until 08:30. In one uninterrupted take they would
start at those times and end at about 09:55 (hi_journey) and 09:25 (wholegames), so sweep (to 09:33) and hi_journey
would set the end, about 2 h 35 min `[I]`.

**Side finding `[L]`: the instance stage can take oldexe's instance.** With `C5_RELEASE_INSTANCE` unset (as
docs/RELEASE-TIERS.md says), the instance stage claims the pool's choice of a 4390913 mods548 instance. At 08:00:13
both started; the instance stage got c5h-a in 21 s, and oldexe, which names c5h-a, was refused ("claimed by
lead-0630-release", then "a process runs from it") and retried every 120 s until 08:06:56: 6 min 19 s lost, and the
instance stage's claim named no ticket (a LOUD line from the pool). Cheap remedy: when a second 4390913 instance is
granted, name it in `C5_RELEASE_INSTANCE`; otherwise the wait is bounded by the instance stage (about 5 min) and
oldexe's 90 min claim wait. A code remedy (the tier's instance stage skips the instances the other stages name) is
not made in M2.

**8.5 The four open items of 5024** (MC #10484; third B lead-0630, 2026-11-22):
1. *The three DLL compiles side by side, their overlap and memory:* **not verified.** In the live checks and in both
   measured takes the x64 DLL was adopted by its build key and both 32-bit DLLs by their SHA-256, so the side-by-side
   compile path of 5018 (linkwrap, the 8 GB floor, 350+ clang-cl processes) has run only on its hermetic tests. A take
   whose DLL pin or toolchain changed (or a work folder without a base take) is the first live run of it.
2. *Suites side by side sharing files or ports:* **no conflict seen** `[L]`. Take 1 ran the suites on 3 lanes beside
   every other stage (21:07:56-21:52:57). In its 22 suite logs: no "address already in use" (WinError 10048), no
   "being used by another process" (WinError 32), no PermissionError. Its failures were time limits only (8.4). Take 2
   runs the same 3 lanes alone (the barrier). Not shown: suites that would share a port only on some runs.
3. *Does the fresh proxy DLL repeat byte for byte?* **No, not across commits** `[L]`. Take 1 was built at 6fd1c320e and
   take 2 at 67e9fd225; `git diff --stat` of proxy, shell, server, render, ipc, models, options, deps, exebuild and
   CMakeLists.txt between the two commits is empty, so the product sources were identical.
   - The proxy DLLs (1,571,328 bytes each) still differ in 23,764 bytes in 2,839 runs: the PE time stamp at 0x80, the
     checksum at 0xd0, and many short runs through the image. Each holds its own short commit id once.
   - server.exe and shell_composite.dll differ between the two takes as well.
   - So the instance stage, whose store key holds the proxy's hash, never carries across commits. It runs fresh in every
     take: 7 min 41 s in take 1.
   - A second build at the same commit `[L]` (ticket 0734, 2026-11-25; corrected 2026-11-25, this line read "Not
     measured: a second build at the same commit"): the intermediate tier's own build of dc259f0b1 gave server.exe
     (6c75d7fa...) and the proxy DLL (48524398...) BYTE-EQUAL to the hotfix take cand6/tier's build of the same commit
     (and to the 0.1b4 installer's payload); shell_composite.dll differed in 77 of 1,950,208 bytes, at the PE time stamp
     (0x108) and the debug directory, i.e. the build stamp [I]. So across commits the proxy differs (above), at one
     commit it repeats; the compositor never repeats byte for byte. [validation-tiers.md](validation-tiers.md) 23.
4. *The 43-civ build inside the tier* (`--major-civs 43`, INTERMEDIATE_M43): **not run.** Its plan and its build and pack
   stages pass on hermetic tests only (7.1). Not live: the m43 build and pack, and wholegames holding 4 headless places
   for a 43-civ take that plays one game.
