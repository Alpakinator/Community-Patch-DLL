# shell/: the shell side of the 64-bit patch (composite DLL, test server, stand-in)

Origin (rule 13, decision D22): this folder starts as the Gate 3 compositor prototype from the project's
fork of Community-Patch-DLL, branch `claude/shell-gates-d8e422`, `experiments/shell-gates/compositor/` and
`experiments/shell-gates/tools/`, copied at branch head c60138b82 on 2026-11-10 (issue 3001). Each copied file names
its origin path and the commit it last changed in. The sections "Design" and "Results (test host)" below are that
prototype's README (last changed in a9d8328e2), kept as written except for paths; the runs, CSVs and screenshots it
cites (`runs/`, `results/`, `shots/`) were not copied and stay on the origin branch. Measured findings live in
`research/05-graphics-composition/`. One statement below is superseded: the loader marker `shell.helper` sits in the
install folder's `crashlogs\`, not in My Games (research/05-graphics-composition/shared-texture-and-compositor.md
section 3).

## Layout

| Path | What |
|---|---|
| `composite/shell_composite.cpp` | `shell_composite.dll` (x86): hooks, insertion logic, state save/restore, stats |
| `composite/vtable_index.c` | vtable slot numbers from the SDK's C declarations (no hard-coded slots) |
| `composite/shell_composite.ini` | DLL config (copied next to the DLL by the build, never overwritten there) |
| `server/shell_server.cpp` | `shell_server.exe` (x64): test server, renders a test scene into a shared NT-handle + keyed-mutex texture |
| `standin/test_host.cpp` | `test_host.exe` (x86): the stand-in for `CivilizationV_DX11.exe` |
| `common/shell_shared.h` | layout of the shared-memory section (x86 = x64, checked by static_asserts); camera block at offset 1024 (issue 3007) |
| `common/exe_camera.h` | issue 3007: the EXE's camera addresses, the byte patterns checked before any read, the consistency checks (DLL and server) |
| `common/shaders.hlsl` | all shaders, compiled offline by fxc (shader model 4.0) |
| `third_party/minhook/` | MinHook v1.3.4 source (issue 3006; BSD-2-Clause, see its NOTICE) |
| `build.cmd` | the one build command |
| `tests/` | pytest stand-in tests, everything on a hidden desktop (`test_errors.py`: the negative tests of issue 3013) |
| `tests/lockstep_server.cpp` | issue 3304: `lockstep_server.exe` (x64, test only), a version-3 server whose picture names the camera it drew (colour = frame id, depth inside `--region`); slow, stalled (ticket 0691: `--stall-heartbeat-stop`, no heartbeat during the stall), holding a frame for N EXE presents with the hold reported (ticket 0691: `--hold-for-exe N,M`, `--hold-on PATH`, `--hold-unreported`), killed on request (`tests/test_lockstep.py`) |
| `tools/memcap.py` | issue 3013: run a command with a memory cap on its whole process tree (job object); used for the ASan run |
| `tools/*.ps1` | the prototype's stand-in scripts (run_pair, ab, soak, stats_summary, ab_table, shot); the three that start `test_host.exe` refuse to run without the MC's permission (rule 14), use `tests/` instead |
| `tools/ab_live.py`, `scan_exec.py` | live-game tools of the Gate 3 run (A/B of ini modes; scan of `insert=exec` indices) |
| `tools/view_mask.py`, `addrspace.py` | live-game tools: the EXE's per-view systems mask; address-space snapshot of a process (read-only) |
| `tools/live3008.py`, `server_keeper.py`, `trace_lists.py`, `listshot_diff.py`, `grid_accuracy.py`, `pan_offset.py`, `sheet.py` | issues 3005/3008 (2026-11-11): live driver of the composite DLL in a harness-launched game (ini switches, `.trace`/`.shot`/`.listshot` triggers, views, camera read, posted-move picking check, main-thread one-shot Lua, minimise/restore without activation); the server kept on a hidden desktop; trace and listshot tables; grid error against the EXE's hex outlines; contact sheets. Evidence `evidence/3000/live/` |
| `tools/ab_frames.py`, `turns_soak.py`, `turns_explain.py`, `mapchange.py`, `stock_acc.py`, `outline_to_grid.py`, `shift_match.py` | issue 3008 second half (2026-11-11, W7): frame cost composite vs passthrough in interleaved windows with statistics (run inside `quietlock.py run --take`); N AI turns and a soak with per-turn missed insertions, memory and handles; missed insertions matched to the 1116 handler's leader screens; exit to the menu / load on the main thread (the load does not work: the channel is silent in the front end); grid vs the EXE's own outline in the stock configuration (`insert=present`) and the key-pan lag; occlusion-free outline-to-grid distance; grid-vs-highlight shift in one shot by correlation. Evidence `evidence/3000/live/3008_w7_*` |
| `tools/gt_reader.py`, `gt_validate.py`, `i2_capture.py`, `i2_tiny_setup.lua` | issues 3003/3004 (2026-11-11): read-only reader of the EXE's camera, grid constants and heightmap by PID; validation against HexToWorld and hex highlights; the in-game capture driver of the road question; the Tiny test setup. Evidence `evidence/3000/road/` |

## Build

From the repository root, in any shell:

```
cmd /c shell\build.cmd
```

Epic 3200 (2026-11-11, decision D58 point 4): the build first runs `render/build.py --target c5render` into
`<output>\render` (gated: exit 3 while another team holds the quiet-machine lock), and `shell_server.exe` links that
static library and builds with the dynamic CRT `/MD`; the composite DLL (now with `composite/bake_tap.cpp`) and
`test_host.exe` keep `/MT`. `C5_SHELL_BUILD` is required (MC order 2026-11-11 14:08, after a build overwrote the formerly
shared default `D:\c5-64bit-work\3000\shell-build`, which is untrusted until rebuilt from main): without it the build
refuses with exit 2, and the tests (`tests/shellrun.py`) refuse to guess a folder. The section layout
is version 3 (`common/shell_shared.h`: tap block at 2048, bake-tap block at 2560, depth block at 3072).

Epic 3200 additions: `shell_server.exe --scene terrain --exe EXE` (the terrain with `c5r::terrain`; options `--tap-file`,
`--save-tap`, `--tile-plots`, `--tile-d4`, `--color`, `--fallback`, `--constant-rgba`, `--pass`, `--tess`, `--water`,
`--depth`, `--overlay-grid`; the full list and exit codes are in the comment at the top of `server/shell_server.cpp`;
design in `research/05-graphics-composition/shared-texture-and-compositor.md` 13, live results 14), `--scene
bakefake` (tests' cache producer); composite ini keys `tap` (height-field tap, section 11) and `bake_tap` (tile-cache
tap, `terrain-rendering-inventory.md` 5); tools `tools/terrain_live.py` (live driver), `terrain_compare.py`
(geometry by the EXE's hex highlight, colour on the cached mask), `tapcheck.py`, `tap_live.py`, `tap_depth.py`,
`bake_reader.py`, `bake_probe.py`, `bake_coverage.py`, `bake_lookat.py`, `bake_instance.py`; tests `test_tap.py`,
`test_bake_tap.py`, `test_terrain_scene.py`, `test_build_cmd.py`.

Builds `shell_server.exe` (x64), `shell_composite.dll` and `test_host.exe` (x86, static CRT `/MT`) with VS 2022
Community (`VSDIR` overridable) and Windows SDK 10.0.26100.0 fxc (`SDKVER` overridable). Output folder (rule 15):
`%C5_SHELL_BUILD%`, required since 2026-11-11 (it was: else `D:\c5-64bit-work\3000\shell-build` or `shell\build`);
binaries in its `bin\`. Nothing built is committed.

`cmd /c shell\build.cmd asan` (issue 3013) builds the same programs and MinHook with MSVC `/fsanitize=address` into
`D:\c5-64bit-work\3000\shell-build-asan` (or `%C5_SHELL_BUILD%`, or `shell\build-asan`): dynamic CRT (`/MD`; the host
and the composite DLL share one ASan runtime, so both are instrumented), no `/SAFESEH`, and
`clang_rt.asan_dynamic-i386.dll` / `-x86_64.dll` copied from the MSVC toolset into its `bin\`. A test variant only:
never load it into the game. Run the suite against it with `C5_SHELL_BUILD` pointing at that folder,
`ASAN_OPTIONS=log_path="D:/.../asan"` (quoted: the colon of the drive separates ASan options) and `C5_ASAN_LOG_DIR`
set to the same folder: an ASan report there fails the test that was running (`tests/conftest.py`; test_host.exe has
no stderr). Start it through `quietlock.py run` with a memory cap: `python shell/tools/memcap.py --mb 6144 -- python -m
pytest shell/tests -v` (a job object over the whole process tree).

## Tests (stand-in, hidden desktop)

```
set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
python -m pytest shell/tests -q -p no:cacheprovider
```

Python 3.11, run without `-I` (the tests import `tools/harness/tests/desktops.py`). Every process starts on a desktop
the test session creates (`HiddenDesktop`); `tests/shellrun.py` `launch()` refuses the interactive desktop through the
harness's `check_desktop` unless the MC's permission variable is set. Each test copies the DLL and ini into its own
temporary folder and uses its own section name, so a running server of another team is never touched. Never point
these at the game.

`tests/test_standin.py` (3001): the prototype's path (immediate-context clear). `tests/test_tagged.py` (3006): the
game-like stand-in (`test_host --game --vary 1 --mt 1`), every tag rule, the fixed exec index as a counter-example,
a rule that matches nothing, no server, a killed server, every kill switch and mode=off (read back from the host's
memory), swap-chain adoption at creation. `tests/test_camera.py` (3007): the camera read on `test_host --fake-exe`
(read path, map wrap, kill switch, stale publisher, the server's own check, frozen globals, Strategic View mode, a
writer on another thread, refused EXEs) and two static checks (the patterns against the EXE file on disk, the camera
block's offsets against the server). `tests/test_camera_offline.py` (3007): the server's grid rendered with the
ground-truth cameras of `D:/c5-64bit-work/i2-ground-truth` against the EXE's own hex outlines (pixel error; skipped
without the ground truth). `tools/measure_tagged.py`: CPU per frame and missed / misplaced insertions,
tagged vs exec (waits for the quiet-machine lock). `tests/test_errors.py` (3013): the negative tests of the table below.
`tests/test_verification_defects.py` (one-off 9035, 2026-11-13): the negative tests of the defects found by the
verification 9031 (bugs 3014-3018), also in the table below ("verification_defects").

A run with a skipped test counts as failed (COMMON rule 9): `tests/conftest.py` turns the exit status into 1 when
anything was skipped (the EXE on disk or the ground truth missing), unless `C5_SHELL_ALLOW_SKIP=1` is set for a run
that knowingly lacks them.

**Timing checks on a shared machine (ticket 0494, 2026-11-17).** The suite runs beside other teams' builds, suites and
games, so a check on wall-clock time states what it checks as a bound with margin, never a hard deadline of a few ms.
Measured on the stand-in in 0494's series (5 to 10 heavy jobs beside each run, CPU mean 57-91 %) [L]:

- *Lockstep waits* (`tests/test_lockstep.py`, `check_late`). The contract: a wait ends within 0.5 ms of its deadline
  (`LkBoundedWait`: 0.25-ms timer slices, then a spin over the last 0.5 ms). Where every wait runs to its deadline (the
  killed server) about half of them end 0.37-0.50 ms past it, the last timer slice overshooting into the spin window
  (cause inferred): the contract has no margin there. Load makes waits late in episodes, when the render thread does not
  run: up to 30 of a test's 248 waits (12 %) past 1 ms, the largest 118.9 ms (9 heavy jobs, CPU up to 100 %). So a wait
  counts as late past 0.5 + 0.5 ms (`LATE_MS`, `LATE_SLACK_MS`), at most half of a test's waits may be late (`LATE_SHARE`,
  at least 3: `LATE_FLOOR`), and none may end a second past (`LATE_FAR_MS`, a stuck wait). The same rule covers the slow
  server's wait lengths (bound 3.4 ms) and the late-insertion overruns (whose counter must still equal the records at
  0.5 ms). A wait that ignores or misplaces its deadline still fails: it is late in every wait of the slow-server and
  late-insertion cases. Not caught here: a defect that makes only some waits late (GetData in every spin, 2026-11-13:
  14 of 183 waits up to 49 ms) looks like load on this machine; a claim about the wait's tail needs a quiet machine
  (COMMON 23). Replaced: "1 % past 0.5 ms, none past 2 ms" (0490: 4 of 74 waits up to 3.0 ms, one 2.15 ms); this series
  also broke 10 % / 25 ms and 10 % / 100 ms. The exact-frame floor (`check_misses_justified`) is 5 % (was 80 %): under load the
  share fell to 43 % (6 heavy jobs) and 19.8 % (13 heavy jobs, CPU 99 %) with every miss justified; what the floor
  guards against, a composite that waits but never takes its own frame, is near 0. The justification rule itself (at
  most max(2, 1 %) of the frames neither exact nor waited for) is unchanged. The slow-server give-up is checked on the rows (never more than 8
  waited misses in a row, exactly 8 before each give-up) instead of "waits <= 8 x (recoveries + 1)": during host stalls
  a frame can be ready without a wait and end a miss run early (39 waits for 4 give-ups and 3 recoveries).
- *Start-up.* The composite's hook commit (in the host's first frames after the load, normally 0.1-1 s) took up to
  10.7 s under load, and the UpdateViews hook's enable (a second commit, which freezes the host) came up to 5.4 s after
  its creation (research 05 hooks-overlays-and-ceg.md); test_host counts `--seconds` from its own start. So
  `LsRun.start_host` waits for `hooks committed (` and, with lockstep on, for the `lsprobe:` enable or refusal, and closes
  the host `seconds` after them (`conftest.py`'s `host_closer`: WM_CLOSE, test_host's Esc path, exit 0), 36 s more on
  `--seconds` as a backstop, and later while a screenshot is still awaited (the composite's control thread once took the shot trigger more than 2.4 s late). Events the tests time
  themselves after that: the stalled server's stall (`lockstep_server --stall-on FILE`, 1.5 s after `lsprobe: hook
  ENABLED`; a stall at 3.0 s on the server's clock, or after 600 drawn frames, came before the first lockstep frames),
  the depth dump (`test_host --depth-dump-on FILE`, 3 s after the commit), and the ring hand-over test's end (1.5 s after
  the host's second `--churn` resize, so its last 100 rows follow the last resize); that test's screenshot is taken again
  (at most twice) when it shows the host's own gray right after a resize. The test servers (`lockstep_server`,
  `ring_producer`, `shell_server`) live 120 s on their own clocks (`SERVER_S`) and are ended by the fixture: a 25-s
  `ring_producer` ran out during an 18-s host freeze.
- *Frame rate* (`tests/test_verification_defects.py`, the two timeout tests). A frame that waits `timeout_ms` (50 at most)
  allows at most 10 frames per 0.5 s stats row, so every row must exceed 20 and the median 50 (was: every row 50); the
  test awaits the hook commit (up to 18 s; a 9.7-s commit outlasted the 10-s wait for the config line) and the rows
  after the kill, then closes the host with `host_closer` (an 8-s host after a slow start-up ended one row after the
  kill). After the give-up no frame waits: every later row's mean acquire time is under 5 ms (was "at
  most 9 timeouts", which counts the immediate `AcquireSync(1, 0)` failures when the suspension lands between the
  server's `frameCounter` increment and its `ReleaseSync`: 3,863 in one row at 0.05 ms each).
- *Limits, 2026-11-17.* No series of 10 runs on the final code passed whole: during the last series the workstation
  carried 12-14 heavy jobs at 90-99 % CPU (twice the 6 of the ticket's profile), and there the host froze for up to
  18 s and the ring tail took 2 of its last 100 frames exact (every miss justified). The pass counts per series and the
  rule changes between them are in tracker issue 0494's log (all 62 runs of its ten series, under rules that changed
  between series: 48 passed; 31 of 38 at 6-10 heavy jobs, 17 of 24 at 11-14).
- *Under the quiet lock, 2026-11-24 (ticket 0507, main 9bda43beb) [L].* `test_lockstep.py` + `test_verification_defects.py`
  (87 tests), each run its own `quietlock.py run --take` with 0 heavy jobs beside it (CPU mean 28.5-33.4 %): **2 of 3
  passed**; the series was stopped after run 3 on the failure (and for account B's lock window). No lockstep wait ended
  past 0.5 ms in the 3 runs (under 13-14 heavy jobs on 2026-11-17: 1-2 per test). Run 2 failed
  `test_timeout_ms_limited_and_killed_server[4000000000]` (the frame-rate floor after the kill): the composite's 8
  timeouts before the give-up lasted ~62.4 ms each (default 15.6-ms timer once the killed server's 1-ms period is gone
  [I]), about one whole 0.5-s row, and one ~215-ms host frame outside the composite stretched them over the whole row
  after the kill's row (6 frames, ft_mean 98.6 ms). Cause and fix route: research 06
  [test-health-on-main.md](../research/06-tooling-and-workflows/test-health-on-main.md) section 5; evidence
  `evidence/0507/` (`take_series.jsonl`, `take_summary.txt`, `fail_run2/`). **So the quiet lock does not make these
  tests reliable, and they need not run under it:** their lockstep checks pass under load since 0494, and the one
  failure under the lock comes from that test's row rule, which a quiet machine makes more likely, not less [I: through the 15.6-ms tick].
  (Earlier, 2026-11-17 on branch task-0507-confirm-0494-timing: 3 of 3 plain runs beside 13-14 heavy jobs passed, 70
  tests; `evidence/0507/plain_summary.txt`.)
- Other waits in the suite (`wait_log(timeout_s=...)`, `host.wait(...)`) are liveness limits of seconds, not
  measurements. `C5_SHELL_TIMING_LOG=<file>` makes `test_lockstep.py` write one JSON line per timing check (count, past
  0.5 ms, late, largest values, exact share). The series runner: `evidence/0494/series_0494.py` (each run one registered
  job through `quietlock.py run`; load jobs added up to 6 heavy jobs beside it).

## Lockstep, hand-over and depth in the composite (issue 3304, epic 3300, 2026-11-13)

Contract: `common/vp_frame.h` (publish block, events, ring) and `frame/frame_source.h`; design and results:
`research/05-graphics-composition/renderer-in-server-design.md` sections 3, 4.3 and 3.1 (as built). Since ticket 0490
(2026-11-17) the built-in defaults are the shipped ini's (`lockstep=1`, `handover=ring`, `depth_write=1`, the product
path; before: all off); with `lockstep=0 handover=v3 depth_write=0` the composite runs the version-3 path exactly as before
(the test fixtures set those).

| ini key | Default (code = shipped ini, 0490) | Meaning |
|---|---|---|
| `lockstep` | 1 (was 0) | 1 = the camera is published at UpdateViews (the 9039 hook on `d3dx9_42!D3DXMatrixInverse`, UpdateViews' own call into `&g_InvView[0]`) with frame id F, and the insertion waits for F |
| `lockstep_mode` | `exact` (ticket 0691, D242, 2026-11-23) | `exact`: the insertion waits for F with no frame-time bound and no give-up; only a dead or hung server ends the wait (below). `bounded`: `lockstep_bound_us` and `lockstep_giveup` as before, for tests and A/B runs only. Not a word = `exact`, CONFIG ERROR |
| `lockstep_hung_ms` | 30000 | `exact`: the hung limit, 2000..120000 (clamped / default, CONFIG ERROR): the server's heartbeat older than this, or no new frame of any id for this long while the heartbeat ticks |
| `hung_sink_test` | 0 | tests only: with no proxy loaded, the hung end's call to the proxy is logged (`hung_sink_test: ServerHung("...")`) |
| `lockstep_bound_us` | 8000 (was 3400; ticket 0675, 2026-11-22) | `bounded` only: the wait's deadline after the publish, 0..10000 (0 = never wait); out of range clamped, not a number = default, both logged as CONFIG ERROR |
| `lockstep_giveup` | 8 | `bounded` only: misses in a row before the composite stops waiting (until a frame is ready at the insertion without a wait), 1..1000 |
| `handover` | `ring` (was `v3`) | `v3`: keyed mutex + copy (the server must write the depth block's `camFrameStamp`: `shell_server.exe --depth 1`); `ring`: issue 3302's ring source (`frame/ring_source.cpp`, linked in), `ring_slots` 2..4 (4), `ring_sync` fence/cpu (fence) |
| `depth_write` | 1 (was 0) | 1 = the server's depth of the drawn frame (R32F NDC z) into the depth view the scene-clear list cleared, in the colour pass (`PSCompositeDepth`: SV_Depth, func ALWAYS, write on, stencil off); `tag_rule=after` only |
| `lockstep_csv` | 0 | 1 = one row per Present in `shell_composite.lockstep.csv` in the output folder (`VPSHELL_OUT_DIR`, else next to the DLL; issue 0148) (frame, published F and its source, publish / deadline / insertion / wait QPC, taken id, kind exact/older/none/nopub/noins, age, slot, wait_off, depth, camera compare, the server's times, camera position) |
| `lockstep_check` | 0 | 1 = premise 3: the camera globals read again at Present and compared with the UpdateViews read per field (`cmp` in the rows, `cmp_n`/`cmp_diff` in the 1 s line) |

- **Publish.** F = max(previous F + 1, presentCount + 1): it only grows, and frameStamp stays the presentCount of the
  Present that ends the frame (shell_server's stale rule unchanged). Frames without an UpdateViews publish (hook refused,
  Strategic View, front end, UpdateViews on another thread, an exception in the publish) publish at Present
  (`VPPUB_PRESENT`, no wait). The camera block is written first, then the publish block, then `<base>.Cam`.
- **Wait.** `<base>.Frm` (if the server sets it) plus a high-resolution waitable timer in slices of at most 0.25 ms (so a
  server that never sets the event, like `shell_server.exe`, is polled) and a spin over the last 0.5 ms; never past the
  deadline. A miss draws the newest older frame (age counted). A `lockstep 1s:` line per stats interval sums it all up.
- **Exact wait (ticket 0691, D242; `lockstep_mode=exact`, the default).** The same event + 0.25 ms timer slices with no
  deadline; every 10 ms the server's liveness: dead = its process ended (`OpenProcess(SYNCHRONIZE)`), another pid in the
  section, ring state FAILED / STOPPED, or magic 0 (the wait ends at once, logged once per pid, `dead_ends`); hung = its
  heartbeat (ring: `VPFrameServer.heartbeatQpc`; v3: the v3 header's) older than `lockstep_hung_ms`, or no new frame of
  any id for that long while the heartbeat is younger than 1 s. A hung end: one `LOUD: lockstep: the server is HUNG:
  <reason>` line, `hungEnds` in the publish block, `hung_ends` in the 1 s line, no wait (`wait_off`) until a frame is ready
  at the insertion ("exact: recovered"), and once per process the proxy's sink (`VPShell_SetHungSink`, exported; the
  proxy passes it at OnBoot; called on the control thread) which takes the D118/D240 failure route (stage `server-hung`).
  The publish block then says `boundUs` 0 and `deadlineQpc` = publish + `lockstep_hung_ms` (the server's Busy retry).
  No message pump in the wait: a wait over 5 s shows Windows' "Not Responding" mark until the frame ends (a
  `PeekMessageW` pump was tried and refused: it did not prevent the mark and ran a cross-thread `SendMessage` mid-frame;
  `research/05-graphics-composition/renderer-in-server-design.md` 3.2).
- **Held frames (ticket 0691, MC #11169).** The server reports a frame it HOLDS for inputs later EXE frames bring (its
  fog at a load: the feed's plot table, the overlay pair) in `VPFrameServer.heldFrameId` / `holdReasons` (vp_frame.h
  `VPHOLD_*`). Such a frame is not waited for (checked before and during the wait): no server world in it (as at a load;
  never an older one), CSV kind 5 `held`, one `LOUD: lockstep: held_skip: ...` line each with the reason bits, `held_skip`
  in the 1 s line, `heldSkips` in the publish block. A run of held frames longer than `lockstep_hung_ms` is a fault: one
  `LOUD: lockstep: the server's HOLD is STUCK: ...` line, `held_faults`, the proxy's sink once (the hung route's call, its
  own reason text); after it the held_skip lines come once per second. Normal play after the first world frame: 0.
- **Sources.** `V3Source` (in `shell_composite.cpp`) wraps today's `AcquireAndCopy`: `newest()` is the depth block's
  `camFrameStamp` when its `frameCounter` equals the frame block's; the depth texture is copied in the same keyed-mutex
  cycle into a local R32F texture. `RingSource`: opened and sized (`request()`) before the first publish, `close()`d in
  `ReleaseAll` (so before `ResizeBuffers`), re-opened and re-sized at the next frame.
- **Depth.** The DSV is the one the scene-clear list cleared (`ClearDepthStencilView` with the depth flag, recorded per
  list); used only inside that list's `ExecuteCommandList` (the list holds it), checked once per view and target size
  (2D, single-sample, writable, the target's size; else logged and counted `depth_refused`).

Stand-in seams (test only): `test_host --fake-uv` (UpdateViews' call site, import thunk and import slot laid out in the
fake image, called once per frame; `--fake-uv-every N`, `--uv-work-us N`, `--fake-cam-move-after-uv`,
`--fake-uv-thread`), `test_host --depth-dump PATH --depth-dump-at S` (the D24S8 buffer after the frame's lists),
`fault_test=uv`, and `bin\lockstep_server.exe`. Tests: `tests/test_lockstep.py` (needs `C5_FRAME_BUILD` = an output of
`frame\build.cmd` for the ring cases).

## Error paths and their tests

Issue 3013 (2026-11-11). Every validation and failure path of the native code and the Python tools in `shell/`, and the
test that provokes it and checks the outputs and the exit code after it. "test_errors" = `tests/test_errors.py`,
"camera" = `tests/test_camera.py`, "tagged" = `tests/test_tagged.py`, "standin" = `tests/test_standin.py`. Test seams
that exist only for these tests: test_host `--fake-exe-bad`, `--fake-cam-bad`, `--format rgb10a2`; the DLL's ini keys
`fault_test`, `hook_test` (and the older `camera_test`); shell_server `--test-abandon`; sections written from Python
(`tests/camblock.py`).

**shell_composite.dll**

| Path | Behaviour | Test |
|---|---|---|
| EXE TimeDateStamp is not 0x546CD0A8 | REFUSED, never read | camera `test_refused_exe_never_read[tds]`, `[real-host]` |
| a byte pattern differs | REFUSED, never read | camera `test_refused_exe_never_read[sig]` |
| no MZ / e_lfanew past 4 KB | REFUSED | test_errors `test_refused_image[mz]`, `[pe]` |
| module base 0 (`VPSHELL_EXE_BASE` 0 or not hex) | REFUSED with the reason (empty before 3013) | test_errors `test_refused_image[base0]` |
| validation itself faults (base in unmapped memory) | caught, REFUSED | test_errors `test_refused_image[unmapped]` |
| a pattern that does not parse | REFUSED | not at run time: the patterns are constants; camera `test_signatures_match_exe_on_disk` expands every one |
| g_Civ5App null / GVS vptr not FCamera / g_NumViews 0 or > 4 / a wrap view not a +-period copy | NO_GVS / BAD_GVS / INCONSISTENT, grid hidden | test_errors `test_camera_bad_state[null-app, bad-vptr, views0, views9, wrap]` |
| NaN or infinity in g_View[0], a wrap view, g_Proj or camWorld (issue 3015, 2026-11-13; before: a NaN was published OK) | INCONSISTENT, never OK; server not OK | `tests/test_verification_defects.py` `test_nonfinite_camera_never_published_ok` (9 cases) |
| grid heightmap not ready | OK camera, gridValid 0, server NO_GRID | test_errors `test_camera_bad_state[grid-not-ready]` |
| view globals frozen, camera moving | INCONSISTENT, server hides after `--bad-frames` | camera `test_frozen_view_globals_inconsistent` |
| view mode 2 (Strategic View) | NOT_MAP | camera `test_strategic_view_mode_not_map` |
| two reads never agree (writer thread) | TORN or retried, never OK when inconsistent | camera `test_writer_on_another_thread` |
| the camera read faults | EXCEPTION, 1 s pause, off for good after 5 | test_errors `test_camera_read_exception` |
| publisher stalls while Presents go on | server STALE (tint / hide) | camera `test_stale_publisher_detected[tint, hide]`; negative control `test_stale_is_not_raised_when_host_idle` |
| view 0 corrupted but status OK | server's own check: SRV_INCONSISTENT | camera `test_corrupt_view_caught_by_server` |
| camera=0 | OFF, grid hidden, live | camera `test_kill_switch_live` |
| hook target outside d3d11.dll / dxgi.dll (host EXE, own module, kernel32, heap) | not hooked, bytes unchanged (D4) | test_errors `test_hook_guard_refuses_foreign_targets` |
| exception inside a hook body (Composite, Present, ExecuteCommandList) | SEH guard, DISABLED, pass-through, host runs on | test_errors `test_exception_in_hook_disables_composite[composite, present, exec]` |
| no server (no section) | STALE, retried once per second, nothing drawn | tagged `test_missing_server_is_reported` |
| section exists, not initialised (magic 0) | STALE (section not initialised) | test_errors `test_section_not_initialised` |
| server exits normally (clears magic) | STALE, last frame kept | test_errors `test_server_clean_exit_marks_stale` |
| server killed (heartbeat stops) | STALE after `stale_ms`; last frame or nothing (`hide_when_stale`) | tagged `test_killed_server_last_frame_or_nothing[0, 1]` |
| OpenSharedResourceByName fails | logged once, retried every 250 ms, nothing drawn | test_errors `test_open_shared_texture_failure` |
| AcquireSync WAIT_TIMEOUT (`timeout_ms` > 0) | counted, previous copy drawn | test_errors `test_acquire_timeouts_counted` |
| `timeout_ms` negative, huge or not a number (issue 3014, 2026-11-13; before: -1 = INFINITE, the presenting thread hung for good at the server's first new texture generation, and 500 with a dead server gave 1 fps) | clamped to 0..50 ms, CONFIG ERROR logged once; no wait while STALE, while the section names another texture, while a resize is pending | verification_defects `test_timeout_ms_limited_and_killed_server[-1, 0, 4000000000, abc]` |
| 8 AcquireSync timeouts in a row (server frozen or dead, heartbeat not yet stale) | no more waits until a frame arrives (zero-timeout path), logged once, counted as `wait_off` | verification_defects `test_timeouts_in_a_row_stop_the_wait` |
| shared texture of another format at the same size (issue 3018 V5) | local texture recreated, logged | verification_defects `test_texture_format_change_at_same_size` (server `--tex-format rgba`) |
| AcquireSync WAIT_ABANDONED (server died holding the mutex) | texture closed, re-opened on the next publish | test_errors `test_abandoned_keyed_mutex` |
| ResizeBuffers on the target | everything released first, new size requested, new generation opened | test_errors `test_resize_releases_and_reopens` |
| bad ini values (mode, insert, nth, tag_rule, stats_ms, timeout_ms) | defaults / limits (timeout_ms: see above) | test_errors `test_bad_ini_values_fall_back` |
| ini missing, unreadable (held without sharing) or without `[composite]` (ticket 0490) | CONFIG ERROR once per state with the path and error, every key not set by `VPSHELL_<KEY>` takes its built-in default (= the shipped ini's), "values used" logged; "readable again" when it comes back | test_config_defaults `test_without_ini_...`, `test_unreadable_ini_...`, `test_no_composite_section_...` |
| the built-in defaults against the shipped ini (ticket 0490) | at load and each re-read: "the ini's values equal the built-in defaults" or the keys that differ, `key=ini (default x)`; then "values used i/N" with every key | test_config_defaults (fails when the ini or a default changes alone) |
| tag_match / tag_anchor with an unknown token or matching nothing | logged, matches nothing, missed insertions counted | tagged `test_wrong_rule_is_caught[dsv_fmt=999, bogus_tag]` |
| tag_match / tag_anchor present but no token after parsing (`" , "`), or a character outside printable ASCII (ini saved as UTF-8) (issue 3016, 2026-11-13; before: matched EVERY list, 55-62 % of insertions outside the window) | matches nothing, CONFIG ERROR logged once per config load; converted without the C locale | verification_defects `test_unusable_tag_rule_matches_nothing[3 cases]` |
| a fixed exec index in a varying frame | insertions outside the window counted | tagged `test_exec_index_is_fragile` |
| kill switches, mode=off | targets restored byte for byte | tagged `test_kill_switches_and_mode_off` |
| target silent for 2 s / a new swap chain | retarget / adoption | tagged `test_swap_chain_adopted_at_creation[0, 1]` (new window, old one destroyed); verification_defects `test_new_chain_on_the_same_window_is_adopted` (the game's case); a new chain at the old chain's address (issue 0141): verification_defects `test_new_chain_at_the_old_chains_address_is_adopted` (test_host `--recreate-same-address N`) |
| swap chains of other windows created through the factory (issue 3017, 2026-11-13; before: the newest chain took the target, and more than 8 others logged at every Present, 2.17 MB in 6 s) | not adopted while the target's window exists (logged per chain); 8 others named once each, the rest counted in one line per 10 s | verification_defects `test_other_windows_swap_chains_do_not_take_the_target[1, 10]` (test_host `--extra-chains`) |
| .shot of an unsupported back-buffer format | refused, logged, no file | test_errors `test_shot_of_unsupported_format` |
| shared texture without a keyed mutex | logged, not opened | **not tested**: the server always creates one; needs a second producer |
| local texture / SRV / state / shader creation fails | DISABLED or not opened | **not tested**: needs a D3D allocation failure (no fault injection in the runtime) |
| AcquireSync returns another error | texture closed | **not tested**: no way to provoke one besides WAIT_ABANDONED |
| MH_Initialize / MH_CreateHook / MH_ApplyQueued fail | inert / not hooked / logged | **not tested**: MinHook fails only on a broken process (heap, unhookable prologue) |
| dummy device and WARP both fail | discovery FAILED, inert | **not tested**: needs a machine without D3D11 |
| swap chain without ID3D11Device (D3D10/12), device without ID3D11Device1 | ignored / warning | **not tested**: the stand-in is D3D11 only; pre-Windows 8 runtime |
| no Present 10 s after the hooks (unhooked path) | a plain "no Present seen yet" line at 10 s; WARNING 10 s after a swap chain was created through the hooked factory, or at 120 s (issue 3018 V9, 2026-11-13: the WARNING at 10 s came at every game launch, the DLL loads 16-30 s before the first Present) | **not tested**: needs a host presenting through another DXGI implementation (live: overlays) |
| more than 128 lists a frame, 128 contexts, 4 implementations per method | frame list full / untagged / not hooked | **not tested**: the stand-in records at most 11 lists on 7 contexts |

**shell_server.exe**

| Path | Behaviour | Test |
|---|---|---|
| unknown option, missing value, malformed or out-of-range value | exit 2 with the reason, nothing started (before 3013: ran with 0 or a default) | test_errors `test_server_refuses_bad_command_line` (13 cases) |
| `--luid` matches no adapter | exit 1 | test_errors `test_server_no_matching_adapter` |
| section cannot be created | exit 1 | test_errors `test_server_bad_section_name` |
| `--dump` / `--print-layout` cannot be written; dump never written (no camera) | exit 1 (before 3013: exit 0) | test_errors `test_server_dump_and_layout_unwritable`, `test_server_grid_dump_without_camera` |
| heightmap missing, wrong sample count, odd byte count | logged, flat z | test_errors `test_server_heightmap_missing_or_wrong_size` |
| heightmap short read (fread < file size) | logged, flat z | **not tested**: needs a read error after a successful open (file shrinking under the reader) |
| `--water-z` default and off (negative) | water raised to 10 (issue 3012, 2026-11-13; was 12) / not raised, pixel-exact | test_errors `test_server_water_z_clamp_and_off` |
| implausible map size (more than 256 x 160 plots since 2026-11-13, issue 3018 V6; was 1024 x 1024), gridValid 0, numViews 0, a non-finite projection, view or camWorld (issue 3015), torn block, stale block | not drawn, state logged | test_errors `test_server_refuses_bad_camera_block` (12 cases) |
| resize request out of range; client adapter LUID differs | ignored and acknowledged; MISMATCH logged | test_errors `test_server_resize_requests_and_luid_mismatch` |
| D3D11CreateDevice, shader, buffer, texture (also at resize), vertex buffer fail | exit 1 / "resize failed, exiting" / not drawn | **not tested**: needs a D3D failure |
| AcquireSync error; client holds the mutex over 50 ms | logged; frame skipped | **not tested**: the composite holds the mutex for one copy only |

**Python tools**

| Path | Behaviour | Test |
|---|---|---|
| `tests/shellrun.py launch()` on the interactive desktop | InteractiveDesktopRefused before anything starts | standin `test_refuses_interactive_desktop` |
| `tools/run_pair.ps1`, `ab.ps1`, `soak.ps1` without the MC's permission | throw, nothing created | test_errors `test_ps1_tools_refuse_without_permission` |
| `tools/gt_reader.py` pid not openable, no EXE module, wrong TimeDateStamp | Refused | test_errors `test_gt_reader_refusals` |
| `tools/gt_reader.py` heightmap not ready | Refused | **not tested**: needs the EXE's memory |
| `tools/memcap.py` no command, cap <= 0; exit code passed through | exit 2 / the command's | test_errors `test_memcap_refuses_bad_arguments` |
| `tools/addrspace.py` no such process | exit 1 with the name | test_errors `test_addrspace_unknown_process` |
| `tests/conftest.py`: an ASan report; a skipped test | the test fails; the run fails | used by every run of the suite (3013 ASan run) |
| live tools (`live3008.py`, `ab_frames.py`, `ab_live.py`, `darkred.py`, `mapchange.py`, `turns_soak.py`, `stock_acc.py`, `view_mask.py`, `i2_capture.py`, `scan_exec.py`): ini key found N times, game pid gone, timeouts, no save | SystemExit with the reason | **not tested offline**: they drive a running game (rule: offline workers never start one) |
| `darkred.py`, `live3008.py`, `stock_acc.py`, `mapchange.py`, `turns_soak.py`, `i2_capture.py` without `-i/--instance` (issue 3018 V7, 2026-11-13: they hard-coded c5h-b) | exit 2 at the command line (darkred: exit 1 "no instance") before any instance is touched | verification_defects `test_live_tools_name_the_instance`, `test_live_tools_refuse_without_instance`, `test_darkred_refuses_without_instance` |
| `build.cmd` without `C5_SHELL_BUILD` (or empty), unknown variant (epic 3200) | exit 2, "refused, nothing built" | `tests/test_build_cmd.py` |
| Height-field tap (DLL, ini `tap`, test seam `tap_test`/`tap_test_load`): hostile grid object, section failure, no ack, newer load, consumer checks (version, truncated, stale, map size, hash) | tap block FAILED with failReason / consumer exit codes | `tests/test_tap.py` (55); list in `research/05-graphics-composition/shared-texture-and-compositor.md` 11 |
| Tile-cache tap (DLL, ini `bake_tap`, test seam `bake_test` = ok/missing/nullview/badvtbl/garbage/hugecount/badtile/nulltr, `bake_manifest`) | tap FAILED or slot absent, logged; reader and coverage tool exit codes | `tests/test_bake_tap.py` (27); `terrain-rendering-inventory.md` 5 |
| `shell_server.exe --scene terrain`: wrong section version, truncated / stale tap, wrong map size, hash mismatch, section missing, missing cache texture, bad manifest, stale camera, bad `--tap-file`, EXE missing, bad arguments | ackResult, counters, log; exit 1/2/4/5 | `tests/test_terrain_scene.py` (59); `shared-texture-and-compositor.md` 13 |
| `tools/terrain_compare.py`, `terrain_live.py` input checks | exit 2-5 | `tests/test_terrain_scene.py` |
| Lockstep (issue 3304): `lockstep_bound_us` negative / too large / not a number, `lockstep_giveup` 0, `handover` unknown, lockstep with `tag_rule=inlist` | CONFIG ERROR line, clamped or default, the bound in force in the `lockstep 1s:` line and the rows; inlist keeps the version-3 path | `tests/test_lockstep.py` |
| Lockstep: no UpdateViews call site (hook refused), UpdateViews on another thread, an exception in the publish (`fault_test=uv`) | camera published at Present, no wait, counted (`pub_present`, `wrong_thread`), logged once; the composite draws on, host exit 0 | `tests/test_lockstep.py` |
| Lockstep: slow server, stalled server, killed server, a server without the depth block | waits end at the deadline (+0.5 ms), the previous picture (older, age), give-up after `lockstep_giveup` waited misses and recovery; ids unknown logged once; depth not written (`depth_nosrc`) | `tests/test_lockstep.py` (`lockstep_mode=bounded`) |
| Exact lockstep (ticket 0691): a 30-ms server, the camera at rest, a 2-s stall, a stall past `lockstep_hung_ms` with the heartbeat stopped and with it ticking, a 7-s stall, a killed server, bad `lockstep_mode` / `lockstep_hung_ms` | every frame its own camera's; the hung route once (LOUD line, the proxy's call, recovery); a sent message answered only after the long frame; the dead route at once; CONFIG ERROR | `tests/test_lockstep.py` `test_exact_*`; `lockstep_server --stall-heartbeat-stop` |
| Held frames (ticket 0691, MC #11169): the server holds a frame until the EXE presented 40 more (the load deadlock's regression), the same hold unreported, a hold that never clears | held_skip per held frame, no world, exact after, no hung end; unreported: the hung route (the deadlock); stuck: one fault and the proxy's call | `test_exact_held_frames_are_not_waited_for`, `test_exact_unreported_hold_deadlocks_into_the_hung_route`, `test_exact_stuck_hold_takes_the_failure_route_once`; `lockstep_server --hold-for-exe` |
| `shell_server.exe --scene terrain --bake standin` (issue 3407): bad `--bake*` / `--standin-*` / `--options-file` / `--tilegen` values, residency options without `--bake standin`, `--tile-plots` other than 1, `--color` other than caches, another scene; a tile that fails to bake; out of memory at texture creation (radius lowered, then baking stopped); options file missing or with a bad value; tile generation block missing, of another map size, bad magic or version | exit 2 with the reason / logged, counted, plain tile, exit 0 | `tests/test_residency.py`; `research/05-graphics-composition/terrain-bake-residency.md` |
| `tools/tilegen_feed.py`, `tools/residency_drive.py` input checks | exit 2-4 | **not tested offline** (tilegen_feed reads a live game by PID); `residency_drive.py --dry-run` checks every input and the build |

## Residency, bake-ahead and invalidation (issue 3407, epic 3400, 2026-11-13)

`shell_server.exe --scene terrain --bake pipeline|standin` bakes and keeps the tiles' colour caches in the server
instead of reading the EXE's through the bake tap. Since wave 3 (2026-11-13) the residency is the library
`c5r::bake::Residency` in `render/` (`render/include/c5r/bake/residency.h`, shared with server.exe through epic 3300's
TileCacheSource seam); `shell_server.cpp` only calls it. Bakers: `pipeline` (3404's `PipelineBaker`, `--game-dir` or
`C5R_GAME_DIR`, the EXE's shaders from `--exe`) or `standin` (`render/include/c5r/bake/standin.h`). Options:
`--bake-radius`, `--options-file` (else `VP64Options.ini` through `options/`, compiled in from `options/src`),
`--bake-rate`, `--bake-burst`, `--bake-pump-ms`, `--bake-gpu-ms-per-s`, `--bake-trace`, `--cache-arrays`,
`--bake-memory-mb`, `--tilegen NAME` (the tile content-generation block and its header-only reader,
`server/tilegen_block.h`), test seams `--standin-fail`, `--standin-vram-mb`, `--standin-ms`. Counters in the log: `residency 1s:` and `residency totals:` (gray frames =
acceptance C's measure). Tools: `tools/tilegen_feed.py` (the EXE's tile+0x1CC by PID into the block; a watch of what
stock does with a changed tile), `tools/residency_drive.py` (a recorded camera path over a real field). Design and
figures: `research/05-graphics-composition/terrain-bake-residency.md`; tests `tests/test_residency.py`.

## Changes since the prototype (issue 3006, 2026-11-10)

The "Design" text below describes the prototype. Since 3006 the DLL writes no vtable: it hooks the implementation
functions in d3d11.dll / dxgi.dll with MinHook (`third_party/minhook`), found through the vtables of real objects;
it tags deferred-context command lists and inserts by rule (`insert=tagged`, `tag_rule=after|before|inlist`,
`tag_match`, `tag_anchor`, `tag_nth`; all live from the ini, documented in `composite/shell_composite.ini`); a swap
chain created through the factory is adopted at its first Present; every hook group has a kill switch
(`hook_<name>=0`). The stats line adds `rule`, `rec_*` (deferred recording cost), `miss_nomatch/noanchor/noexec/notready`,
`ins_checked/ins_wrong` (insertions outside the clear..3D window, judged from the tags), `lists_pf`, `untagged`,
`nodraw`; `slot_rewrites` is gone. Findings: `research/05-graphics-composition/insertion-point-and-frame-structure.md`
section 8 and `hooks-overlays-and-ceg.md` section 2.

## Camera and hex grid (issue 3007, 2026-11-11)

The DLL reads the EXE's camera (view/projection globals, wrap views, view mode, viewport, grid constants) at every
Present of its target, read-only, after validating the EXE once (TimeDateStamp + the byte patterns of
`common/exe_camera.h`, rebased on the module base), and publishes it in the section's camera block. Ini: `camera=1`
(0 = kill switch, live), `camera_test=stall|corrupt` (tests only). `shell_server.exe --scene grid [--grid-scale F]
[--heightmap DUMP.raw] [--flat-z Z] [--stale-frames N] [--grid-stale tint|hide] [--bad-frames N]` draws a hex outline
grid in world coordinates with it (view 0 yellow, wrap views cyan/green, STALE red; transparent background: use
`blend=alpha` in the ini to see the game under it); `--dump PATH --dump-after N` writes one frame and exits;
`--print-layout PATH` writes the block's offsets. Findings and the live checklist:
`research/02-exe-internals/camera-and-picking.md` section 10, `research/05-graphics-composition/shared-texture-and-compositor.md`
section 7.

## Live naming of the frame's lists (issues 3005/3008, 2026-11-11)

The trace (`shell_composite.trace`) now prints, per executed list, every render-target and depth format, the number of
clears and binds, and the size of the first non-back-buffer target and of the first depth view (sizes recorded only
while a trace is pending). New trigger `shell_composite.listshot` (`[listshot] from=A to=B`): dumps the back buffer right
after `ExecuteCommandList` #k, one k per frame, k = A..B (BMPs `listshot_kNNN_*.bmp`; nothing is drawn; keep the camera
still). Findings: `research/05-graphics-composition/insertion-point-and-frame-structure.md` section 9 (the game clears
its back buffer and D24S8 depth in one deferred list; `tag_rule=after`, `tag_match=clear_bb` puts the world under
landmarks, cities, units and UI) and `shared-texture-and-compositor.md` section 8 (live checks).

## Run by hand

Only on a hidden desktop (rule 14). `bin\shell_server.exe [--size 1920x1080] [--rate 60|0] [--luid HEX] [--seconds N]
[--section NAME] [--name TEXBASE] [--log PATH]`; `bin\test_host.exe [--swap blit|flip] [--vsync 0|1] [--load
before|after|none] [--bb 2560x1440] [--scenario robust] [--deferred] ...`. Start order does not matter: the DLL opens
the section once per second until it exists.

## Design

```
 shell_server.exe (x64)                          test_host.exe / CivilizationV_DX11.exe (x86)
 ----------------------                          --------------------------------------------
 own D3D11 device (adapter by LUID               game device + immediate context + swap chain
 or the one with most VRAM)                         |
   render scene -> private RT                       | Present / ResizeBuffers   (IDXGISwapChain vtable, dxgi.dll)
   AcquireSync(0)  [or (1): overwrite]              | Clear / OMSetRenderTargets / ExecuteCommandList
   CopyResource -> shared texture  ======NT handle, named===> (per-object ID3D11DeviceContext vtable)
   write frame# + QPC into section                  v
   ReleaseSync(1)                                shell_composite.dll at the insertion point:
                                                   AcquireSync(1, timeout) -> read frame# + QPC
 Local\VPShellSection (4 KB):                      CopyResource -> local texture, ReleaseSync(0)
   texture name/size/generation (seqlock)         save state -> fullscreen triangle -> restore state
   frame counter + frame QPC (under the mutex)
   heartbeat QPC, resize requests (client->server)
```

### Server (`shell_server.exe`)

* Picks the adapter by `--luid HEX` or the non-software adapter with the most
  dedicated VRAM, and logs every adapter with its LUID. The client writes its
  own adapter LUID into the section and the server logs a MISMATCH warning if
  they differ (the shared texture cannot be opened across adapters).
* Renders a hex grid + spinning quad + a magenta bar that moves one pixel per
  frame, plus a 32-cell barcode of the frame counter in the top-left corner,
  into a private render target, then publishes it into the shared
  `B8G8R8A8_UNORM` texture created with
  `D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`
  and exported with `IDXGIResource1::CreateSharedHandle` as
  `Local\VPShellWorld_<pid>_<generation>`.
* Keyed mutex: `AcquireSync(0,0)`; if that fails, `AcquireSync(1,0)` (the
  client did not take the previous frame: it is overwritten and counted); if
  both fail for 50 ms the frame is skipped. Copy, write frame counter + QPC into
  the section, `ReleaseSync(1)`. With `--gpu-sync 1` (default) the server waits
  for the GPU to finish the copy before releasing, so the frame QPC is the
  GPU-complete time and the client never waits on the server's GPU work; the
  mutex is held for one copy, not a whole render. A server never blocks on a client that stopped presenting (minimised
  game, hung game), and a client never waits for the server (timeout 0).
* Rate: `--rate 60` (default, high-resolution waitable timer + spin), `--rate 0`
  = as fast as possible; GPU work in flight is capped at 2 frames with event
  queries (there is no swap chain to throttle it).
* Resize: the client writes the wanted size and bumps `reqSeq`; the server
  recreates both textures under a new generation number and name and publishes
  the new texture block under a seqlock. A restarted server starts at the size
  the client asked for last, so a restart costs no extra recreation.
* Logs per second (rendered/published/overwritten/skipped, max acquire wait)
  and every 10 s private bytes, working set and process video memory.

### Composite DLL (`shell_composite.dll`)

**Loading.** `DllMain` does nothing heavy: it pins the module (hooks point into
it), opens the log and starts a control thread. The control thread reads the
config and, unless `mode=off`, creates a dummy device + a blit-model and a
flip-model swap chain on two hidden windows, reads their vtable pointers,
destroys them, and patches `Present` (slot 8), `ResizeBuffers` (13) and
`Present1` (22) of the swap-chain vtable in `dxgi.dll` (same vtable for both
swap effects on this machine). Nothing waits for the host: loading after the
host created its device is the normal path, and loading before it works too
(both tested).

**The immediate context is hooked from the real one, not from the dummy.**
On Windows 11 the `ID3D11DeviceContext` vtable is a per-object table that
lives in heap memory right behind the context object (`ctx 030C00E0`, `vtbl
030C00E4` in the logs), so the dummy device's context vtable is useless. At the
first `Present` of the target swap chain the hook calls
`GetDevice -> GetImmediateContext` and patches that context's table:
`ClearRenderTargetView` (50), `OMSetRenderTargets` (33),
`ExecuteCommandList` (58).

**The runtime rewrites the ClearRenderTargetView slot inside every Present.**
Measured: right after the first patch, the next frame's clear was not seen;
`slot_rewrites` in the stats equals the frame count in every run. The
DLL re-asserts its context slots right after each original `Present` and
`ResizeBuffers` returns (one compare per slot; the table is in writable heap
memory so no VirtualProtect is needed). `OMSetRenderTargets` and `ExecuteCommandList` were
never rewritten. Each hook looks up its original by `(vtable, slot)`, so several
patched vtables never share one "original" pointer, and if somebody else
re-points a slot the hook chains to the new target.

**Target swap chain.** The first swap chain that presents is adopted (logged
with size, format, buffer count, swap effect, sample count, window and title).
Other swap chains are logged once and ignored; if the target stays silent for
2 s, the next presenting chain becomes the target. *Since 2026-11-13 (issue 3017):
a chain created through the factory takes over at its first Present only when it
presents to the target's window (the game's resolution change) or the target's
window is gone; beyond 8 other chains, their Presents are counted in one line per 10 s.*
*Since 2026-11-13 (issue 0141): a chain is known by its address and its creation
number (`creation #N` in the log), so a new chain that gets the released target's address
(heap reuse) is adopted like any new chain, with fresh back-buffer state.*

**Insertion points** (`insert=` in the ini, `nth=` for N):

| insert | fires | draws into |
|---|---|---|
| `clear` (default) | after the first `ClearRenderTargetView` of the back buffer in the frame | that RTV |
| `nthclear` | after the N-th `ClearRenderTargetView` of any RTV in the frame | that RTV (viewport = its size) |
| `omset` | after the N-th `OMSetRenderTargets` that binds the back buffer at slot 0 | that RTV |
| `exec` | after the N-th `ExecuteCommandList` on the immediate context | own RTV on buffer 0 |
| `present` | just before `Present` (on top of everything; debug) | own RTV on buffer 0 |

The back buffer is recognised by comparing the RTV's resource with the raw
pointer of swap-chain buffer 0 (no reference is kept, so `ResizeBuffers` is
never blocked). Calls on other contexts (deferred contexts) are ignored by the
clear/omset hooks; their work reaches the immediate context through
`ExecuteCommandList`, hence the `exec` mode.

**At the insertion point:** open the texture by name if `(server pid,
generation)` changed (`ID3D11Device1::OpenSharedResourceByName`), check the
frame counter in the section first (skips the kernel call when nothing new was
published), `AcquireSync(1, timeout_ms)` (default 0), read frame counter + QPC
(consistent with the pixels because the server writes them while holding the
mutex), `CopyResource` into a local `B8G8R8A8_TYPELESS` texture,
`ReleaseSync(0)`, draw. On timeout the previous copy is drawn; on
`WAIT_ABANDONED` (server died holding the mutex) or an error the texture is
closed and re-opened when the server publishes again.

**State saved and restored around the draw:** IA input layout + topology (no
vertex/index buffer is bound: the triangle comes from `SV_VertexID`); VS, PS,
GS, HS, DS with their class instances (GS/HS/DS are nulled during the draw);
PS SRV slot 0 and sampler slot 0; RS state, all viewports, all scissor rects;
OM blend state + blend factor + sample mask, depth-stencil state + stencil ref,
all 8 RTVs + DSV; predication. The draw binds only the target RTV (no DSV),
depth/stencil off, cull none, opaque blend (or straight alpha with
`blend=alpha`).

**Formats.** If the target RTV is an `_SRGB` view, the local copy is sampled
through a `B8G8R8A8_UNORM_SRGB` view so decode + encode cancel out (verified:
same pixel values as the UNORM case). MSAA targets are drawn into directly
(all samples get the same value). The back-buffer dump resolves MSAA first.

**Robustness.** Every hook body runs under SEH; an exception or an unexpected
D3D failure sets `DISABLED` and the hooks keep passing calls through. Hooks
check `ctx == the target's immediate context` and a thread-local re-entrancy
flag (the DLL's own `OMSetRenderTargets` calls go through its own hook).
`ResizeBuffers` on the target releases every D3D object of the DLL (including
the shared and local textures and its own back-buffer RTV) before calling the
original, then re-reads buffer 0 and asks the server for the new size.

**Config and live switching.** `shell_composite.ini` next to the DLL, every key
overridable by an environment variable `VPSHELL_<KEY>`. The control thread
re-reads the ini when its timestamp changes, so `mode` / `insert` / `nth` can
be switched while the host runs; `mode=off` unpatches every slot, switching
back re-patches. Trigger files next to the DLL: `shell_composite.shot` dumps
the next presented back buffer to a BMP (`shot_dir=`), `shell_composite.trace`
logs every clear / OMSetRenderTargets / ExecuteCommandList of the next frames
(`[trace] frames=N` inside the file, default 2). The first `trace_frames`
frames after adopting a swap chain and after every `ResizeBuffers` are always
traced.

**Log** (`shell_composite.log` in the output folder, or `VPSHELL_LOG`; the output folder is `VPSHELL_OUT_DIR` when set,
else the DLL's folder. On the product path the proxy sets `VPSHELL_OUT_DIR=<user folder>\Logs`, so the log and the
CSVs are in `Documents\My Games\Sid Meier's Civilization 5\Logs`, not beside the DLL: the EXE watches the whole
`<user folder>\MODS` tree and stalls ~150 ms on every write under it, issue 0148. Ticket 0385: a previous log is kept as
`shell_composite.<yyyymmdd-hhmmss>[-n].log`; ticket 0747: only the newest `VPSHELL_KEEP_LOGS` (1 to 100, else 10; the
proxy sets it from `[Logs] KeepRuns`) of those stay, older ones are deleted at the start, never another name): one `stats`
line per second with frames, Present-to-Present frame time (mean / p99 /
max), CPU cost inside the insertion (`comp_*`), `AcquireSync` CPU time
(`acq_*`), GPU time of the composite from timestamp queries split into frames
that only draw (`gpu_draw_*`) and frames that also copy a new server frame
(`gpu_copy_*`, with `gpu_syncwait_*` the part up to the `AcquireSync`), latency server-release to client-Present for new frames
(`lat_*`), mean age of what is displayed (`age_mean`), new / dropped server
frames (`srv_new`, `srv_drop` = gaps in the server's frame counter), frames
that showed the previous copy (`dup`), mutex timeouts, frames where the
insertion point never fired (`missed_ins`), context-slot rewrites, clears per
frame, server staleness and texture/back-buffer sizes.

### Test host (`test_host.exe`)

Window + D3D11 device + swap chain: `--swap blit` (`DXGI_SWAP_EFFECT_DISCARD`,
1 buffer, the likely 2010-era setup), `flip` (`FLIP_DISCARD`) or `flipseq`;
`--buffers`, `--format rgba|bgra|srgb`, `--msaa N`, `--vsync 0|1`. Each frame
clears to grey, then draws "UI": opaque top/bottom bars and minimap box, a 70 %
opaque left panel and a 55 % opaque centre dialog, each with alpha-tested
glyph noise. `--deferred` records the clear on a deferred context and runs it
through `ExecuteCommandList` (the Civ 5 pattern). `--load before|after|none`
(after = at frame 30, long after the device exists), `--bb WxH` fixes the
back-buffer size independently of the window, `--scenario robust` scripts
minimise / restore / window resize / ResizeBuffers 1920x1080 and 2560x1440 /
window back (/ fullscreen with `--allow-fullscreen`), `--churn N` calls
ResizeBuffers every N seconds (soak). F1 = ResizeBuffers to the next
resolution, F2 = fullscreen toggle, F3 = resize window. The window is shown
with `SW_SHOWNOACTIVATE` and opens on the left monitor so it never takes the
foreground. Logs 1 s frame stats, 10 s memory, and a summary (first 3 s
excluded) that `--summary` appends to a CSV.

## Results (test host)

Machine: RTX 4080 (LUID `E0D7`), Windows 11 26100, d3d11.dll 10.0.26100. The
host window sits on a 165 Hz monitor, so "vsync on" = 165 fps. The real game
was running on the same GPU during every measurement (another agent's probes),
which is the main source of run-to-run noise.

### (i) The composite sits above the clear and under the UI

![blit model, insert=clear](shots/10-blit-clear-t5.png)

`shots/10-blit-clear-t5.png`, blit model, `insert=clear`: the hex world fills
the frame where the host only cleared to grey; the opaque top/bottom bars and
minimap box cover it; the 70 % left panel and the 55 % centre dialog show it
through; the host's text noise is on top. The server's frame-counter barcode
(top-left) is hidden by the top bar, as it should be. With `insert=present`
(`shots/14-blit-present-t5.png`) the world covers the whole UI and the
barcode is visible: the debug mode works as intended.

Functional matrix (8 s runs, 165 Hz vsync, 1280x720, server 60 Hz; stats over
the last 3-5 s, `results/runs_summary.csv`):

| run | swap chain / host | insert | composite visible, UI on top | DLL CPU mean / p99 (ms) | latency mean / p99 (ms) | missed insertions |
|---|---|---|---|---|---|---|
| 10 | blit DISCARD, 1 buffer, RGBA8 | clear | yes | 0.17 / 0.73 | 3.1 / 7.2 | 0 |
| 11 | flip DISCARD, 2 buffers | clear | yes | 0.15 / 0.60 | 3.4 / 6.0 | 0 |
| 12 | blit | nthclear, N=1 | yes | 0.18 / 0.74 | 3.1 / 6.0 | 0 |
| 13 | blit | omset | yes | 0.28 / 1.13 | 5.3 / 9.2 | 0 |
| 14 | blit | present | yes, on top of the UI (by design) | 0.23 / 0.94 | 3.6 / 8.0 | 0 |
| 15 | blit, 4x MSAA back buffer | clear | yes | 0.24 / 0.90 | 3.4 / 6.6 | 0 |
| 16 | blit, R8G8B8A8_UNORM_SRGB | clear | yes, colours identical to run 10 (pixel check) | 0.17 / 0.63 | 4.6 / 10.7 | 0 |
| 17 | blit, DLL loaded **before** device creation | clear | yes | 0.15 / 0.75 | 3.5 / 6.9 | 0 |
| 18 | flip DISCARD, 3 buffers, BGRA8 | clear | yes | 0.14 / 0.55 | 3.5 / 6.2 | 0 |
| 19a | blit, clear on a **deferred context** + ExecuteCommandList | clear | **no** (never fires: `missed_ins` = every frame) | - | - | all |
| 19b | same | exec, N=1 | yes | 0.12 / 0.53 | 3.2 / 6.7 | 0 |

Screenshots for each are in `shots/` (`<run>-t5.png`). Run 19a is the
expected failure that motivates the `exec` mode and step 2 of the checklist.

### (ii) Frame time: no DLL / off / passthrough / composite

Host uncapped (`--vsync 0`), back buffer fixed at **2560x1440** (window
1280x720), blit model, server at 60 Hz rendering 2560x1440. Five modes
interleaved over 3 rounds of 20 s (60 s per mode; a fresh host process per
run; first 3 s of each run dropped). `tools/ab.ps1`, `results/ab_*.csv`.

| mode | frames | host frame time mean (ms) | spread of the 3 run means | host p99 (ms) | DLL CPU per frame mean / p99 (ms) | AcquireSync CPU per new frame (ms) | GPU, draw-only frame (ms) | GPU, copy + draw frame (ms) |
|---|---|---|---|---|---|---|---|---|
| no DLL | 556,148 | 0.0927 | 0.082 - 0.106 | 0.296 | - | - | - | - |
| off (DLL loaded, no hooks) | 549,789 | 0.0944 | 0.082 - 0.112 | 0.310 | - | - | - | - |
| passthrough (hooks, no draw) | 550,803 | 0.0927 | 0.089 - 0.097 | 0.291 | - | - | - | - |
| composite, insert=clear | 584,818 | 0.0873 | 0.084 - 0.091 | 0.289 | 0.0053 / 0.0116 | 0.126 | 0.0143 | 0.078 |
| composite, insert=present | 582,290 | 0.0877 | 0.085 - 0.092 | 0.290 | 0.0051 / 0.0103 | 0.130 | 0.0135 | 0.075 |

At ~11,000 frames/s the five modes are indistinguishable: the differences
between modes (< 0.01 ms) are smaller than the spread between runs of the same
mode (up to 0.03 ms), which comes from the GPU being shared with the running
game. The direct measurements are the useful numbers: per composited frame the
DLL spends ~5 us of CPU when there is no new server frame and ~0.13 ms more
(`AcquireSync`) when there is one (60 times a second); the GPU spends 14 us on
the 2560x1440 fullscreen draw and ~75 us on a frame that also copies the
14.7 MB shared texture.

At display rate the CPU costs are higher because the CPU sits in low clock
states between frames (runs 30 and 33, 165 Hz vsync, 2560x1440 back buffer):

| run | mode | frames | host frame time mean / p99 (ms) | DLL CPU per frame mean / p99 (ms) | AcquireSync mean / p99 (ms) |
|---|---|---|---|---|---|
| 33 (30 s) | passthrough | 4,441 | 6.08 / 7.98 | - | - |
| 30 (60 s) | composite, clear | 9,339 | 6.10 / 9.69 | 0.118 / 0.494 | 0.248 / 0.450 |

**GPU timestamps across the keyed mutex are not a cost measure at display
rate.** The DLL brackets the composite with timestamp queries and splits
frames that copy a new server frame into "up to the `AcquireSync`"
(`gpu_syncwait`) and "copy + draw" (`gpu_copy`):

| run | host | GPU draw-only frame (ms) | GPU sync part (ms) | GPU copy + draw (ms) | host frame time (ms) |
|---|---|---|---|---|---|
| 37 | uncapped | 0.014 | 0.044 | 0.070 (p99 0.15) | 0.084 |
| 36 | 165 Hz vsync | 0.26 | 0.009 | 5.1 (p99 7.7) | 6.10 |

At 165 Hz the copy span is 5 ms although the same copy takes 0.07 ms when the
GPU is kept busy, and the host's frame time and p99 do not move against
passthrough (6.10 / 9.7 vs 6.08 / 8.0 ms). `AcquireSync` makes the runtime
submit the command buffer and queue a wait on the shared sync object, so the
commands after it (copy, draw, and the rest of the host's frame) land in the
next submission; the span measures the GPU timeline until that submission
runs, mostly idle time at low load and low clocks, not work. Draw-only frames
grow from 14 us to 0.15-0.26 ms for the same reason (GPU clocks drop between
frames at 165 Hz with this tiny workload). Two consequences for the game: one
extra command-buffer split per new server frame (60/s), and the GPU numbers
that matter must come from a busy GPU or from GPUView/PIX, not from these
timestamps. To rule out that the client waits for the *server's* GPU work, the
server by default (`--gpu-sync 1`) waits for its copy to finish before
`ReleaseSync(1)`; with or without it the spans were the same (runs 34 / 35:
5.9 vs 6.2 ms), and `gpu_syncwait` is 9-44 us.

### (iii) Latency server frame -> client Present

`lat` = client `Present` call time minus the QPC the server wrote right before
`ReleaseSync(1)` of that frame (same machine, same QPC clock), for frames that
brought new content; `age` = the same for every displayed frame including
repeats.

| run | host | server | lat mean / p99 (ms) | age mean (ms) | server frames shown | dropped (overwritten before the host took them) | repeats |
|---|---|---|---|---|---|---|---|
| 30 | vsync 165 Hz, 2560x1440 | 60 Hz | 4.03 / 7.79 | 8.72 | 3,376 in 56 s (60.3/s) | 0 | 5,849 |
| 31 | vsync 165 Hz, 2560x1440 | as fast as possible (~4,000/s) | 0.27 / 0.80 | 0.89 | 3,867 | 128,371 | 379 |
| 32 | uncapped (~3,700 fps), 2560x1440 | as fast as possible | 0.33 / 0.63 | 0.43 | 53,518 | 31,324 | 42,247 |

With a 60 Hz server and a host that presents faster, the content is on average
half a server period old plus the time to the host's next insertion point, and
every server frame is shown exactly once (0 dropped). A free-running server
drives latency under 1 ms at the cost of rendering frames nobody sees.

### (iv) Robustness

| event | how | result |
|---|---|---|
| minimise / restore | `--scenario robust` (ShowWindow) | Present returns `DXGI_STATUS_OCCLUDED`, host drops to 33 fps, composite keeps running; normal after restore (`runs/20-robust`) |
| window resize | 1280x720 -> 1024x640 -> back | DLL releases everything before the original ResizeBuffers (0.6 ms, returned S_OK), asks the server for the new size, server recreates the texture (generation +1), new texture opened 0.25 s later; old one drawn stretched in between |
| ResizeBuffers to another resolution | 1920x1080 and 2560x1440 in a 1024x640 window | same path, S_OK, `shots/20-robust-t16.png`, `-t20.png` |
| fullscreen toggle | not run, see limitations | - |
| server killed (TerminateProcess) | `runs/21-server-restart` | 1 s later `server STALE`; the last copied frame keeps showing (`shots/21-server-killed.png`); no errors, host unaffected |
| server relaunched | new process, new pid | `server alive again`, texture `Local\VPShellWorld_<newpid>_1` opened by name within 0.3 s (`shots/21-server-restarted.png`); with the current build the new server starts at the client's last requested size |
| DLL loaded before / after the device | `--load before`, `--load after` (frame 30) | both work (runs 17, 10) |
| live mode switch | edit the ini while running | applied at the next Present; `off` unpatches, other modes re-patch |

### (v) 10-minute soak

`tools/soak.ps1`: server at 60 Hz, host at 165 Hz vsync, insert=clear, and
`ResizeBuffers` every 20 s cycling 1280x720 -> 1920x1080 -> 2560x1440
(29 resizes, 31 server texture generations), 600 s. Private bytes and handles
sampled every 30 s from outside (`results/soak_mem.csv`):

| process | private bytes at 30 s | at 120 s | range 120-600 s | at 600 s | handles 120 -> 600 s |
|---|---|---|---|---|---|
| test_host.exe (with the DLL) | 33.92 MB | 34.20 MB | 34.08 - 34.86 MB | 34.82 MB | 435 -> 441 (range 432-445) |
| shell_server.exe | 65.32 MB | 82.39 MB | 82.34 - 82.52 MB | 82.50 MB | 441 -> 435 (range 433-441) |

No growth trend: the host moves within a 0.8 MB band without a direction, the
server is flat after its first 2560x1440 texture pair (the step from 65 to
82 MB in the first minute). Process video memory at the same back-buffer size
is identical at minute 1 and minute 10 (host at 2560x1440: 56.47 MB both;
server at 2560x1440: 50.45 MB both). Over the 600 s: 98,074 frames, 35,383
server frames shown, 2 dropped, frame time 6.09 / p99 8.40 ms, DLL CPU
0.150 / p99 0.574 ms per frame, latency 3.9 / p99 7.1 ms, no DLL errors. The
soak exposed one bug, fixed and re-verified since: `ResizeBuffers` also
restores the ClearRenderTargetView slot, so the first frame after each resize
missed its insertion (29 `missed_ins`); the DLL now re-asserts its slots after
`ResizeBuffers` as well (`runs/23-robust-v2`: 0 missed).

## Screenshots

| file | shows |
|---|---|
| `shots/10-blit-clear-t5.png` | default: blit model, insert=clear, UI over the world |
| `shots/11-flip-clear-t5.png` | flip model |
| `shots/12-blit-nthclear1-t5.png`, `13-blit-omset-t5.png` | other insertion points |
| `shots/14-blit-present-t5.png` | insert=present: world over the UI, barcode visible |
| `shots/15-blit-msaa4-t5.png` | 4x MSAA back buffer |
| `shots/16-blit-srgb-t5.png` | sRGB back buffer: the world is unchanged, the host's own UI is washed out (the host writes linear colours into an sRGB target; the composite is not affected) |
| `shots/17-load-before-t5.png`, `18-flip-bgra-3buf-t5.png` | load order, BGRA flip with 3 buffers |
| `shots/19-deferred-execmode-t5.png` | host clears on a deferred context, insert=exec |
| `shots/20-robust-t4/t10/t14/t16/t20/t23.png` | before minimise, after restore, 1024x640 window, back buffer 1920x1080 and 2560x1440 in that window, back to 1280x720 |
| `shots/21-server-killed.png`, `21-server-restarted.png` | frozen last frame while the server is dead, live again after relaunch |

## Loading the DLL into the real game

The coordinator's hook in `CvGameCore_Expansion2.dll`: at `DllGetGameContext`
time (main menu), read `<My Games>\Sid Meier's Civilization 5\crashlogs\shell.helper`
(one line, absolute path) and `LoadLibraryW` it once. That fits this DLL:

* `DllMain` only pins the module, opens the log and starts the control thread;
  all hooking happens on that thread after the loader lock is released, and the
  context hooks are installed from inside the first `Present` on the game's
  render thread. Loading long after the device exists is the tested default
  (`--load after`); there is no time limit.
* `DllGetGameContext` is not called under the loader lock anyway, but nothing
  in the DLL depends on that.
* Dependencies: `d3d11.dll`, `user32.dll`, `kernel32.dll` only (static CRT;
  `dumpbin /dependents`). Never call `FreeLibrary` on it (it pins itself
  regardless: the hooks point into it).
* `shell_composite.ini` lives next to the DLL, and so does `shell_composite.log` unless `VPSHELL_OUT_DIR` or
  `VPSHELL_LOG` names another place (issue 0148; never a folder under `MODS`), so put
  the DLL in a writable folder outside the Steam install (for example a
  `shell\` folder in the worktree, or next to the marker in `crashlogs\`).
* Log the `LoadLibraryW` result and `GetLastError()` in the game DLL; a NULL
  return with error 126 is a wrong path, 193 a 64-bit build.
* `shell_server.exe` is started separately (before or after the game; the DLL
  polls for the section once per second). It will be asked for a texture the
  size of the game's back buffer.
* Kill switches: delete the marker (next start), or set `mode=off` in the ini
  while the game runs (unpatches every slot within 0.5 s).
* Cost in the 32-bit address space: the dummy device + two 16x16 swap chains
  exist for a few milliseconds at load; afterwards the DLL holds about 0.5 MB of
  static data, a few D3D state objects, 24 queries and two textures the size
  of the back buffer (the opened shared one and the local copy, both GPU-only,
  no CPU mapping). Not yet measured inside the game.

## Checklist for the first live-game run

**0. Before starting.** Build. Copy `bin\shell_composite.dll` + `.ini` to the
chosen folder. Set `mode=passthrough`, `insert=clear`, `trace_frames=3`. Start
`bin\shell_server.exe --rate 60`. Write the marker. Do **not** run RenderDoc and
the helper in the same session: RenderDoc wraps the device, so the DLL would
patch RenderDoc's wrapper vtables and both captures become hard to read. Do a
RenderDoc capture alone first if time allows.

**1. Read the log top-down (main menu).**
* `loaded into pid ... CivilizationV_DX11.exe`, `swap-chain vtables: blit X,
  flip Y`, three `patch ... ok` lines.
* `adopted swap chain`: note width x height (2560x1440 expected), `fmt`
  (28 = R8G8B8A8_UNORM, 29 = its sRGB, 87 = B8G8R8A8_UNORM, 91 = its sRGB),
  `buffers`, `effect` (0 = DISCARD, 1 = SEQUENTIAL, 3 = FLIP_SEQUENTIAL,
  4 = FLIP_DISCARD), `samples` (> 1 = MSAA back buffer), `windowed`, window
  title. If instead `WARNING: hooks installed 10 s ago but no Present seen`
  appears, the game's swap chain does not use the dxgi.dll vtable the dummy
  found (another overlay wrapping it, or an older DXGI path): stop and look at
  which module the game's `Present` lives in.
* `another swap chain presents` lines: a second swap chain exists (second
  window, tablet UI). Check that the adopted one is the 2560x1440 main window.
* Three `patch ID3D11DeviceContext::...` lines with a vtable address right
  behind the context address.

**2. Read the frame trace** (the `trace:` lines of the first 3 frames; write an
empty `shell_composite.trace` next to the DLL to get more, e.g. in game rather
than at the menu). Questions, in this order:
1. Is the back buffer cleared on the immediate context (`clear #n ... <== BACK BUFFER`)?
   `missed_ins` in the passthrough stats answers it for every frame: 0 means
   the `clear` insertion point fires each frame.
2. How many `ExecuteCommandList`s per frame? Civ 5 is known for DX11
   multithreaded rendering: if most work arrives in command lists, clears on
   deferred contexts are invisible here and the insertion point is an
   `ExecuteCommandList` index (`insert=exec`, `nth=k`).
3. Does the world render into an offscreen target (an RTV of screen size,
   possibly MSAA, possibly a float format such as 10 = R16G16B16A16_FLOAT or
   26 = R11G11B10_FLOAT) that is later resolved or blitted into the back buffer?
   Then the first `OMSetRenderTargets` that binds the back buffer *after* that
   pass is where the UI starts: `insert=omset`, `nth=k`.
4. Which `OMSetRenderTargets` binding of the back buffer starts the UI pass?
   (`omsets N, of which bind the back buffer M` per frame.)

**3. First composite: `insert=present`.** Edit the ini (it is re-read live).
The hex world must cover everything, UI included. This proves, inside the game
process, the same adapter, `OpenSharedResourceByName`, the keyed mutex and the
state restore (the game must keep rendering normally underneath). Write an
empty `shell_composite.shot` to dump the back buffer to a BMP. Check the stats:
`lat_mean`, `gpu_copy_mean`, `comp_mean`, `missed_ins=0`, `srv_drop`, `dup`.

**4. Under the UI.** Switch to the insertion point the trace suggested:
`clear` if the back buffer is cleared at frame start on the immediate context
(that places the world *under* the game's own 3D world, so it only shows once
the EXE's world rendering is turned off, see `[DisableSystems]`), else
`omset` / `exec` with the `nth` from the trace, else `nthclear`. Confirm with a
`.shot` that the UI is on top and the world shows through translucent panels.

**5. What can go wrong, and how it shows.**

| Symptom | Likely cause | What to do |
|---|---|---|
| no `adopted swap chain` | game Present is not on the dummy's vtable | find the module; hook that vtable or the function |
| `missed_ins` = frames in `clear` mode, `clears_pf` 0 | back buffer cleared on a deferred context, or never cleared | use `exec` / `omset` |
| composite visible but covered by the world | inserted before the 3D pass | later `nth`, or `omset` after the world's resolve |
| world resolved into the back buffer with `ResolveSubresource` / `CopyResource`, no clear or bind in between | no hook on those calls yet | add an insertion after `ResolveSubresource` (slot 57) - not implemented |
| `samples` > 1 | MSAA back buffer | handled (drawn into all samples); `.shot` resolves |
| washed-out or too dark composite | sRGB mismatch | handled for `_SRGB` RTVs; check `viewfmt` in the trace |
| `OpenSharedResourceByName ... failed` with LUID mismatch | game on another adapter (hybrid laptop) | start the server with `--luid` = the LUID the DLL logs |
| `slot_rewrites` = 0 and clears missed | the runtime changed how it rewrites the table | the stats show it; re-assert point may need to move |
| `DISABLED` in the stats | an exception or failure inside the DLL | the log line before it says where; hooks keep passing through |
| UI flickers or vanishes after the composite | a piece of state not restored (UAVs, other PS slots, stream-out) | compare in RenderDoc the state before/after the inserted draw |
| game crash with the helper loaded | not expected: report the log tail | `mode=off` isolates hooks vs. drawing |
| `stale=1`, frozen world | server not running or hung | the last frame keeps showing (`hide_when_stale=1` hides it) |

**6. Record:** the full log, the trace lines, two `.shot` BMPs (present mode,
final mode), the stats over 60 s in each of `passthrough` and the final
composite mode (switch live via the ini), and Task Manager's GPU engine use of
both processes.

## Known limitations and open points

* **Exclusive fullscreen toggle not exercised.** The code path
  (`SetFullscreenState` -> `WM_SIZE` -> `ResizeBuffers`) uses the same
  release/re-acquire path that the window-resize and ResizeBuffers tests
  passed, and the host supports it (`--scenario robust --allow-fullscreen`,
  F2), but switching a window to exclusive fullscreen takes the foreground and
  could knock the game (running another agent's probes on the main monitor)
  out of its own fullscreen. Run it when the game is not running.
* Latency is CPU time: server `ReleaseSync` submit to client `Present` call.
  It excludes the GPU execution of both sides and scan-out.
* `AcquireSync` costs 0.1-0.6 ms of CPU per new server frame (a kernel
  transition), paid on the game's render thread 60 times a second; mutex polls
  without a new frame are skipped by checking the frame counter first. A shared
  `ID3D11Fence` (the runtime is the OS's, so available to the 2010 game on
  Windows 10+) or a ring of textures would be cheaper.
* Not hooked: `IDXGISwapChain3::ResizeBuffers1`, `ClearView`,
  `OMSetRenderTargetsAndUnorderedAccessViews` (a back-buffer bind through it is
  not seen by `omset`), `ResolveSubresource` / `CopyResource` as insertion
  points, deferred contexts themselves.
* The state restore does not cover UAVs bound through
  `OMSetRenderTargetsAndUnorderedAccessViews` (restoring the RTVs with
  `OMSetRenderTargets` may unbind them) or stream-out targets; only PS SRV and
  sampler slot 0 are touched, so other slots need no restore.
* Only one swap chain is composited: the first presenting one, then a chain created on the same window (issue 3017).
* Untested: multi-adapter, the D3D debug layer, coexistence with the Steam
  overlay or RenderDoc, the real game.
