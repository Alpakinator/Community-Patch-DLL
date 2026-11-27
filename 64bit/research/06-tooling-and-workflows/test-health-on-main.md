# Test health on main: the suites no merge check runs, and what is red (ticket 0627)

Ticket 0627 (2026-11-20, first beta, D216). This file lists which test folders exist outside the merge checks, how to
run each one, and what each gave on main. It is written so that the next sweep can repeat it command by command.

**Run on:** main 5c455426a (2026-11-20 20:27 EST) plus ticket 0627's test commit (branch `task-0627-test-sweep`; it
changes only `shell/tests/test_model_programs_table.py`), on the workstation, from 2026-11-20 20:40 EST. Python 3.11.5
(anaconda), `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`. No game, no instance, no scene library. Builds made from the same tree under
`C:/c5-64bit-work/0627/`.

## 1. What the merge checks run, and what they do not [C, docs read 2026-11-20]

At a merge request a lead hands in two gate lines (`docs/MASTER-COORDINATOR-HANDOFF.md` 4.6, auditor cycle 7 H5):
`tools/export/check.py --rev HEAD --since main` and, when `tools/` changed, `tools/harness/tests/test_safety_static.py`.
The MC's merge-check notes add: scene-bundle rule A, a link of every builder of a shared source, and a live smoke of
a product-path hook. `server/tests/render/run_all.py` runs the render suites when a lead runs it. No check runs
`shell/tests`, `shell/frame/tests`, `proxy/tests`, `proxy/tools/test_*.py`, `server/tests` outside `render/`,
`render/tools`, `perf/` or `tools/*/tests`. Those run only when a ticket's own brief names them. Ticket 4965 ran "the
shell test files for these files": `test_model_programs_table.py` reads `unit_filter.cpp` as text but is named after
4874, so it was not among them (`research/02-exe-internals/exe-build-table.md` section 15, last subsection).
The same happened twice to `shell/tests/test_join_ring.py`. 0490 (EXE taps off on the stand-in) and 0494 (the host closer) each fixed `test_lockstep.py`, the file that failed in front of them, and ran only that file. `test_join_ring.py` imports test_lockstep's `INI` and `LsRun` and kept both faults (section 6).

## 2. How to repeat the sweep

The scripts are scratch files (not committed):
- `C:/c5-64bit-work/0627/sweep.py`: runs one pytest per folder, each through `quietlock.py run --who impl-0627`, with
  `--junitxml` and `--ignore-glob` for live files (`*test_live_*`, `*test_safety_live_*`, `*_live.py`,
  `*test_bake_live*`, `*live_*.py`).
- `C:/c5-64bit-work/0627/scripts.py`: runs the script-style tests (section 4).
- `C:/c5-64bit-work/0627/triage.py`: groups junit failures by their first message line.

The commands they run are listed below, so the sweep can be repeated without them. Run each from the repository root:

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider -rfEs --junitxml=OUT/<name>.xml \
        --ignore-glob=*test_live_* --ignore-glob=*test_safety_live_* --ignore-glob=*_live.py \
        --ignore-glob=*test_bake_live* --ignore-glob=*live_*.py <folder>

Run `perf/` as one pytest per `tests` folder, never as one session. Two pairs of modules share a name and collide in
`sys.modules`: `perf/sampler/sampler.py` with `perf/turnstudy/runner/sampler.py`, and `perf/ledger/page/build.py` with
the `build` that speedkit imports. One session over all of `perf/` gave 36 failures and 9 errors that do not exist
(2026-11-20 20:46-20:55). Run on their own, the same folders pass.

## 3. Pytest folders: results on main [L, runs 2026-11-20 20:40-23:30 EST]

"Inputs" names what the folder needs beyond a checkout: missing inputs give errors or skips that are not red tests.

| Folder | Result | Inputs, notes |
|---|---|---|
| `shell/tests` without builds | 219 passed, 132 failed, 581 errors | every failure and error is "C5_SHELL_BUILD is not set" (110 + 581) or "C5_WATER_READER must name water_reader_test.exe" (22): inputs, not red |
| `shell/frame/tests` without builds | 34 failed | all "C5_FRAME_BUILD is not set" |
| `proxy/tests` (pytest) | 3 passed, 1 skipped, 10 errors | the 10 errors are the known script-style files ("fixture 's' not found"); the skip wants `C5_PROXY_BUILD_OUT` |
| `proxy/gen/tests` | 33 passed | |
| `server/tests` without `render/` | 37 passed, 42 failed, 4 skipped, 7 errors | 31 failures need `C5_SCENE_LIBRARY` (extras scene suites: rule A slot and a sceneshot build, not run here); 7 need `C5_UNITS_TEST_EXE`; **4 red: `tools/test_save_mods.py`** (section 5); the 7 errors are script-style files run in section 4 |
| `render/tools` | 8 passed, 13 skipped | the skips want `UNITANIM` / `UNITSCENE` builds |
| `render/tests` | 68 passed | |
| `ipc/tests`, `models/tests`, `options/tests` | no Python tests | C++ only |
| `ops/tests` | collection error `SystemExit: 0` | `test_hooks.py` is a script: run as one (section 4) |
| `perf/speedkit/tests`, `perf/tests`, `perf/turnbench/tests` (each alone) | 25, 320, 15 passed | (outside quietlock, 2026-11-20 21:00) |
| `tools/harness/tests` without `test_safety_static.py` | 2201 passed, 12 skipped, 55 deselected | 17 min |
| `tools/harness/native/tests` | 2 passed, 6 errors | `test_shim.py` is a script (section 4) |
| `tools/d10/tests`, `diskwatch`, `drift` | 65, 81, 4 passed | |
| `tools/exebuild/tests` | 52 passed | 8 min |
| `tools/export/tests` | 55 passed | 7 min |
| `tools/hi/tests`, `tools/humanplay/tests` | 4, 32 passed | |
| `tools/installer/tests` | 36 passed | 8 min |
| `tools/instances/tests` | 142 passed | |
| `tools/psprobe/tests` | 9 passed, 2 failed | timing cases under load (a shell suite ran at the same time): see section 5 |
| `tools/queue/tests` | 91 passed | 35 min (it is product-level: each command is its own `ticketq.py` process) |
| `tools/release/tests` | 414 passed, 3 failed (100 min under load, 22:05-23:45) | 2 failed with "this repository moved during the build stage". The take tests read the real checkout's HEAD, and this ticket committed to the worktree at 23:07 and 23:15 while they ran. The third was `test_second_driver_in_the_same_work_folder_is_refused`. Rerun alone, 23:48-23:56, with no commit during it: 3 passed. **Do not commit in a checkout while its release tests run.** |
| `tools/scenario`, `scenelib`, `testbed`, `wait`, `hi/journeys` tests | 40, 34, 259, 21, 23 passed | |
| `tools/x64-dll/tests` | 116 passed | |
| `tools/x64-dll/cvgamedatabase/tests` | no Python tests | |
| `tools/tests` | 49 passed | 4.5 min |
| `shell/tests` with builds (below) | 831 passed, 97 failed, 4 errors (89 min, 21:28-22:57, beside the tools sweep) | 84 failures and the 4 errors are reader inputs: `C5_SHADOW_READER` 30, `C5_ICE_READER` 20, `C5_FOWMASK_PRODUCER` 19, `C5_OVERLAY_READER` 15, `C5_RENDER_TEST_BUILD` 4 (run_all's builders). **9 red: `test_verification_defects.py` `test_nonfinite_camera_never_published_ok`** (section 5); 4 load-sensitive (section 5) |
| `shell/frame/tests` with the build | 33 passed, 1 failed | load-sensitive (section 5) |

The shell builds used were `shell\build.cmd` with `C5_SHELL_BUILD=C:\c5-64bit-work\0627\shell-build`,
`shell\frame\build.cmd C:\c5-64bit-work\0627\frame-build`, and
`server/tests/render/water/build_water_tests.py --out C:/c5-64bit-work/0627/water`, with
`C5_WATER_READER=<that>/bin/water_reader_test.exe`. Each was started through `quietlock.py run`, and all three built
in about 6 minutes. The local exe tables (`C:/c5-64bit-work/exe-builds`) were already there.

## 4. Script-style tests: results on main [L, runs 2026-11-20 21:55-23:00 EST]

These run as `python -I -X utf8 <file> <args>` from the repository root, each through `quietlock.py run`. They build
what they need into their `--out` or `--scratch` folder (`C:/c5-64bit-work/0627/scripts/<name>/work`).

| Test | Args | Result |
|---|---|---|
| `proxy/tools/test_strictness.py` | none | 45/45 |
| `proxy/tools/test_cfgini.py` | `--scratch` | 6/6 |
| `proxy/tools/test_vp64helper.py` | `--scratch` | 193/193 |
| `proxy/tests/test_crash_filter.py` | `--scratch` | 124/124 |
| `proxy/tests/test_farm_guard_proxy.py` | `--scratch` | 11/11 |
| `proxy/tests/combat_hold/test_combat_hold.py` | `--scratch` | 15/15 |
| `proxy/tests/livereload/test_livereload.py` | `--scratch` | 10/10 |
| `proxy/tests/luaexec/test_luaexec.py` | `--scratch` | 9/9 |
| `proxy/tests/ui_bridge/test_ui_bridge.py` | `--scratch` | 9/9 |
| `proxy/tests/inout/test_inout.py` | `--scratch` | 20/20 |
| `server/tests/guard/test_server_child_guard.py` | `--out` | 23/23 |
| `server/tests/remote/test_dll_interface_version.py` | `--out` | 19/19 (12 min) |
| `server/tests/remote/test_dll_fatal_report.py` | `--out` | 43/45: a false failure caused by the folder name (section 5) |
| `server/tests/remote/test_exe_crash_report.py` | `--out` | 76/76 |
| `tools/harness/native/tests/test_shim.py` | none (after `build.bat`) | ALL PASS |
| `perf/turnstudy/budget/tests/test_budget.py`, `.../budget/stock/tests/test_stock.py` | none | 66 checks, 0 failed; 0 failed |
| `ops/tests/test_hooks.py` | none | exit 0 |
| `server/tests/budget/run_budget_test.py`, `crashdump/run_crashdump_test.py`, `effects/run_trigger_emitter_test.py`, `extras/run_extras_test.py`, `feed/run_feed_test.py`, `feed/run_ui_record_test.py`, `hidden_embark/run_hidden_embark_test.py`, `runfolder/run_runfolder_test.py`, `run_rt_ring_test.py`, `run_vfs_test.py`, `units/run_units_{fights,queue,records,seed}_test.py` | `--out` | all OK |

Not run (an input this sweep did not have, with the reason):
- `proxy/tools/test_proxy_rt.py`, `test_proxy_pack.py` and `test_proxy_session.py` need `--build <build_server out>`.
  `test_served_exception_text.py` needs a server build, the DLL source and the deps. `test_purity.py` and
  `test_policy_table.py` need the DLL source at main's pin, 1fd51353e. main still pins 1fd51353e, and MC-B's rule for
  tonight was no long build at that pin. A build from keep/pins-0.1b refuses on main until 0616 merges. For such a
  build, MC #8846 says to make `<out>/bin` writable in your own out folder only (ticket 0629).
- `server/tests/db/run_db_write_test.py`, `feed/run_remote_feed_test.py`, `feed/run_render_events_test.py`,
  `limits/run_player_limits_test.py`, `remote/stale_instance/test_stale_instance.py`: these need the fork source
  (`--src`/`--dll-src`) or stubs. `db/test_run_db_write_test.py` skipped because `deps/dll.lock`'s `release` layout
  `<home>/c5-i0/dlls/x64-1fd51353-p1e09fc7` is not laid out on the workstation (the lock says "to be laid out by
  the MC").
- `server/tests/extras/*_scenes.py`, `extras/units/*_scenes.py` (31 cases) and `units/test_units_runner.py` (7) need
  the scene library (rule A slot), a sceneshot build and `C5_UNITS_TEST_EXE`. `run_load_water_test.py`,
  `run_power_test.py`, `run_preinit_report_test.py`, `run_stdout_console_test.py` and `check_map_script_pair.py` need a
  server build or a game run.
- `server/tests/render/` is `run_all.py`'s.

## 5. Every failure, with its cause and owner [C: each cause read in the code and the log]

| Test | Red since | Cause | Owner | Disposition |
|---|---|---|---|---|
| `shell/tests/test_model_programs_table.py::test_the_table_equals_the_evidence` | 7cb3e57f8 (0590 stage 2, 2026-11-20 09:00; green at 1d207d3d8) | 4965 turned kSites' literal rows into `VPX_` symbol rows, and the test's regex matched none | 0590 (4965) | fixed in 0627: checks the symbols' shell.tsv anchors, offsets and site patterns against p5_sites.txt; 14 negatives |
| `server/tests/tools/test_save_mods.py`, 4 `MainInProcess` cases | 4a515a43f (Merge 0488, 2026-11-17 06:34; green at d997ea096) | run_server.py starts server.exe through `jobcap.run`, and the test mocked only `subprocess.run`. **For three days every run of this test started a real process (CreateProcess on a fake bin, WinError 2), and nobody saw it.** | 0488 / 0303 | fixed in 0627 (MC #8841): both routes faked, `subprocess.Popen` refused, and the test fails unless exactly one start went through a fake (negative checked: with the jobcap fake removed, all 4 cases fail with "started a real process outside the fakes") |
| `shell/tests/test_verification_defects.py::test_nonfinite_camera_never_published_ok`, 9 cases | 7cb3e57f8 (4964's 4a818131d) | test_host now logs `fake EXE: Steam build 4390913 (...) image at 0x...`, but `fake_base()` waits for `fake EXE: image at` | 0590 (4964) | fixed in 0627 (MC #8864): `FAKE_BASE_RX` allows the label and is anchored. The line's message must start with `fake EXE:`, and the capture is the 8 hex digits after `image at 0x`. `test_fake_base_line_pattern` covers both forms and five lines that must not match. The same old text is in the evidence driver `shell/evidence/9031/hostile/hostile_cases.py`, left as it is (not a test) |
| `shell/tests/test_join_ring.py`, 4 cases | 3608 (water_tap 1 by default, 2026-11-14) [I] and 0494 (2026-11-17) | no `EXE_TAPS_OFF` (0490's fix stayed in test_lockstep), and for 2 of the 4 also no `close_fn` (0494's closer is set only in test_lockstep's fixture) | 0490 / 0494 | **fixed** 2026-11-21: taps in 0627 (MC #8911), the closer in 0632 (`jrun` sets `r.close_fn = host_closer`); the file run alone: 4 passed [L] (section 6) |
| `server/tests/remote/test_dll_fatal_report.py` av/own negatives | since 0422 (latent) | the "no dll-fatal line" check searched the whole server log for the bare word `dll-fatal`, and a logged line names the run's path: the sweep's `--out` was `.../srv-dll-fatal-report/...` | 0422 | fixed in 0627 (MC #8866): it looks for `dll-fatal:`, the tag every dll-fatal line carries. Checked both ways (23:10-23:20): the same `--out` gives 45/45; a copy whose log has an injected `dll-fatal: report: written to X` fails exactly the two negatives (43/45) |
| `server/tests/render/scenebundle/sceneshot_test.cpp:1437` (`[warp]`/`[hw] unclaimed instance`, seen by lead-0615 on 4118a39fa) | not a code change: the input | sceneshot's NOTE names the instance of the INSTALL first (`InstanceOf(install)`), and lead-0615's `C5R_GAME_DIR` was the pool instance `c5h-a`, so the NOTE named c5h-a and not the test's c5h-T0614 (`C:/c5-64bit-work/0615/lead3/sst/out.txt` lines 1, 18, 115) | 0614 | red on main under any install inside a pool instance; draft NEW-0627-1 (one NOTE per source in sceneshot, or a non-instance install for the case) |
| `shell/tests` 4: `test_lockstep::test_ring_handover_lockstep[cpu]`, `test_residency::test_radius_keeps_rings_and_evicts[0]`, `test_residency::test_rebake_on_raised_generation`, `test_verification_defects::test_new_chain_at_the_old_chains_address_is_adopted` | load-sensitive | failed under the same load. Alone, 23:00 (6 cases with their parameters): 6 passed | their tickets | named, not widened |
| `shell/frame/tests` 1 case, `tools/psprobe/tests` 2 cases | load-sensitive | failed while the tools sweep and the shell suite ran at the same time (two pytest sessions of this ticket, plus the other agents' jobs on the workstation, 21:30-22:57). Alone, 23:00-23:10: frame 34 passed, psprobe 11 passed | their tickets | named, not widened (MC #8841) |
| `shell/tests/test_pacing.py::test_stall_while_panning_is_one_freeze`, `shell/tests/test_config_defaults.py::test_unreadable_ini_is_named_once_and_readable_again` (seen by lead-0673's 0697 run, 2026-11-23 06:33, main aed29d9e8) | since 0688 (pacing) and 0490 (ini); load only | test faults under CPU load [L]: under load the stall test saw a second >= 800 ms freeze (the UpdateViews hook commit suspending the host's threads) and world ages of 100+ ms (a starved stand-in), and the ini test's host (fixed 8 s life, 10-s wait) ended before the composite's ini watch began, which runs only after its dummy-device hook commit (7.9-9.1 s under load). Quiet both pass | 0701, 0702 (worker-0673s) | fixed in the tests (stall found by its QPC window, age bound by the stall; host past the commit, closed by WM_CLOSE); [frame-pacing-instruments.md](../08-performance/frame-pacing-instruments.md) 5b |
| `server/tests/extras/units/test_ships_moored.cpp` case `moored_flag_off`, `server/tests/extras/units/test_fix_unit_placement.cpp` case `place_flag_off` (seen by impl-4986's all-builders pass, 2026-11-23 08:15 EST) | ea8878286 (one-off 9081, 2026-11-22 01:34: FixAirBases defaults to 1, D235); first on main in merge 0b23173cc (2026-11-22 05:17) | both assert that the default extras module has nothing on (`!feat->anyOn()` with every option at its default), which FixAirBases' default 1 makes false | 9081 / 4985 | fixed by 4986 (branch `task-4986-airbases-intercept`, MC #11510): those two checks configure the module with `FixAirBases = 0`; 7 of 7 and 9 of 9 [L]. `test_units_extras`' `null_factory` was not a main fault (4986's fourth extra's line count) |
| `shell/tests/test_verification_defects.py::test_timeout_ms_limited_and_killed_server[4000000000-50-...]` (ticket 0507, 2026-11-24 18:25, main 9bda43beb, **under the quiet lock: 0 heavy jobs, CPU mean 28.5 %**) | since 0494's row rule (2026-11-17); not seen before because it needs a quiet machine and a host hitch | the test's row rule, not the composite [L from `fail_run2`'s logs, ticket 0729, 2026-11-25; was [I]; the arithmetic in section 7]: after the kill the composite gives up waiting only after 8 timeouts in a row; each `AcquireSync(1, 50)` lasted ~62.4 ms (acq_mean 62.37), which is 4 ticks of the default 15.625-ms timer [I: the killed `shell_server` held the 1-ms `timeBeginPeriod`, so its death returns the tick to 15.6 ms on a machine where nothing else asks for 1 ms; under load other processes keep 1 ms], so the give-up takes ~500 ms, one whole 0.5-s stats row. The test drops only the kill's own row (`rows[1:]`); here the kill came ~60 ms before that row's end and one host frame of ~215 ms outside the composite (ft_max 279.3 ms, comp_max 64.0 ms; test_host's own max 277.0 ms) stretched the give-up over the whole next row: 6 frames, ft_mean 98.553 ms; the give-up line at 18:25:06.641, full rate from the next row on (8,216 frames). The ~215-ms host frame's cause is not known ([I] candidate: the driver's clean-up of the killed server's shared texture) | 0494 / 3014 | **fixed in the test by 0729 (2026-11-25, branch `task-0729-row-rule-hitch`)**: `killed_server_rule` counts the rows from the row after the composite's `timed out 8 times in a row (timeout_ms=50)` line, not from the kill's row, and the test sets `stale_ms=5000`; the 0507 failure is replayed as a unit case (section 7). Was: "open, no test change in 0507 (its rule)". Evidence `shell/evidence/0507/` (take series), shell/README.md "Limits". **Whether these two files must run under the quiet lock (0507, 2026-11-24): no** [L, 3 runs under the lock: 2 passed; no lockstep wait past 0.5 ms]: the lock does not make them reliable, and this case's fault is likelier on a quiet machine [I] |
| `perf/` as one session | sweep artifact | module name collisions (section 2) | perf team | none needed when run per folder; a note in the perf README could prevent it (not done) |

## 6. The full shell suite: what "the shell suite" means [L, builds and runs 2026-11-20]

A shell run without every input below is partial. Each missing variable fails its cases with "must name" or "is not
set", and does not skip them. On 2026-11-20 a run with only the shell, frame and water builds had 88 such cases. A
report of a shell run names the inputs it had.

| Variable | The program | Built by (from the repository root, each through `quietlock.py run`) | Minutes (2026-11-20, workstation, under load) |
|---|---|---|---|
| `C5_SHELL_BUILD` | the folder | `set C5_SHELL_BUILD=<dir>` then `cmd /c shell\build.cmd` (builds c5render too) | about 4 |
| `C5_FRAME_BUILD` | the folder | `cmd /c shell\frame\build.cmd <dir>` | about 1 |
| `C5_WATER_READER` | `<dir>/bin/water_reader_test.exe` | `python -I server/tests/render/water/build_water_tests.py --out <dir>` | about 1 |
| `C5_SHADOW_READER` | `<dir>/bin/shadow_reader_tool.exe` | `python -I server/tests/render/shadow/build_shadow_tests.py --out <dir>` | about 1 |
| `C5_ICE_READER` | `<dir>/bin/ice_tap_test.exe` | `python -I server/tests/render/ice/build_ice_tests.py --out <dir>` | under 1 |
| `C5_FOWMASK_PRODUCER` | `<dir>/bin/fowmask_producer.exe` | `python -I server/tests/render/fow/test_fow_build.py --out <dir>` | about 3.5 |
| `C5_OVERLAY_READER` | `<dir>/bin/test_reader_tool.exe` | `python -I server/tests/render/overlay/test_reader_build.py --out <dir>` | under 1 |
| `C5_RENDER_TEST_BUILD` | `<dir>/bin` | `python -I server/tests/render/build_render_tests.py --out <dir>` | about 5 |

The local exe tables (`exe_tables`, `C:/c5-64bit-work/exe-builds`) must exist (`exe-build-table.md` section 6). Then
run `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider shell/tests shell/frame/tests`.
`shell/tests/conftest.py` says to run without `-I`. Alone this takes about 60 min (3,685 s in
`ops/effectiveness/facts.md`). On 2026-11-20, beside the tools sweep, it took 89 min. Optional inputs with defaults:
`C5_TINY_TAP`, `C5_GROUND_TRUTH`. The script `C:/c5-64bit-work/0627/readers.sh` holds these builds in order (scratch,
not committed).

**The rerun with all inputs, 2026-11-20 23:17 to 2026-11-21 00:15 EST [L].** The 9 reader-dependent files
(`test_shadow_tap`, `test_ice_tap`, `test_fow_models`, `test_overlay_tap`, `test_camera_offline`, `test_join_ring`,
`test_residency`, `test_terrain_scene`, `test_verification_defects`) ran with the six readers built at 23:06-23:17
(181 MB in `C:/c5-64bit-work/0627/readers`, a scratch folder removed after 0627's merge: the commands above remake it
in about 12 min): 332 cases, 327 passed, 5 failed, 58 min. There was no product fault:
- `test_join_ring.py`, 4 cases (`test_server_ring_to_composite_ring[fence]` and `[cpu]`,
  `test_server_ring_composite_v3_no_frames`, `test_server_ring_killed_composite_goes_on`): a stale expectation. The
  file sets `INI` from test_lockstep without 0490's `EXE_TAPS_OFF`. The stand-in's fake EXE then makes `water_tap`
  log `FAILED REFUSED ... DISABLED`, and `LsRun.finish()` refuses every DISABLED line. 0490 (2026-11-17) fixed
  test_lockstep, but its run left out the reader-dependent files, so this file kept the fault. Red since `water_tap`
  became 1 by default (3608, 2026-11-14) [I: from 0490's account, not rebuilt at that commit]. Fixed in 0627 (MC #8911): `JR_TAPS = dict(EXE_TAPS_OFF, tap="1")` in the three `set_ini` lines. The real-EXE taps are off, and the file's synth tap stays on. Rerun alone (00:20-00:23): `[fence]` and `[cpu]` pass. `v3_no_frames` and `killed_composite_goes_on` still fail on a SECOND cause, named here and not chased (MC #8911): the host exit code is None because `LsRun.close_fn` is None in this file. 0494 (2026-11-17) made LsRun close its host through `close_fn` (conftest's `host_closer`) and set it only in test_lockstep's `lrun` fixture (`r.close_fn = host_closer`), so test_join_ring's `jrun` fixture never sets it (thread exception "'NoneType' object is not callable" in `close_host`). Draft NEW-0627-4, **fixed in 0632
  (2026-11-21) [L]**: the cause was confirmed from the code first (`LsRun.close_fn` defaults to None and `close_host`
  calls `self.close_fn(host.pid)`; `jrun` took no `host_closer`). `jrun` now takes conftest's `host_closer` and sets
  `r.close_fn = host_closer` as `lrun` does. The file ran alone (about 01:04-01:06 EST, through `quietlock.py run`, with
  0627's `shell-build-s` built from the squash fc453ae6f, whose non-doc tree equals main 1ebd32ca2, and the render
  test build `readers/rt/bin`): **4 passed, 0 skipped, 92 s**. test_lockstep.py alone on the same builds plus
  `C5_FRAME_BUILD` (0627's `frame-build`; without it its 10 ring-producer cases error on the missing input, not red):
  **37 passed, 234 s** (01:10-01:14 EST). **The pattern twice in one file:** 0490 and 0494 each fixed test_lockstep and ran only the files they knew of, and the sibling test_join_ring kept both faults (4 and 3 days). That is the argument for a runner that runs every file that imports a changed test module, and for lead-0630's area-to-checks map.
- `test_shadow_tap.py::test_description_change_remakes_slots_never_mixed`: load-sensitive. Under load every quality
  was 2, because the reader started after the 3-s description change. Alone (00:20): 1 passed.

## 7. The killed-server row rule of `test_timeout_ms_limited_and_killed_server` (ticket 0729, 2026-11-25)

### 7.1 Why 0507's run 2 failed [L from the logs; two causes behind it stay [I]]

Source: `shell/evidence/0507/fail_run2/` (`composite_rows.txt`, `host.log`, `server.log`, `pytest_failure.txt`), run 2 of
0507's take series, 2026-11-24 18:25 EST, main 9bda43beb, under the quiet lock with 0 other heavy jobs (CPU mean 28.5 %).
Case `[4000000000-50-...]`: the ini's timeout is clamped to 50 ms (`CONFIG ERROR ... clamped to 50`, `timeout=50`).

| Time (composite log) | Row / line | Read from it |
|---|---|---|
| 18:25:05.426 | row before the kill: 31 frames, ft_max 17.1 | 60 Hz server, every frame takes its server frame |
| (about 05.923) | the kill | 62 ms before the next row's end (that row's one slow frame), and 1000 ms before the STALE line |
| 18:25:05.987 | the kill's row (`rows[0]`): 31 frames, ft_mean 18.12, ft_max 62.17, comp_max 62.08 | one frame waited a whole timeout: timeout 1 |
| 18:25:06.578 | `rows[1]`, el=591 ms: **6 frames**, ft_mean 98.553 (591.3 ms), ft_max **279.302**; comp_n 6, comp_mean 62.44 (374.6 ms), comp_max 63.96; acq_n 6, acq_mean 62.37 | timeouts 2-7. 216.7 ms of the row is outside the composite. The other 5 frames add up to 312.0 ms (62.4 ms each, their composite time), so all of it is in the one 279.3-ms frame: **about 215 ms of that frame was outside the composite** |
| 18:25:06.641 | `AcquireSync timed out 8 times in a row (timeout_ms=50)` | timeout 8, about 716 ms after the kill: 8 x 62.4 ms (499 ms) + 217 ms |
| 18:25:06.923 | `server STALE (heartbeat age 1000 ms)` | the default `stale_ms` 1000 |
| 18:25:07.078 | `rows[2]`: 8,216 frames, acq_n 1, acq_mean 61.53 | the 8th timeout fell into this row; full rate again |
| 07.579-10.079 | `rows[3..8]`: 9,926-10,831 frames, acq_n 0 | full rate, no wait |

test_host's own 1-s line for 18:25:06.299-07.298 has max 277.048 ms: the host measured the same long frame.

So the composite did what it must (8 clamped waits, the give-up line, full rate after it), and the test's rule failed:
`full_rate(rows[1:])` wants `rows[1]` above 20 frames, and `rows[1]` held 6. Without the long frame the give-up would
have come about 06.42, inside `rows[1]`, with about 60 ms of full-rate frames (over a thousand) after it, and the rule
would have passed. The margin of the old rule was the time between the give-up and the end of `rows[1]`: about 0.5 s
minus 8 x 62.4 ms plus however early in `rows[0]` the kill fell. Here it was about 60 ms, and the long frame was longer.

Still [I]: (a) why each 50-ms wait took 62.4 ms. It is 4 ticks of the default 15.625-ms timer. `shell_server` calls
`timeBeginPeriod(1)` (`shell/server/shell_server.cpp`, `[C]`), and the guess is that its death gives the default tick back on
a machine where nothing else asks for 1 ms. Under load other processes keep 1 ms, the waits take 50 ms (400 ms for 8), and
the margin is larger, which fits the failure appearing under the quiet lock and not in 0494's loaded series. Against this
guess: 0729's smoke run (2026-11-25 07:16, 3 other owners' heavy jobs running, no lock) also took 61.7 ms per timeout
(`acq_n=8 acq_mean=61.7043`, all 8 in the kill's own row, give-up at 07:16:01.085) [L]. So the wait is not shortened by
load either, and why the 8 waits end on a 62.5-ms grid is open. (b) What the
~215 ms outside the composite were. They are in the host's frame but not in the composite's measured part (`comp`). One
candidate is the driver's clean-up of the killed server's shared texture. Not chased: the test must pass whatever stalls the host.

### 7.2 The rule since 0729 [C: `shell/tests/test_verification_defects.py`, `killed_server_rule`]

From the composite log's lines after the kill (the kill's row first):
- clamped to 0 (`-1`, `0`, `abc`): no frame waits. There is no give-up line, and every row after the kill's row is at full rate (as before).
- clamped to 50 (`4000000000`): exactly one line `AcquireSync timed out 8 times in a row (timeout_ms=50)`. It falls into the
  kill's row or one of the 3 rows after it (`GIVEUP_LAST_ROW`; nominally `rows[1]` or `rows[2]`, the third allows a host
  frame of up to about 0.5 s more). Every row after the row it fell into has `acq_mean` under 5 ms (no frame waits) and is
  at full rate (`full_rate`: every row above 20 frames, the median above 50), and there are at least 5 such rows. A wait that is
  not clamped never returns. Waits that are not counted never give up. Waits that go on after the give-up show in
  `acq_mean` or the frame counts. Each of these fails the rule.
- The test now sets `stale_ms=5000`, as `test_timeouts_in_a_row_stop_the_wait` does. With the default 1000 a host frame of about 300 ms more would let
  STALE end the waits before the 8th timeout, and the give-up line, which the rule requires, would not come. The STALE path
  is not what this test is about.
- The rule reads the lines taken right after the 9 rows are there, before `host_closer`, so a short last row at the
  host's close is never counted.

Unit cases (no build, under a second): `test_killed_server_rule_replays_0507_run2` reads `fail_run2/composite_rows.txt` from
the line after the 18:25:05.426 row. It checks that the old rule fails on it and the new one passes, with the give-up in
`rows[2]` and the 6 rows after it checked. `test_killed_server_rule_cases` has 3 passing logs, one with a 290-ms frame that pushes the give-up to
`rows[3]`, and 10 that must fail. A mutation check (2026-11-25): with `GIVEUP_LAST_ROW` 99 the "too late" case passes and
the test fails; with the floors at -1 the "slow row" case passes and the test fails.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -X utf8 -m pytest -q -p no:cacheprovider shell/tests/test_verification_defects.py -k killed_server_rule

### 7.3 The 10-run series under the quiet lock

Builds 2026-11-25 07:12-07:15 from the branch (`shell/evidence/0729/build_0729.sh`). Series script
`shell/evidence/0729/series_0729.sh` (runbook 2.2). A smoke run of the four cases first (07:16, no take): 4 passed.

**Series 1 (07:17-07:35), stopped after run 1 on `--stop-on-fail` [L].** The take waited 07:17-07:25 for account B's
builders. Run 1 ran 07:25-07:35 with 0 heavy jobs and CPU mean 29.0 %: 88 passed, 1 failed. 0729's test passed all four cases. The failure was
`test_lockstep.py::test_exact_hung_server_takes_the_failure_route_once[alive]` (0691's negative control): no
`lockstep: exact: recovered` line after the hung end. From the logs (`shell/evidence/0729/series1/`):
- After the 6-s stall the stand-in `lockstep_server` drew only about 64 frames a second (`1s: drawn 64`, max draw 15.4-16.6
  ms) in both parameters. Before the stall it drew 540 a second at 0.5 ms.
- In `[alive]` the host was no longer waiting after the hung end and ran at about 570 fps, so its frame was never ready at the
  insertion. The recovery is checked once per insertion (`newest >= want`). The one frame after the stall that came exact
  (CSV row 2531: pub_f 2532, taken_f 2532, kind 0, wait_off 1) was published between the check and the take.
- In `[stopped]` the first frame after the stall was ready at its insertion (`recovered: frame 2582`, 07:27:04.703), and from then
  on the host waited for each frame.
- Run alone three times right after (07:38-07:40, through `quietlock.py run`): both parameters passed, and after the stall
  the stand-in drew 530-549 frames a second again.
- [I]: why the stand-in's draws took about one 15.6-ms tick at 07:27 is not known. Candidates are GPU time sharing with another
  process, or power throttling of the windowless stand-in. `lockstep_server.cpp` calls neither `timeBeginPeriod` nor the
  power-throttling opt-out that `shell_server` has (bug 0118).
- The MC's ruling (#14428, 2026-11-25 07:41): `[alive]` stays selected; a run counts as 0729's pass when 0729's test passes;
  any `[alive]` race is listed separately with its CSV row. A draft ticket for the race (NEW-0729-1) goes in the hand-back.

**Series 2 (07:46-09:07) [L].** Run 1 (08:07-08:17) and run 2 (08:54-09:04): 89 passed each (0729's four cases and `[alive]`
included), 0 heavy jobs beside either, CPU mean 20.1 % and 27.8 %. The takes waited 07:46-08:07 and 08:27-08:54 (the second refused once at
its 20-min limit). Other owners' heavy jobs never reached 0 for long: account B's chains started a new build or suite as each one ended.
The MC stopped the series at 09:07 (#14525): the rest runs inside 0754's held quiet hour, one take at a time between 0754's arms.
Records: `shell/evidence/0729/series2/` (`series.jsonl`, `attempts.jsonl`).

**Series 3 (0754's quiet hour, 10:03-10:18) [L].** Run 3 took the lock at 10:08 through the quietlock queue, between 0754's
launches p3 and the next. At 10:1x the MC stopped the interleaving (#14650): 0754 runs back to back, and the remaining runs (4-10)
go into a short quiet window of their own that the MC books later. Run 3 (10:08-10:18): 89 passed, 0 heavy jobs beside it, CPU mean 21.9 %. Records: `shell/evidence/0729/series3/`.

**Count so far (2026-11-25 10:18): 3 consecutive passing runs of both files** (series 2 runs 1-2, series 3 run 3), plus series 1 run 1, where 0729's test passed and only `[alive]` failed. So 0729's test passed 4 of 4 runs. The acceptance's 10 consecutive passes are not reached yet. Runs 4-10 are
`sh shell/evidence/0729/series_0729.sh series4 --first 4 --runs 7` with the window's `--gap-min` and `--max-hours` (runbook 2.2).
The builds in `C:/c5-64bit-work/0729/b` are kept for them.

**What the takes cost (2026-11-25) [L].** A `quietlock.py run --take` waits for other owners' heavy jobs to reach 0, for
at most 20 minutes. On a busy day that took 8-27 minutes per run (07:17-07:25, 07:46-08:07, 08:27-08:54), and one attempt was
refused at the limit. Each 10-minute hold also refused account B's rule A starts (w149-0042: about 9 minutes lost at 08:56, and
"the third 0729 hold inside B's rule A line today"). Without a window a 10-run series under the lock does not fit a working day
beside the other accounts' chains. Book it as a short quiet window of its own from the start. Inside a window shared with another
holder, `quietlock.py queue --who <id>` makes the takes alternate. The head of the queue takes next: 0729's run 3 took at 10:08 as
head while 0754 was taking launch after launch. The 10-min gap after a hold applies while someone else is queued (the tool's
docstring; not tested here).
