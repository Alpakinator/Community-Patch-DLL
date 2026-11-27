# The renderer inside the server: thread model, event feed, lockstep, hand-over, depth (epic 3300 design)

Written 2026-11-13 by the team-3300 lead before wave 1, from decision D81 points 1-2, the Architect's
`research/07-architecture-decisions/architect-2026-11-12-renderer-scope.md` 3.1-3.2, `lockstep-probe.md` (9039),
`proxy-route-compositor.md` (9040) and `shared-texture-and-compositor.md` 12-15.

**Status (lead, 2026-11-13 17:25): built and measured.** The design sections (1-4.3, 5-10) were written before any
code. Each wave's as-built and measured sections follow them and are current where they differ: 1.1-1.2 (3301), 2.1-2.2
(3303), 3.1 (3304), 4.4-4.6 (3302), 4.7 (3306), 4.8 (3308), 4.9 (3307), 4.10 (3310), 4.11 (3311), 4.12 (3312), 4.13
(3313).

**Independent verification (one-off 9055, 2026-11-13):** `renderer-in-server-verification.md`. It confirms A, B, C (as
stated, with a caveat: every frame-time figure here drops frames over 100 ms, which hides a 2-Hz ~150-ms stall of the
product path that is not the renderer's, its V1), D, E, and the render thread's isolation; it corrects 4.11's reading of the
composite's `late` counter (its 4.1) and lists the stale rows of section 5 (its 8). Both fixed by issue 0150
(2026-11-13): the counter is split in two (3.1, Records) and section 5 is corrected.

The contracts are committed as headers:
- `shell/common/vp_frame.h`: the frame contract;
- `shell/frame/frame_sink.h` and `frame_source.h`: the two sides of the hand-over;
- `server/src/feed/srv_feed.h`: the event feed;
- `server/src/render/srv_tile_source.h`: the seam with epic 3400, agreed and implemented by 3308.

Marks: `[C]` read in code or a file, `[L]` measured live (as the cited file reports), `[I]` inferred or designed.

## 1. Thread model of server.exe with the renderer

Today in remote mode [C, `server/README.md`]: the main thread attaches and serves; one service thread per vpipc
channel (ui, core, aux; 64 MB stacks) runs the engine's calls into the DLL; `ICvGame1::Update` runs on the core
channel's thread between `SrvRt::EnterUpdate` and `LeaveUpdate` (`server/src/remote/srv_rt.cpp`).

Added by this epic:
- **One render thread** (`vp-render`), started at `kRt_Boot` when `server.cfg` has `render=1`, stopped before the
  report at `kRt_Shutdown`. It owns the D3D11 device and its immediate context alone (no other thread touches D3D in
  server.exe), the terrain scene (`c5r::terrain`), the version-3 section (camera, height-field tap, bake tap), the frame
  section of `vp_frame.h` and the hand-over sink.
- **It never calls into the DLL and never reads a DLL object.** Enforced by construction: the render sources
  (`server/src/render/`) are compiled without the fork's include path, so a DLL type cannot even be named there; the
  feed header has no DLL type either. Reason: bug 0103 (a read with a lazy side effect changed the game) and the
  parity the D10 gate rests on.
- **Wake-up by event, not by timer or polling.** The render thread waits in `WaitForMultipleObjects` on `<base>.Cam`
  (set by the composite after each camera publish) and a stop event, with a 100 ms timeout for housekeeping (taps,
  heartbeat). No waitable timer, no sleep loop: 9039 saw the old server's 0.5 ms timer loop fall to 64 Hz for five
  minutes (bug 0118) [L, lockstep-probe.md 3.2]. Exception for the transition: while the composite has never written
  the publish block (a composite without 3304, which publishes at Present and sets no event), the render thread polls
  the camera block on shell_server's 0.5 ms timer, counted as legacy pacing; the same poll runs while the composite
  presents without publishing and ends at its next publish (issue 3312, 4.12). The render thread runs at `THREAD_PRIORITY_ABOVE_NORMAL` and opts
  itself out of execution-speed throttling (`SetThreadInformation(ThreadPowerThrottling)`); the process-wide opt-out is
  bug 0118's (bugfix team) and is used when it has landed.
- **A renderer that FAILS ends the game by D125's route (corrected 2026-11-20, ticket 0600, decision D213; replaced
  "Failure inside the render thread never ends the game", which left the player in a game with a grey map).** An
  exception on the render thread is still caught at the loop
  (SEH + C++; it must never reach the process's `SetUnhandledExceptionFilter(Unhandled)` of `RemoteMain`, which ends
  the session with exit 2 [C, srv_remote_main.cpp]), logged with its address, and the renderer goes to `VPFS_FAILED`;
  then, once, the render thread calls `HostConfig::fatal` (server.exe: `SrvRemote::ReportAndExit(5, ...)`: the
  server's report, then the proxy's report and the patch's popup, the game ends), at init and in play alike. A device loss
  the renderer recovers from ends nothing; D132 R6 (a single hand-over's fault in play is logged and counted) is unchanged.
  Every failure mode and its route: [renderer-failure-modes.md](renderer-failure-modes.md). A lost device
  (`DXGI_ERROR_DEVICE_REMOVED`/`RESET`) is rebuilt inside the process: release everything, back off 1, 2, 4 ... 30 s,
  create the device again on the same adapter, re-open the shared textures, re-apply the height field from the
  render host's own copy; state `VPFS_REBUILDING` meanwhile; after 3 lost devices in 5 minutes `VPFS_FAILED`
  (`VPFSF_REBUILDS`).
- Memory: the renderer moves `shell_server.exe`'s footprint into server.exe (1,227 MB private, 873 MB video at the load
  view on the huge save [L, proxy-route-compositor.md 3]; mostly the height field copy and the scene). The x64
  process has the room; acceptance E checks that it does not grow per turn.

### 1.1 As built by issue 3301 (W1, 2026-11-13)

Code: `server/src/render/` (`srv_render_host.*` the thread, device, loop and failure policy; `srv_v3_sink.*` the
version-3 keyed-mutex sink; `srv_render_scene.*` the terrain scene; `srv_exe_cache_tap.*` the bake tap as the first
`TileCacheSource`; `srv_render_common.*`), `server/src/remote/srv_remote_main.cpp` (start at `kRt_Boot` after the DLL's
context registered, `HostStop` before the report at `kRt_Shutdown`, `HostAbandon` on every `ReportAndExit`),
`server/build_server.py` (c5render via `render/build.py --target c5render`, render units, D3D libraries),
`server/run_server.py --render / --render-opt`, tests `server/tests/render/`. Ported, not moved: the scene, TapPoll,
BakePoll, EnsureDepth, PublishDepthBlock, the keyed-mutex publish and the dumps come from `shell/server/shell_server.cpp`
at b4f7748 (origin named in each file; shell_server.cpp unchanged). Marks as in the header of this file.

- **What differs from shell_server.exe --scene terrain** [C, code]: the scene maps the version-3 section itself, so the
  v3 and the ring sink see the same camera and taps; the sink owns colour and depth targets (`frame_sink.h`), the host
  clears and draws; no `--rate` cap, no `--tap-file` / `--save-tap` / overlay grid (test-only options of shell_server).
  A draw error abandons the frame (the last published one stays, shell_server's rule). A v3 sink re-opened in the same
  process (device rebuilt, adapter followed) continues the texture and depth generations from the section: the composite
  re-opens a texture only on a new generation, and the names `<base>_<pid>_<generation>` must not repeat.
- **Configuration**: the render_* keys of server.cfg (`server/README.md` "Renderer"; design section 5 plus
  `render_texture`, `render_log`, `render_depth`, `render_gpu_sync`, `render_size`, the scene's options and the test
  switches). `ParseHostConfig` refuses any malformed or out-of-range value with the key; server.exe then reports
  `render: LOUD: not started: server.cfg KEY=VALUE: ...` and the game goes on. Defaults: `render_exe` =
  `<game_dir>\CivilizationV_DX11.exe`, `render_log` = `<run_dir>\render.log`, `render_handover` = `v3`.
- **Adapter**: `render_luid`, else the composite's `clientLuid` from the version-3 header, else the hardware adapter with
  the most dedicated video memory (shell_server's rule). When a composite later reports another adapter the device is
  re-created on it once per LUID (not counted as a lost device).
- **Logs**: everything into `render.log` (per-second lines `terrain 1s:` and `host 1s:`, memory every 10 s); notes and
  LOUD lines also into server.log with the prefix `render: `; the report gets one summary line (state, draws,
  published, busy, lost devices, rebuilds, exceptions, cameras skipped, waits, feed drains / events / changed plots).
  *Added 2026-11-14: `terrain 1s:` ends with ` | lod L= mip= step= tess= min= value= visible=` (issue 0191) and
  ` | pass p0= p1= spec= list_p0= list_p1= list_nocache= list_p0_hash=` (issue 0189: the pass per tile and its count over
  the EXE's visible list, terrain-surface.md 8); the terrain init line ends `distance fog 0|1, spec map 0|1`.*
- **Delay-load**: `d3d11.dll`, `dxgi.dll`, `d3dcompiler_47.dll` are delay-loaded and bound at the renderer's start
  (issue 9008's rule for LLD's delay-load thunk): a server with `render=0` never loads them.
- **The other live server**: `HostStart` refuses a section whose `<base>.Frame` server block or version-3 header names
  another live pid, before writing anything into it; the sinks check again at `open()`. A `VPFSF_SECTION` failure never
  writes the state into a block that may belong to another server.
- **Lost device** (forced by `render_test_device_lost=N`, every N-th draw): `ReleaseDevice` (scene, sink, `ClearState`,
  device), state REBUILDING, back-off `render_rebuild_backoff_ms` (default 1000) doubled per loss inside the window up to
  30 s, at most 6 creation attempts; the third loss inside 5 minutes is FAILED (`VPFSF_REBUILDS`).
  `VPFrameServer::deviceLostTotal` counts losses.
- **Exceptions**: the thread runs `RunCpp` (try / catch of every C++ exception) inside `RunSeh` (`__try` / `__except` with
  a filter that records code, address and module); either makes the renderer FAILED (`VPFSF_EXCEPTION`), the D3D objects
  are released inside another `__try`, and the thread waits for the stop without a heartbeat.
- **`__HrLoadAllImportsForDll` compares the DLL name case-sensitively** [L 2026-11-13]: `"d3dcompiler_47.dll"` returned
  0x8007007E (ERROR_MOD_NOT_FOUND) although the DLL is delay-loaded; the delay-import table names it
  `D3DCOMPILER_47.dll` (`llvm-readobj --coff-imports`; lld-link's `/DELAYLOAD:d3dcompiler_47.dll` itself matched
  without case). Bind by the table's exact spelling.

### 1.2 Measured (issue 3301, 2026-11-13 07:38-07:48 EST, workstation, no timing claim)

Builds: `server/tests/render/build_render_tests.py --out D:/c5-64bit-work/3300/w1/rt` (clang-cl, c5render from
`render/build.py --target c5render`; the render objects of server.exe without the DLL; suite at a07ed86) and server.exe from this branch at commit 176d55e
(build id "repo 176d55e70 dirty=1": the issue log only) (`build_server.py --src` 9036's plain copy of the fork at 76992d235, `--deps` 9036's build_all,
release, offline, 54 s with c5render; server.exe 63F8826A..., proxy B7C91B65...) beside the x64 DLL of the pin layout
`x64-76992d23-p1607d42/bin` (FA75F483..., COMMON 18). Logs and runs: `D:/c5-64bit-work/3300/w1/` (`tests-r2.out`,
`tests-r5-asan.out`, `game-r2.out`, `drive-r2/`; the r2 and r3 run folders were removed, the final run r4's are kept:
`b-r4/`, `bin-r4/`, `tiny50-r4-render{0,1}/`, `tiny5-guard-r4/`, `game-r4.out`, and `tiny50-h1-hugefield/`,
`game-h1.out`, `parity-*/`, `tests-r7.out`, `tests-r8-asan.out`).

- **Suite** [L]: `server/tests/render/test_render_host.py`, 35 passed, 0 skipped (66.7 s), every process on a hidden
  desktop: frames per camera (40 camera blocks -> 40 draws -> 40 published; the consumer's frames carry
  `frameCounter == depth frameCounter`, `camFrameStamp` of the drawn camera, terrain over 30 % of the view, depth in (0, 1)
  where drawn and exactly 1.0 elsewhere), a static camera drawn once, event pacing, feed drained (500 events), resize
  (400 x 300, generation 2), device lost and rebuilt (state READY again, `deviceLostTotal` 1, the kept field re-applied),
  FAILED on 3 losses / a C++ exception / an access violation / a missing EXE with the process alive (exit 0, no heartbeat
  after FAILED), 16 refused configurations (exit 2, nothing written), a second server on one section refused (exit 3),
  a tap with a wrong hash refused (ackResult -1), the ring hand-over (CPU sync, 4 slots: READY slot records per
  published camera, drawResult OK, latestFrameId following), `run_server.py` render keys, and the build check (no DLL
  header in `server/src/render`; with `render_compile_args` a probe including `srv_common.h` fails "file not found").
  **Under MSVC `/fsanitize=address`** (`build_render_tests.py --asan`: cl 14.43, c5render from `render/build.py --asan`,
  clang_rt.asan_dynamic-x86_64.dll linked) the same 35 passed, 0 skipped (08:01:12-08:02:25), 0 ASan reports; a canary
  (one byte past a heap block) built with the same flags and run with the same `ASAN_OPTIONS log_path` wrote its
  heap-buffer-overflow report (`D:/c5-64bit-work/3300/w1/canary/`). **Final suite at ab5e848** (08:14-08:18): 39 passed,
  0 skipped, plain (84.8 s) and under ASan (90.2 s, 0 reports); the four added tests drive the EXE-cache tap with a
  bakefake ported from shell_server (`server/tests/render/bakefake.cpp`): typed and typeless caches drawn red / blue on
  their tiles and the manifest acknowledged, a missing slot counted and drawn with the fallback, a manifest of version 2
  refused. They found that the tap's counters were read after the source was released (0 in the totals line); the scene
  now keeps them across a release. Two findings of the ASan build: MSVC refuses `__try` in a
  function whose `__except` body makes a temporary `std::string` (C2712; clang-cl accepted it), so the handler body is a
  function of its own; and c5render's ASan build keeps the STL's container annotations, so a test program built with
  `_DISABLE_VECTOR_ANNOTATION` / `_DISABLE_STRING_ANNOTATION` fails to link against it (LNK2038 'annotate_string').
- **The game does not change** [L]: `proxy/tools/run_proxy_game.py --mode proxy --fail-on-unexpected`, Tiny
  (`server/specs/continents-tiny-fixed-seed.json`, 0 -> 50), `render=0` against `render=1` with
  `render_exe` = the Ghidra project's copy of the EXE and a section of its own: **IDENTICAL**, 9,257 rows each; both PASS,
  0 LOUD lines in server.log. During the render=1 game `drive_fake_composite.py` wrote a synthetic tap and 237 camera
  blocks (30 Hz) and `fake_consumer.exe` took 80 frames through the keyed mutex (every one terrain over at least 78 % of
  the view, camFrameStamp among the written cameras): the render thread drew and published 237 frames (0 busy, 0 errors,
  156 overwritten unconsumed), drained the feed 861 times (13,783 events, 2,115 changed plots), and stopped at
  kRt_Shutdown before the report (state STOPPED). One run per arm; wall times 23.0 / 23.2 s are not a timing result.
- **Draw cost on the synthetic 6 x 6 map** [L, render.log per-second lines]: GPU 0.234 ms per frame (max 0.70), CPU
  0.04-0.08 ms, 9 tiles, at 320 x 240. Not the huge save; not a frame-time result.
- **Memory of server.exe with the renderer** [L, one run each, Tiny]: peak working set 166 MB (render=0) and 208 MB
  (render=1, drawing the synthetic map); the render thread's lines: private 106.8 MB right after the device was created
  (before the field), 198.9 / 237.8 MB at 10 / 20 s into the game (the DLL's game state grows too: not attributable),
  video local 44.5 MB while drawing, 0 after the release. The huge save's numbers belong to acceptance E (wave 2).
- **The final build** [L 08:18-08:21, run r4]: server.exe 7594FA4F... (proxy 592AE45D...) from commit ab5e848: the same
  results (Tiny 50 render=0 / render=1 IDENTICAL, 9,257 rows; 236 frames drawn and published for the fake composite, 80
  taken; 0 LOUD; premise-6 run PASS, device on the RTX 4080 1.5 s after the start; peak working set 166 / 204 MB). Run r3
  (08:03, server.exe CC61056A..., commit c8bb790) gave the same and is the build of the huge-field run below.
- **server.exe with the huge field** [L 08:06:48-08:07:10, `D:/c5-64bit-work/3300/w1/game-h1.out`, run h1]: the same
  server.exe during a headless Tiny 50 game (PASS, 0 LOUD) drew epic 3000's huge ground-truth field
  (`D:/c5-64bit-work/3200/w2/gt/huge_3004.c5hf`, 128 x 84 plots, 88,080,512 bytes, no material layer) at the ground-truth
  camera `v1_city_default_after` for the fake composite at 2554 x 1411 (`drive_fake_composite.py --tap-file --gt-dir
  --request`): tap read, hashed, copied and acknowledged in 181.6 ms; field built as 64 x 42 tiles of 2 plots in
  1,021.8 ms (no bake tap, so the default tile size; shell_server in 9040 built 128 x 84 tiles of 1 plot in 2,503 ms);
  236 frames drawn and published (29 tiles drawn, 2,659 culled, all without a cache: fallback material), 140 taken by
  the consumer, terrain over 100 % of the view. Memory: private 108 MB at the renderer's start, 180 MB 10 s later
  (before the tap; the game's growth), **877 MB private / 306 MB working set / 590 MB local video while drawing the huge
  field**, 409 MB after the renderer's release (the kept 88 MB field copy and the heap); peak working set 344 MB against
  166 MB with render=0. For comparison shell_server.exe held 1,227 MB private / 873 MB video at the huge load view with
  1-plot tiles and the tapped caches (proxy-route-compositor.md 3). GPU per draw 0.86-2.11 ms (per-second means) with
  a game running beside it: not a timing result.
- **Pixel parity with shell_server.exe --scene terrain** [L 08:09-08:10, `server/tests/render/parity_shell_server.py`,
  `D:/c5-64bit-work/3300/w1/parity-{synth,tiny,huge}/parity.json`]: the same camera block and tap given to
  shell_server.exe (`--scene terrain --depth 1 --dump`, W4's build of the team branch, shell_server.cpp unchanged,
  B73A9867...) and to render_host_test.exe (the render objects of server.exe, F2CE8AAD..., built at a07ed86; the draw path
  is unchanged since) on sections of their own: the first frame's colour and depth are **byte-identical** in all three
  cases: the synthetic 6 x 6 field at 320 x 240 (76,800 pixels, terrain over 78 %), Tiny's ground-truth field
  (`tiny_3004.c5hf`) at the ground-truth camera `v1_city_default_after` at 2554 x 1411 (3,603,694 pixels), and Huge's
  (`huge_3004.c5hf`) at its `v1_city_default_after` at 2554 x 1411. So the v3 sink hands today's composite the picture
  shell_server.exe hands it, at least for the first frame of a load without the bake tap (D66: the rendering goal is
  unchanged by the move).
- **Legacy pacing rate** [L]: the 0.5 ms high-resolution timer wait (`WaitForMultipleObjects` on the timer and the stop
  event) completes about 800-860 times a second (`waits_legacy`), i.e. about 1.2 ms per poll, with the bug 0118 opt-out
  in force (process control 0x5 state 0x0 read back). shell_server's loop uses the same wait; the event wake-up of
  3304's composite replaces it.

Premise 6 (section 8) [L 07:47:45]: server.exe started by the proxy with `CREATE_NO_WINDOW` and `CIVBG_GUARD=1` (guard
installed, hooks 0x31f, self-test ok) created its D3D11 device on the RTX 4080 (LUID 0xE0D7, feature level 11_1) 1.3 s
after its start; the guard log (`%TEMP%/civbg_guard_60012_20261113-124744-632.log`) has no window event after the
install, server.log 0 LOUD lines, the Tiny 5 game PASS. In the headless game there is no EXE: the adapter was the one with
the most video memory; the composite's adapter (clientLuid) path was exercised by the fake consumer (same LUID).

## 2. The event feed (`server/src/feed/srv_feed.h`)

- **Capture.** The DLL's notifications to the engine (`GameplayPlotStateChange`, `GameplayFeatureChanged`,
  `GameplayFOWChanged`, `GameplayCityCreated/Destroyed/Captured`, `GameplayNaturalWonderRevealed`,
  `GameplayWallCreated`, `GameplayYieldMightHaveChanged`, `UpdateCountryBorder`) are DLL -> engine calls; in remote mode
  they pass through the server's `RemoteGen_ICvEngineUtility4` and `RemoteGen_ICvUserInterface2` classes, which the
  hand-written `RemoteEngineUtility` / `RemoteUserInterface` in `server/src/remote/srv_remote_objects.cpp` already
  subclass [C]. The override pushes one `SrvFeed::Event` and then forwards unchanged (the crossing to the EXE, its
  policy and its timing do not change). Plot coordinates come from the plot the DLL passes, read with its own getters on
  the DLL's thread inside the DLL's call (GetX/GetY-class getters on the D30 purity list only).
- **Plot records.** One `SrvFeed::PlotRecord` (16 bytes: terrain, plot type, feature, route, resource, improvement,
  owner, flags, fog of the active team) per plot. A whole table is taken after a load at one defined point of the
  DLL's thread: `SrvRt::LeaveUpdate` of the first `ICvGame1::Update` after a load (the core thread, inside the
  dispatch, after the DLL's Update returned), through getters of `proxy/model/pure-methods.tsv` only; then records are
  patched from the events (the event carries the new values or names the plot whose record the override re-reads
  with pure getters, still on the DLL's thread). Where the stock notifications carry too little (no call for a
  terrain-type or plot-type change: the hills-into-mountains bug [C, asset-pipeline.md 4.1]), the record is refreshed by
  pure getters at the same LeaveUpdate point, never from the render thread (Architect 3.1).
- **Queue.** One SRW lock; writers append (any DLL thread), the render thread swaps the vector out once per loop
  iteration (`Drain`). A cap (65,536 events between two drains) drops the excess and records a `kOverflow` event; the
  reader then re-reads the whole table. The feed is off unless `render=1` (writers return at once).
- **Consumer column.** `proxy/model/policy-table.tsv` gets a last column `consumer` (EXE, renderer, both), generated by
  `proxy/gen/classify.py` from a hand-written `proxy/model/consumers.tsv` (method, consumer, reason). In this epic every
  renderer-relevant notification is `both`: the EXE still draws units, cities, improvements and fog in increment 2,
  *(2026-11-14, issue 3957: since epic 0168 the server draws the fog of war of the ground from the feed's
  `PlotRecord::fow` (bit 0 revealed, bit 1 visible, the active team's), mapped to the EXE's classes 3 / 2 visible,
  1 seen, 0 unexplored, by the EXE's own FOWDrawMask and FOWBaseLayer programs (`fog-of-war.md` 5.2, 7); the feed and
  `GameplayFOWChanged` are unchanged and still cross to the EXE, which keeps its minimap fog and draws its models by
  the DLL's visibility; the EXE's FluidFOW stays off under D33)*
  so nothing stops crossing. A row becomes `renderer` (and stops crossing) only when the server draws that system
  alone; that is a later epic's decision, recorded in that file.
- **Proof that the feed does not change the game.** A headless proxy-route game (`proxy/tools/run_proxy_game.py`, Tiny
  spec, 50 turns) with `render=1` against `render=0`: `stats.db` identical (`server/tools/compare_stats.py`), and the
  same for the huge save over 5 turns. This is the feed's main product-level test (0103's lesson).

### 2.1 As built by issue 3303 (W3, 2026-11-13)

Code: `server/src/feed/srv_feed.cpp` (queue, table, field patches; no DLL header, unit-tested alone),
`server/src/feed/srv_feed_server.h` (writer-side additions: `UpdatePlot` with a field mask, `GetPlot`, `GetSize`,
`Counters2`, `ResetForTest`), `server/src/remote/srv_remote_feed.cpp/.h` (the DLL-side capture), the overrides in
`server/src/remote/srv_remote_objects.cpp`, one call in `SrvRt::LeaveUpdate` (`srv_rt.cpp`). Marks as in the header
of this file.

- **Notifications taken** [C, code]: `RemoteEngineUtility` overrides GameplayPlotStateChange, GameplayFeatureChanged,
  GameplayFOWChanged, GameplayCityCreated/Destroyed/Captured, GameplayNaturalWonderRevealed, GameplayWallCreated,
  GameplayYieldMightHaveChanged; `RemoteUserInterface` overrides `ICvUserInterface1::UpdateCountryBorder` (the DLL calls
  it from `CvPlot::setOwner`, CvPlot.cpp:7133-7137 at 76992d235 [C]; only the `CvDLLInterfaceIFaceBase` twin is never
  called: the header's earlier comment "the DLL never calls it" was wrong and is corrected). Each override calls
  `SrvRemoteFeed::On*` and then the generated base method with the same arguments, so the crossing is unchanged.
- **What the DLL passes** [C, CvPlot.cpp:13917-14058]: `GameplayPlotStateChange(plot, resource, improvement,
  improvementState, route, routeState)` carries the ACTIVE TEAM's view: `resource = getResourceType(activeTeam)` (none on
  a city or an unexplored plot), `improvement = getRevealedImprovementType`, state 1 half-built / 2 built / 3 pillaged,
  `route = getRevealedRouteType`, routeState = the DLL's `eRoadTypes` (0 road, 1 railroad, 2/3 pillaged, 4/5 under
  construction, 6/7 trade route, 8 none). It is sent from `CvPlot::updateLayout`, i.e. only when the engine calls
  `ICvMap1::UpdateLayout` (dirty plots) or `ICvPlot1::UpdateLayout`. `GameplayFOWChanged(x, y, mode, false)` comes from
  `CvPlot::updateFog`, which returns at once while `GC.IsGraphicsInitialized()` is false (CvPlot.cpp:504-519).
- **Pure getters only** [C, `proxy/model/pure-methods.tsv`]: plot reads use ICvPlot1 GetPlotIndex, GetTerrainType,
  GetPlotType, GetFeatureType, GetImprovementType, GetOwner, IsRiver, GetActiveFogOfWarMode (only when
  `ICvGame1::GetActiveTeam() >= 0`: the DLL's PRECONDITION); ICvMap1 GetPlot(x, y), PlotNum; ICvGameContext1 GetMap,
  GetFeatureInfo; ICvFeatureInfo1 IsNaturalWonder; ICvCity1 GetOwner, GetID; Destroy. The grid is derived without the
  unlisted GetGridWidth/GetGridHeight: width = `PlotNum(0, 1)`, height = the first row at which `GetPlot(0, y)` is NULL
  (no Civ V map wraps in y; an index check stops a wrap). **Not on the list, so not used** (wanted for wave 2): ICvMap1
  GetGridWidth / GetGridHeight / GetPlotByIndex; ICvPlot1 GetResourceType / GetRouteType / IsImprovementPillaged /
  IsRoutePillaged / IsCity / GetRevealedImprovementType; ICvCity1 GetPlot. Consequences: city events carry
  no plot (`plot = -1`), `PlotRecord.flags` bit 4 (city) is never set, and resource, route and the pillage bits come only
  from GameplayPlotStateChange.
  **Correction 2026-11-23 (ticket 5062, W107; lead-0042 #12015):** ICvCity1 GetX / GetY were listed here as not used.
  The feed now calls them in one place only, `OnCombat` for a GameplayCityCombat whose ATTACKER is a city, on the DLL's
  thread, to put the attacking city's plot + 1 into kind 28's `a[3]` (`srv_feed.h`). Purity audit [C, the fork at the D30
  audit's commit 1b87dd185]: `CvDllCity.cpp` 143-146 / 148-151 return `m_pCity->getX()` / `getY()`, and `CvCity.h`
  549-552 / 554-557 are inline `return m_iX;` / `return m_iY;`: plain member reads with no side effect. The audit is
  the server's own and is cited in `srv_remote_feed.cpp`'s header. `proxy/model/pure-methods.tsv` (the proxy's list) is
  not changed. The other city kinds keep `plot = -1`. Details: unit-combat-server.md 18.37.3.
- **The plot record** (`srv_feed.h`, 20 bytes since 3303: `improvementState`, `routeState`, `known`, `reserved1`
  appended). Two sources that never overwrite each other (a patch carries a field mask): getter fields (terrain, plot
  type, feature, owner, river, natural wonder, fog) and layout fields from GameplayPlotStateChange (resource, revealed
  improvement and its state, route and its state, pillage bits). `improvement` is the revealed one once a
  GameplayPlotStateChange was seen for the plot (`known` bit 1), the getter's before. *Added 2026-11-23 (ticket 0631,
  live-terrain.md 8.18): the old `reserved0` (offset 11) is `riverEdges`, the plot's own three river edges from the
  pure getters IsWOfRiver / IsNWOfRiver / IsNEOfRiver (added to the purity list), with `known` bit 2; a getter field.*
- **Snapshot and refresh rule** (`SrvRemoteFeed::AtLeaveUpdate`, called by `SrvRt::LeaveUpdate` after the DLL's Update
  returned and the turn was read, on the core thread, inside the dispatch): a **snapshot** (new table, generation + 1,
  `kSnapshot` event) when there is no table, the grid changed, the turn went back or jumped by more than one (a load),
  or the queue dropped events (`kOverflow`); else, at a **turn change**, every plot is re-read and patched (this is the
  refresh for changes no notification carries, e.g. hills into mountains); else the plots named by notifications since
  the previous Update (feature, natural wonder, wall, yield) are re-read. The new table is built beside the published
  one and swapped in at `EndSnapshot` (the reader never sees a half-filled table). Layout fields are carried into a
  snapshot of the same grid (no getter can re-read them) and layout patches that arrive before the first snapshot are
  kept per plot index and applied by it. Not handled [I]: a load of a different map of the SAME size keeps the old
  layout fields until GameplayPlotStateChange replaces them. *Added 2026-11-21 (ticket 4914, epic 0615): a fourth
  path, the **rolling refresh** (K plots round robin per Update between turns, server.cfg `feed_rolling_plots`, default
  256), because a plot-type or terrain change with equal yields raises no notification: `live-terrain.md` 11.7.*
- **Off means off** [C]: every `SrvRemoteFeed::On*` and `AtLeaveUpdate` returns before any DLL call while
  `SrvFeed::Enabled()` is false (`server.cfg` `render=0` and `feed=0`, the default). Exceptions inside the capture are
  caught there (counted, logged `feed: LOUD:` at most 5 times) and never reach the DLL.
- **Per-turn counts**: the server writes `feed-per-turn.tsv` in its working folder (cumulative counts per kind, patches,
  snapshots, refreshes, plot reads and their milliseconds), one row at each turn change.
- **Consumer column** [C]: `proxy/model/consumers.tsv` lists the ten renderer-relevant notifications, all `both`;
  `policy-table.tsv` has the last column `consumer` (606 `exe`, 10 `both`, 999 `-` for engine_to_dll rows) and
  *(2026-11-16, W-4470 for epic 0375: 22 rows added, the unit and city calls of `unit-feed-contract.md` 2.1 (slots
  146-158, 160, 161, 165-170) and `GameplayEraChanged` (144), all `both`; the column now 584 `exe`, 32 `both`, 999 `-`;
  `apply_consumers.py --check` 0, `server/tests/feed/test_consumers.py` 20/20, its unlisted-row example moved from
  `GameplayUnitMoved` to `TryGameCoreLock`)*
  `model.json`'s dll_to_engine methods a `consumer` field. Written by `proxy/gen/apply_consumers.py` (validation shared
  with `classify.py`, which writes the same column when it runs). **classify.py cannot regenerate the committed table**
  [L, 2026-11-13]: its toolchain names the fork clone `%USERPROFILE%\Documents\GitHub\Community-Patch-DLL-x64` at
  e9d0c4837, and the committed table carries 9029's hand edit of the TryGameCoreLock row (`server_local`, upstream fix
  6cc672569); a full run reverted exactly that row and nothing else.

### 2.2 Measured (issue 3303, 2026-11-13 06:30-06:35 EST, workstation, no timing claim)

Build (run `r3`): server.exe D61BA7E4... and proxy 100D95ED... from this branch at commit b8242e4
(`server/build_server.py --src` 9036's plain copy of the fork at 76992d235, `--deps` 9036's build_all, release,
offline, 41 s); the x64 DLL of the pin layout `x64-76992d23-p1607d42/bin` (FA75F483..., COMMON 18) beside that
server.exe in one bin folder used by both arms. Runs and logs: `D:/c5-64bit-work/3300/w3/` (`run_final-r3.log`,
`tiny50-r3-render{0,1}/`, `huge355-h-r3-render{0,1}/`, `compare-*.{txt,json}`; an earlier build `r1` of 06:23, before the
refresh counters, gave the same results). In this branch `render=1` turns on the feed only (the render host of 3301 is
not in it).

- **Determinism proof** [L]: `proxy/tools/run_proxy_game.py --mode proxy --fail-on-unexpected --server-cfg render=R`,
  R = 0 then 1, headless 32-bit host. Every run PASS (host exit 0, 0 Lua errors, server `result: passed`, 0 unserved,
  0 LOUD lines in server.log and vpproxy.log). `compare_stats.py` render=0 against render=1:
  - Tiny (`server/specs/continents-tiny-fixed-seed.json`, 0 -> 50): **IDENTICAL**, 9,257 rows each (2300's row count);
  - huge save (`VP8P-HUGE_0350` copy, SHA-256 c499d3c6...5188c3, W3's own copy of 9020's copy; 350 -> 355; mods from the
    pinned fork copy as 2300's `huge360`, `--extra-c5h=--no-addins`): **IDENTICAL**, 62,453 rows each.
  One run per arm; wall times (Tiny 26.5 / 25.8 s, huge 56.4 / 62.3 s) are not a timing result.
- **What the feed saw** (`feed-per-turn.tsv` of the render=1 runs; a row holds the counts at the end of its turn) [L]:

  | | Tiny 0 -> 50 (56 x 36 = 2,016 plots) | huge 350 -> 355 (128 x 80 = 10,240 plots) |
  |---|---|---|
  | snapshot | 1 at turn 0, 2,016 reads, 0.17 ms | 1 at turn 350 (after the load), 10,240 reads, 0.81 ms |
  | full refreshes (turn changes) | 49; median 0.21 ms each | 4; median 1.5 ms each |
  | dirty refreshes | 92 | 46 |
  | GameplayPlotStateChange | **0** | **0** |
  | GameplayFOWChanged | 10,264 (2,047 by the end of turn 0; median 126 per turn) | 27,347 (11,779 in the load turn; median 1,430 per turn) |
  | GameplayYieldMightHaveChanged | 2,866 (2,685 at turn 0; median 3) | 2,446 (median 392) |
  | GameplayFeatureChanged | 501 (494 in map generation, before the snapshot) | 11 |
  | UpdateCountryBorder | 136 | 224 (median 46) |
  | GameplayCityCreated / Captured / Destroyed | 15 / 0 / 0 | 127 (124 in the load: the DLL announces every city again) / 3 / 0 |
  | GameplayWallCreated / NaturalWonderRevealed | 0 / 0 | 125 (121 in the load) / 0 |
  | records a full refresh found changed (no notification had patched them) | 1 (improvement) | 3 (improvement) |
  | dropped events, exceptions | 0, 0 | 0, 0 |

  Readings: **GameplayPlotStateChange never fires in a headless game**: the headless host never calls
  `ICvMap1::UpdateLayout`, so resource, route and the revealed improvement stay unknown there; only the live EXE
  shows their rate (not measured: no live run in 3303; *corrected 2026-11-15, epic 0166 (terrain-decals.md 5.F, P15)
  [L]: measured live since, by 3306 r2 and 3308 r1 on the huge save (9,966 at the load, then 7-236 per AI turn, product
  path in 3308) and by 4109 on a Tiny copy on the product path (34 at the load, then +11 / +4 / +1 per AI turn, 0 LOUD);
  it describes the active team's revealed state only and comes before the EXE lays out the decal (P13), so the decal
  tap's generation, not this notification, is the decal re-bake signal. Replaced: "not measured: no live run in
  3303"*). Every change of terrain, plot type, feature, owner, river,
  natural wonder and fog that occurred was carried by a notification (the full refreshes found 0 of them); the only
  changes the refresh rule caught were improvements, which the DLL announces only through GameplayPlotStateChange.
  The last turn of both runs carries a burst (Tiny 2,127 FOW events and 1,933 changed records in turn 49; huge 11,428
  and 9,506 in turn 354): the end of the run [I, the host's shutdown], not play.
- **Premise 5** (the overrides see the plot through pure getters) [L]: confirmed for every plot-carrying notification
  that occurred (`GetPlotIndex` on the DLL's own wrapper inside the call); `GameplayFOWChanged` carries x, y and is
  mapped with the snapshot's width.
- **Pair test** [L]: `proxy/tools/hutest/build_hutest.py` with `feed=1` in its server.cfg; the fake DLL sends the ten
  notifications in one order with known arguments and the fake engine checks them (`feed_notifications_forwarded`:
  `ps:77:11,12,3,1,6;fc:77:17;fow:5,6,2,1;cc:505:4;cd:505:3;cx:505:2;nw:77;wall:77;yield:77;border:77;`): passed, and
  failed as expected under `--break` (routeState perturbed). **Finding** [L]: `GameplayNaturalWonderRevealed(ICvPlot1*)`
  is not in the proxy's borrowed-argument list (issue 2800, proxy-design 17.4): the proxy logs `LOUD ... is not in the
  borrowed-argument list` for it, so a live game in which the DLL reveals a natural wonder through the proxy writes a LOUD
  line (acceptance A counts them). The EXE's handler has not been read; named to the MC for an id (proxy owner).

## 3. Lockstep: one frame number for the EXE's frame and the server's picture (`shell/common/vp_frame.h`)

The protocol is written step by step at the top of `vp_frame.h`; the reasons:

- **Publish point: the UpdateViews hook of 9039**, not Present. The probe's MinHook detour on
  `d3dx9_42!D3DXMatrixInverse`, filtered to the call at return address 0x51E1C8 with `pOut == &g_InvView[0]`, fires
  once per map frame on the main thread, after view 0, the projection, the wrap views and `g_NumViews` are final, in
  100 % of map frames of both probe runs [L, lockstep-probe.md 1.1, 2, 3]. After the original returns, the composite
  reads the camera (the existing validated read of `exe_camera.h`), sets frame id F greater than every earlier F (as built: max(previous F + 1, presentCount + 1); corrected 2026-11-13 after 3304), writes the
  version-3 camera block with `frameStamp = F`, then the publish block (`publishQpc`, `deadlineQpc`), then sets
  `<base>.Cam`. The camera read moves from Present to this point; Present keeps only `presentCount`. When the hook is
  refused (validation), the composite publishes at Present as today with `lockstep = 0` (no wait), counted.
- **The server draws exactly the newest published camera**, woken by the event. Cameras that arrive while it draws
  are skipped except the newest (counted), so it never queues behind the EXE.
- **The insertion waits for its own frame, bounded.** (Superseded 2026-11-23 by ticket 0691, D242: the shipped
  `lockstep_mode=exact` waits with no frame-time bound; the bound below is `lockstep_mode=bounded`, kept for tests and
  A/B runs; 3.2.) After the scene-clear list (the tagged insertion of 3200), the
  composite takes the slot of F when it is ready, else waits on `<base>.Frm` until `deadlineQpc` = publish + bound.
  The bound is measured from the publish, so the EXE's own work between UpdateViews and the scene clear
  (1.0-3.4 ms median by scene [L, lockstep-probe.md 2]) counts toward it and the wait at the insertion is at most
  `bound - uv_sc`. Default 3,400 us = the old server's turnaround p99 (3.35 ms [L, 9039 3.2]); 3304 re-measures the
  turnaround with the event wake-up and the ring and sets the default from the data (acceptance B: wait distribution).
  Clamped 0..10,000 us; 0 = never wait (today's newest-picture behaviour).
- **A miss draws the previous picture** (the newest ready frame), counted with its age in frames; never an unbounded
  wait (bug 3014). (Since 0691, 2026-11-23: in `exact` mode a miss happens only at a dead or hung end, 3.2; the wait
  is unbounded in frame time but ends by the liveness checks, so bug 3014's endless wait cannot come back.) After 8 misses in a row the composite stops waiting until a frame is ready at the insertion without
  a wait (as 3014's fix), counted as `wait_off` frames.
- **The wait itself.** WaitForSingleObject's millisecond timeout overshoots by up to a timer tick (15.6 ms when the
  resolution is coarse), so the composite waits on `<base>.Frm` together with a high-resolution waitable timer set to
  the deadline (or spins the last ~0.2 ms); the contract only requires "never later than the deadline + 0.5 ms" and the
  per-frame records show it.
- **Frames without a publish** (front end, Strategic View: UpdateViews' mode-2 path does not take the hooked call
  [C, lockstep-probe.md 5]) do not wait; they take the newest frame.
- **Per-frame records** (acceptance B): the composite writes one row per Present into a ring of 16,384 rows that its
  control thread flushes to `shell_composite.lockstep.csv` (as the probe's `.lsprobe.csv`): frame id published, publish
  QPC, scene-clear QPC, wait start/end, frame id taken, take kind (exact / older / none / no publish), slot, the
  server's wake/submit/ready QPC from the slot record, camera position (pan speed). Pan lag by 9038's method stays the
  measure of what reaches the screen.

Expected cost [I, from 9039's model, lockstep-probe.md 4]: with the old turnaround, 8.5-11.3 % of city-view frames
wait 0.23-0.25 ms; sparse views would wait in most frames up to ~1 ms. The event wake-up (up to 0.5 ms), the ring
(no keyed mutex, no copy in the EXE) and the fence (no CPU wait on the server's GPU) all shorten the turnaround; the
floor of the EXE's own time is about 1.0 ms [L].

### 3.1 As built in the composite (issue 3304, W4, 2026-11-13) [C code, L stand-in; live: wave 2]

Code: `shell/composite/shell_composite.cpp` (section "Lockstep, hand-over and depth"), `shell/common/shaders.hlsl`
(`PSCompositeDepth`), ini keys in `shell/composite/shell_composite.ini` and `shell/README.md` ("Lockstep, hand-over and
depth"); stand-in seams `test_host --fake-uv` / `--depth-dump`, `shell/tests/lockstep_server.cpp`; tests
`shell/tests/test_lockstep.py`. Every key is off by default; without `lockstep`, `handover=ring` and `depth_write` the
version-3 path runs unchanged.

- **Publish point = the 9039 hook.** The probe's MinHook detour on `d3dx9_42!D3DXMatrixInverse` (UpdateViews' call for
  `&g_InvView[0]`, return 0x51E1C8, validated as before) now also publishes when `lockstep=1`: camera read, camera block
  (seqlock), publish block, `<base>.Cam`. Refused there, and published at Present instead (`VPPUB_PRESENT`, no wait,
  counted `pub_present`): the hook refused or not installed, UpdateViews on a thread that does not present the target
  (`wrong_thread`), an exception in the publish (from then on), and frames without UpdateViews (front end, Strategic
  View). With `lockstep=0` but `handover=ring` or `depth_write=1` every camera is published at Present into the publish
  block too (the ring server needs it).
- **Frame ids.** F = max(previous F + 1, presentCount + 1) at UpdateViews, max(previous F + 1, presentCount) at Present:
  F only grows and equals the presentCount of the Present that ends the frame, so `shell_server.exe`'s stale rule
  (presentCount - frameStamp > N) still holds when frames without UpdateViews come between (vp_frame.h's text says
  "previous F + 1"; a contract wording change is asked of the lead).
- **Version-3 source.** `V3Source` wraps the keyed mutex + copy behind `vpframe::FrameSource`. `newest()` = the depth
  block's `camFrameStamp` when its `frameCounter` equals the frame block's (the server writes both before
  `ReleaseSync(1)`; `shell_server.exe` only with `--depth 1`). Without that block the ids are unknown: logged once,
  lockstep misses and gives up (tested). `acquire` retries `AcquireSync(1, 0)` for at most 0.5 ms when the frame is
  announced but not yet released; the depth texture is copied in the same keyed-mutex cycle into a local R32F texture and
  used only when it belongs to the same server frame as the colour. When the depth block already names the wanted frame
  but `AcquireSync(1, 0)` still fails (the server took the texture back to copy the next camera's frame over the
  unconsumed one and waits for its GPU), the insertion retries until the deadline (11d1651; found 08:24 with two cameras
  per host frame on a loaded machine: 28 of 2,521 frames had taken a ~100-frame-old copy without waiting).
- **The wait.** `<base>.Frm` (when a server sets it; `shell_server.exe` does not) plus a high-resolution waitable timer
  in slices of at most 0.25 ms, and a spin over the last 0.5 ms; ends at the frame or the deadline. A miss counts toward
  `lockstep_giveup` only when the composite waited and the frame did not come (a frame that was ready but not drawable,
  for instance while the shared texture opens, is no sign of a slow server). Given up: no wait (`wait_off`) until the
  frame is ready at the insertion without one ("recovered").
- **Depth.** The DSV is the view of the last `ClearDepthStencilView` with the depth flag recorded in the list that the
  insertion follows (`tag_rule=after`); no reference is held: the list holds the view while it executes and the
  composite uses it only inside that `ExecuteCommandList`. Checked once per view and target size (2D, mip 0,
  single-sample, writable, the target's size), else logged and counted (`depth_refused`). One pass: `SV_Target` as before
  and `SV_Depth` from the frame's R32F (point-sampled), depth func ALWAYS, write on, stencil disabled (untouched); PS
  slot 1 and sampler 1 are saved and restored with the rest of the state.
- **Records.** `lockstep_csv=1`: one row per Present (`shell_composite.lockstep.csv`: published F and source, publish /
  deadline / insertion / wait QPC, taken id, kind exact / older / none / nopub / noins, age, slot, wait_off, depth written,
  camera compare, the server's camPublish / wake / submit / ready QPC and CPU / GPU ms from the source, camera position;
  since issue 0139 a last column `unbaked`, the taken frame's land tiles drawn without their bake, -1 = not counted: 4.17);
  a `lockstep 1s:` line per stats interval (counts, wait mean / p99 / max, late starts and overruns, give-ups, depth
  counters, the UpdateViews publish cost `uv_us_*`, the ring source's counters). **Late starts and overruns (issue 0150,
  2026-11-13, `shell/composite/lk_late.h`) [C, L stand-in]:** `late_start` counts waits that began at or after their
  deadline (the EXE reached the insertion more than the bound after UpdateViews; the loop returns at once, so the EXE
  was late, not the wait); `overrun` counts waits that ended more than 0.5 ms after max(deadline, wait start), the
  contract's real overstay, with `overrun_max_us`. They replace `late` / `late_max_us` (until 0150: end - deadline
  > 0.5 ms, which counted every late start more than 0.5 ms past the deadline as late; 9055 V4). Tested by
  `shell/tests/lk_late_test.cpp` (17 checks) and live on the stand-in by
  `test_lockstep.py::test_late_insertion_counts_late_starts_not_overruns` (insertion 5 ms after UpdateViews, server
  8 ms per frame; 2026-11-13 21:08-21:14 EST, build of this branch: waits 8, late_start 8, overrun 0, one give-up; the old
  counter would have said 8 late).

**Stand-in results (2026-11-13 07:43-07:46 EST; workstation, RTX 4080, Windows 11 26100, MSVC 14.43; build of commit
bac4209 into `D:/c5-64bit-work/3300/w4/shell-build`, plain /O2; no game, no DLL, no pins involved; not under a quiet-lock
hold, other teams' builds and games running: no timing claim) [L stand-in].** `shell/tests/test_lockstep.py`: 22 passed,
0 skipped. Host: `test_host --game --fake-exe --fake-uv --uv-work-us 1500 --vsync 0`, 1280 x 720, ~570 frames/s; server:
`lockstep_server.exe` (version 3, woken by `<base>.Cam`, GPU-synced before `ReleaseSync(1)`); figures over the frames
published at UpdateViews after the first 60 (`D:/c5-64bit-work/3300/w4/summ.py` over the pytest folders):

| Run | Frames | Exact | Older / none | Waits (ended at) | Publish -> server READY p50 / p95 / p99 |
|---|---|---|---|---|---|
| publish -> exact (`test_publish_at_updateviews_exact`) | 3,369 | 3,368 | 1 / 0 | 1 (1.84 ms, 25 us after its deadline) | 0.34 / 0.49 / 0.71 ms |
| the same with `lockstep_check=1` | 2,231 | 2,231 | 0 | 0 | 0.35 / 0.42 / 0.52 ms |
| `handover=ring`, fence, `--churn 3` (two ResizeBuffers) | 4,115 | 4,113 | 0 / 2 | 3 (max 1.53 ms) | 0.08 / 0.13 / 0.19 ms |
| `handover=ring`, cpu, `--churn 3` | 4,139 | 4,137 | 0 / 2 | 4 (one ended **0.68 ms** after its deadline) | 0.21 / 0.42 / 0.45 ms |
| server 8 ms per frame (`--delay-us 8000`) | 2,812 | 0 | 2,812 / 0 | 8, then given up (no recovery) | - |
| server stalled 1.5 s | 3,940 | 3,091 | 849 / 0 | 10; one give-up, one recovery | 0.34 / 0.51 / 0.72 ms |
| server killed after 400 frames | 2,830 | 287 | 2,543 / 0 (the last frame) | 8, then given up | - |
| **today's `shell_server.exe --scene terrain --depth 1`** (08:02, the same build, test of 9617898, the composite's synthetic tap, the EXE read for its shaders) | 3,895 | 3,675 (94 %) | 11 / 209 (before the field) | 1,852 (p50 1.07 ms, max 2.14 ms; ended at most 0.33 ms after the deadline); one give-up, one recovery | 1.58 / 2.29 / 2.51 ms |

- **Today's `shell_server.exe` works with lockstep as it is** (it draws on every new camera block and names the camera in
  the depth block), but it waits in about half the frames on the stand-in: its turnaround (0.5 ms camera polling, the
  terrain draw, the GPU wait before `ReleaseSync(1)`) has a 1.58 ms median against the 1.5 ms of host work between the
  publish and the insertion; the version-3 source sees its frame only at the next 0.25 ms poll slice (it sets no
  `<base>.Frm`). The event-woken stand-in server turns around in 0.34 ms (0.22 ms in a second run at 08:02) and the ring
  with the fence in 0.07-0.08 ms: the turnaround, not the wait logic, decides how often lockstep waits [L stand-in].
- **Waits end at the deadline**: every missed wait lasted 1.79-1.84 ms = the 3.4 ms bound minus the 1.58 ms (median)
  from the publish to the insertion; the latest end was 25 us after the deadline with the version-3 source. The ring
  source's own `waitFor` (3302) once ended 0.68 ms after its deadline (sync cpu, 1 of 4 waits) while other teams' builds
  ran, and under ASan (08:06, same arm) 5 of 74 waits ended 0.56-1.11 ms after it: over the contract's 0.5 ms. The
  composite's own loop (`LkBoundedWait`, 0.25 ms slices) stayed within 0.38 ms in 1,447 waits against `shell_server.exe`
  under ASan. **Changed 2026-11-13 08:12:** the insertion no longer calls the source's `waitFor`; it waits with its own
  loop on `newest()` and calls the ring's `poll()` in it (which gives slots back, as the ring's `waitFor` did). Why the
  ring's loop overshoots is not established [I: it sleeps one chunk up to 0.5 ms before the deadline and calls
  `serverOk()` and `complete()` in its spin; not profiled]; 3302's owner may want to look. With the composite's loop
  (ASan run of 08:16, build of 70f23fb's composite, machine loaded by two games and other teams' ASan builds): ring cpu 7
  waits, latest end 10 us after the deadline; ring fence 3 waits, 185 us; `shell_server.exe` terrain 1,887 waits, one
  ended 523 us after its deadline (the rest within 0.5 ms). But at 08:50 (plain build, two games and another team's ASan
  suite on the machine) the ring cpu arm ended 14 of 183 waits 0.6-49 ms late with the ring's `poll()` called in every
  spin of the loop, while the version-3 waits of the same run stayed in bound. `poll()` calls `GetData` on the EXE's
  immediate context; it is now called at most every 0.25 ms and never in the last 0.5 ms before the deadline (bce6889)
  [I: that `GetData` under GPU contention, not plain preemption, made the late ends is not proven].
- **ASan** (`shell\build.cmd asan` into `D:/c5-64bit-work/3300/w4/shell-build-asan`, `shell/tools/memcap.py --mb
  6144`): **no ASan report in any of the five runs** (08:06, 08:16, 08:50, 08:58, 09:01). Final: build of bce6889's
  composite, tests of 688af58, `test_lockstep.py` **23 passed, exit 0**, peak commit 876 MB (09:01-09:04).
  Final plain run (build of bce6889, 08:55): 23 passed; ring cpu waits then ended at most 1 us after the deadline (13
  waits), ring fence 1 us (3), the version-3 source against `shell_server.exe` 82 us (2,544), against the stand-in server
  447 us (189 waits, two games running).
- **Exact share depends on the machine's load** (other teams' games and builds on the same CPU and GPU): the same test
  gave 3,368 of 3,369 exact at 07:43 and 2,828 of 3,116 (91 %) at 08:14 with two games and two ASan builds running
  (the event-woken stand-in server then missed 8 frames in a row several times: descheduled). The tests therefore check
  the rule (a lockstep frame takes its own frame unless it waited to the deadline or had given up), with a floor on the
  share; timing claims belong to the wave-2 series under the quiet lock.
- **The camera read at the new point costs 4.4-5.0 us** per publish (mean per 0.5 s interval; p99 9-14 us, single
  maxima up to 76 us): camera read twice + validation + the camera and publish blocks + `SetEvent` [L stand-in: the
  fake image's globals; the game's are the same reads]. The same work at Present (hook refused, `cam_us_mean` of the
  stats line) took 4.9-6.4 us on the stand-in; in the game the read alone was 2.3 us per Present (3008, section 8 of
  shared-texture-and-compositor.md). The second run (08:02, same build): 4.1-4.5 us at UpdateViews.
- **Premise 3 on the stand-in**: with `lockstep_check=1` 0 of 2,408 compared frames differed between the UpdateViews
  read and a Present read; with view 0 moved after UpdateViews (`--fake-cam-move-after-uv`) 2,466 of 2,466 differed, in
  `view` only. The stand-in writes its globals before UpdateViews, so this proves the comparison, not the game: the live
  count is for wave 2.
- **Premise 4 on the stand-in** (`depth_write=1`, server depth 0.5 inside x < 580, the host's units list after the
  insertion with LESS, read back from the D24S8 after the frame's lists): 0.5 where the server drew (also under the yellow
  z 0.8 and cyan z 0.6 units, which lost), 0.3 under the orange unit (won), 0.6 under the cyan unit outside the region,
  1.0 elsewhere; with `depth_write=0`: 1.0 / 0.8 / 0.6 / 0.6 / 0.3 / 1.0. On screen the server's colour shows where the
  units lost. Holds on the stand-in; the EXE's own lists (LESS_EQUAL, section 12 of shared-texture-and-compositor.md) are
  acceptance D of wave 2.
- Negative paths, all with the composite going on (host exit 0, nothing disabled): no UpdateViews call site (hook
  refused, every camera at Present, no wait), UpdateViews on another thread (`wrong_thread`, published at Present), an
  access violation in the publish (`fault_test=uv`: caught, Present from then on), a server without the depth block (ids
  unknown: logged once, one give-up, depth not written, the picture still drawn), `tag_rule=inlist` with lockstep (CONFIG
  ERROR, version-3 path), `lockstep_bound_us` -5 / 99999 / abc / 12x, `lockstep_giveup` 0, `handover` bogus (CONFIG ERROR,
  clamped or default, the bound in force shown in the 1 s line and the rows).

**Not verified in wave 1, and what wave 2 needs.**
- No live run: the quiet-machine lock was held or queued by five teams through the window, so no Goldberg instance was
  claimed. Wave 2 (3306): the D33 setup of `proxy-route-compositor.md` 3 with `insert=tagged tag_rule=after
  tag_match=clear_bb lockstep=1 lockstep_csv=1 lockstep_check=1 depth_write=1` and `shell_server.exe --scene terrain
  --depth 1` (or 3301's server), huge save, under the quiet lock: the wait distribution, exact share, misses, idle and
  panning, city and sparse view (acceptance B), premise 3's live count, the camera publish cost in the game, depth at
  3200's cameras (D).
- `shell_server.exe` itself never sets `<base>.Frm`; the version-3 source then polls in 0.25 ms timer slices, so a frame
  that becomes ready during a wait is seen up to ~0.25 ms (plus the timer's overshoot) late. 3301's version-3 sink should
  set `<base>.Frm` after `ReleaseSync(1)` (the composite opens the event already).
- The ring source's colour is sampled through its UNORM view even when the back buffer's RTV is sRGB (the slot textures
  are B8G8R8A8_UNORM, not typeless); the EXE's swap chain is R8G8B8A8_UNORM [L, insertion-point-and-frame-structure.md
  table 1], so this is only a note.

### 3.2 Exact lockstep: no frame-time bound, only a dead or hung server ends the wait (ticket 0691, D242, 2026-11-23) [C code, L stand-in]

Decision D242 (the owner, 2026-11-22): the interface waits as long as necessary so that every presented frame carries
the server picture of its own camera. Design: `tracker/issues/0691-lockstep-always-exact.md` (lead-0673, approved MC
#10951; the pump of point 1 corrected after test (d) below). Code: `shell/composite/shell_composite.cpp` (section
"ticket 0691", `LkExactWait`, `LkxServerDead`, `LkxHungEnd`, `LkxSinkPoll`, export `VPShell_SetHungSink`),
`shell/common/vp_frame.h` (`VPFRAME_HUNG_MS_*`, `VPFramePublish::hungEnds` from `reserved[0..1]`, layout unchanged),
`proxy/src/proxy_shell.cpp` (the sink, passed at OnBoot), `proxy/src/proxy_rt.cpp` (`ServerHungRoute`, next to
`CallFailed`).

- **Keys** (composite ini only, not the player's options file): `lockstep_mode=exact|bounded` (default `exact`),
  `lockstep_hung_ms` (default 30,000, clamped 2,000..120,000), `hung_sink_test` (tests only). `bounded` is the 3304 rule
  (`lockstep_bound_us`, `lockstep_giveup`) unchanged.
- **The wait** (`exact`): the event + 0.25 ms high-resolution timer slices of 3.1 with no deadline. Every 10 ms the
  server's liveness. Dead (the wait ends at once, logged once per pid, `dead_ends` in the 1 s line): its process ended
  (`OpenProcess(SYNCHRONIZE)` signalled), another pid in the section than at the wait's start, ring server state FAILED
  or STOPPED, magic 0. Hung (`lockstep_hung_ms`): the heartbeat older than the limit (ring: `VPFrameServer.heartbeatQpc`;
  v3: the version-3 header's; server.exe's `V3Sink::tick` writes both with one value), or no new frame of any id for
  that long while the heartbeat is younger than 1 s (lead-0673's no-progress limit: a server that ticks but never
  delivers, e.g. a v3 server without the depth block or a ring Busy forever, would otherwise freeze the game for good).
- **A hung end**: one `LOUD: lockstep: the server is HUNG: <reason>` line, `hungEnds` (publish block) / `hung_ends`
  (1 s line), no wait until a frame is ready at the insertion again (`wait_off` frames, then "exact: recovered"), and once
  per process the proxy's sink (called on the composite's control thread, never mid-frame on the EXE's thread) ->
  `ProxyRtInt::ServerHung` -> the route of a server that failed in a game: report stage `server-hung` code `no-frame`,
  the helper's popup, `Fatal(3)` in a D33 session (D118/D125/D240). The reasons: "server drew no frame for N s
  (heartbeat age N s)" and "no frame delivered for N s, heartbeat alive (heartbeat age N s)". Since ticket 0697
  (2026-11-23): before every `Fatal`'s TerminateProcess the proxy calls the composite's `VPShell_FlushTotals` on a thread
  of its own, at most 1,500 ms, so the composite's log ends with `pacing totals: why=fatal`
  ([frame-pacing-instruments.md](../08-performance/frame-pacing-instruments.md) 5a).
- **LIVE: the first map frame of a load deadlocks** [L, 2026-11-23 02:51-02:52, c5h-c, EXE 4390913, the build of
  0141a06c6; pan-coherence.md 5.7]. The EXE's first in-game frame with a publish (frame 24) waited for the server's
  frame 24, and the server held that frame for inputs only later EXE frames bring (the feed's plot table: "fog: frames
  held from frame 24: no plot table from the feed", "residency: held frame 24 ... no table yet"; the fog install waits
  for 30 EXE Presents; the overlay and shadow taps publish at Present 24 after the insertion). After 30.0 s the
  no-progress limit ended the wait (`hung_ends` 1) and the proxy took the hung route: `FATAL: the server hung in a D33
  session`, exit code 3, no box. A player's session would get the failure popup at every load. **Fixed offline
  2026-11-23 by the server's hold report (3.2.1 below; live: the load passed 04:42); this line said "Not fixed" until then.** The stand-in
  tests (a-g) have no such dependency. With the load in `bounded` mode and `exact` switched on live in game, the
  exact wait works (pan-coherence.md 5.7: 100 % exact, `hung_ends` 0).
- **The publish block in `exact` mode**: `boundUs` 0, `lockstep` 1, `deadlineQpc` = publish + `lockstep_hung_ms`.
  The server's Busy retry (3302's rule, `srv_render_host.cpp`, `ring_producer`) runs until `deadlineQpc`: with 0 it would
  skip a camera whose slot the composite still reads, and the composite would then wait for a frame never drawn.
- **The version-3 keyed-mutex retry** (frame announced, `ReleaseSync(1)` not yet made) is capped at 100 ms in `exact`
  mode (it ran to the bound's deadline in `bounded`).
- **No message pump in the wait** (lead-0673's choice (A), 2026-11-23). The design's `PeekMessageW(&m, nullptr, 0, 0,
  PM_NOREMOVE | PM_QS_POSTMESSAGE)` every 500 ms was built and tested first, and refused [L stand-in, 2026-11-23 01:14,
  test_host on the hidden desktop, a 7-s stall, `lockstep_hung_ms=20000`; IsHungAppWindow read every 100 ms from another
  thread]: with the peek the window was marked hung at 4.92 s into the stall anyway (4.72 s without the peek), and a
  cross-thread `SendMessageTimeoutW(WM_NULL)` sent 2.0 s into the stall was answered at 2.51 s, i.e. inside the next peek:
  the window procedure ran in the middle of the frame (re-entry); `PM_QS_POSTMESSAGE` does not hold sent messages back.
  Without the peek it was answered at 7.02 s, after the frame. So a stall over 5 s shows Windows' "Not Responding"
  ghost until the frame ends; the hung limit stays 30,000 ms (the render thread's render.log stalls: 30 s in 5054's
  report, 10.97 s in its repro, 7.4 s in pan-coherence.md 3.2). Later, not decided here: `DisableProcessWindowsGhosting()`
  would suppress the ghost for the whole game process (stock's own hangs too), an MC/owner decision.

**Stand-in results** (`shell/tests/test_lockstep.py` `test_exact_*`, v3 `lockstep_server.exe`, workstation under
other teams' load, 2026-11-23; shell build of this ticket) [L stand-in]:
- (a) a 30-ms server (`--delay-us 30000`), camera panning: 194 lockstep frames after the start-up, every one its own
  camera's (0 older, 0 give-ups, 0 hung or dead ends), frame time median 31.44 ms (max 46.4 ms).
- (f) the camera at rest, the same server: 162 frames, all exact, each waited, median 31.59 ms: no exemption.
- (b) a 2-s stall, heartbeat ticking: every frame exact, one long frame of 2,002.7 ms, no hung end, no proxy call.
- (c) NEGATIVE CONTROL, `lockstep_hung_ms=3000`, a 6-s stall: heartbeat stopped (`--stall-heartbeat-stop`): one
  `LOUD ... server drew no frame for 3.0 s (heartbeat age 3.0 s)`, the sink call 160 ms later on the control thread
  (`hung_sink_test`), `wait_off` frames, "exact: recovered" 3.0 s later, exact frames after it; heartbeat alive: one
  `LOUD ... no frame delivered for 3.0 s, heartbeat alive (heartbeat age 0.0 s)`, the sink call 0.49 s later, recovered
  2.5 s later. `hung_ends` 1 in each.
- (d) a 7-s stall, `lockstep_hung_ms=20000`, no pump: a `SendMessageTimeoutW(WM_NULL)` sent 2.0 s into the long frame
  was answered at 7.01 s, when the frame ended (no re-entry); IsHungAppWindow went TRUE at 4.22 s (recorded, not
  asserted; 4.72 s in the first run).
- (e) a killed server (`--exit-after 400`): "exact: the server is dead (the server process ended; pid ...)" once, then
  2,608 dead ends in the remaining frames, each at once (every wait under 100 ms), the last frame on screen, no hung end.
- (g) `lockstep_mode=bounded`: the 3304/0150/0494/0675 cases of the file unchanged and passing; the shipped-ini ring
  case (`ring_producer`, fence) runs `exact`: 2,871 lockstep frames, all exact.
- Config: `lockstep_mode=fast` (exact, CONFIG ERROR), `lockstep_hung_ms` 500 (clamped 2000) and `x` (default 30000).
- The whole file plus `test_config_defaults.py`, `test_lk_late.py`, `test_lsprobe.py`, `test_farm_guard.py`: 75 of 77
  passed in one run (513 s); the two others (`test_ring_record_carries_the_unbaked_count[frame]`: 200 frames not reached;
  `test_unreadable_ini_is_named_once_and_readable_again`: the hook commit took 9.3 s, past its 10-s wait) passed alone
  (4 passed, 30 s): machine load, the cases test-health-on-main.md lists as load-sensitive.

**Not verified.** No live run (the laptop caplock acceptance of the ticket, F5 = 100 % in camera moves, is arranged by
lead-0673); the frame rate in the game with `exact` is not measured. The ring source's exact wait ran only in the
shipped-ini case (`ring_producer`, fast); a hung or dead ring server is covered by the code read only [C]. The proxy's
route was tested offline (`shell_unit.cpp`: the sink reaches `g_serverHung`; a composite without the export is named),
not with a live hung server.

#### 3.2.1 Held frames: the server reports a hold per frame id, the composite does not wait for it (ticket 0691, MC #11169, 2026-11-23) [C code, L stand-in]

The fix of the load deadlock above (lead-0673's option (a), MC #11169; worker-0673n).

- **Where the server holds a frame** [C, 2026-11-23]: only in `Scene::decide` (`srv_render_scene.cpp`), through
  `FowScene::admit` (`srv_fow_scene.cpp`): admit returns false (nothing drawn for that camera) while it waits for the
  feed's plot table, a table of the field's size, a revealed plot (install only) or the overlay pair (taken at an EXE
  Present). *(Corrected 2026-11-23 14:5x by 3.2.2: decide also refuses a camera for the TERRAIN FIELD (`TS_NO_FIELD`: no
  field, or the field of another load after a map change), which was not reported and deadlocked beta 3 (cand4) on a
  reload; it is now reported as `VPHOLD_NO_FIELD`. Was: "Nothing else in decide holds".)* Otherwise nothing else in decide
  holds: the shadow tap and the decals do not hold frames (they draw without their input
  until it comes), and the residency's "held frame N ... no table yet" line is the preparation of the frame the fog
  holds (`ResidencySource::prepareHeld`, ticket 0424), not a hold of its own. A fog in `kFailed` returns false for every
  frame but waits for nothing: it is NOT reported (the composite's hung route stays; with HostConfig::fatal the server
  ends first, the dead route).
- **The slot** (`shell/common/vp_frame.h`, `VPFrameServer`, from `reserved[0..6]`, size 216 and `VPFRAME_VERSION` 1
  unchanged; a server before it leaves 0 = no hold): `holdReasons` (uint32, offset 92), `heldFrameId` (int64, 96),
  `heldFramesTotal` (int64, 104); `reserved[26]` from 112. Publish block: `heldSkips` (int64, 168, from `reserved[0..1]`).
  Static asserts for every new offset. Writer order: `holdReasons`, then `heldFrameId`; a clear writes `heldFrameId` 0
  first. The reader takes the id, the bits, the id again and uses the bits only when both ids equal its frame.
- **The reason bits** (`VPFrameHoldReason`; `FowScene::HoldBits` has the same values, a static assert in
  `srv_render_host.cpp` keeps them equal): `VPHOLD_FOG_NO_TABLE` 0x1 (no plot table from the feed),
  `VPHOLD_FOG_TABLE_SIZE` 0x2 (the table is not the field's size: a new load's table not there yet),
  `VPHOLD_FOG_NO_REVEALED` 0x4 (install: no revealed plot), `VPHOLD_FOG_NO_PAIR` 0x8 (no overlay pair yet),
  `VPHOLD_FOG_INSTALL` 0x10 (with one of them: the install is pending), `VPHOLD_TEST` 0x80000000 (the stand-in only).
- **Set and cleared** (server): `FowScene::admit` clears `m_holdBits`/`m_holdFrame` at its start and sets them only at
  its final `return false` of a wait (`holdFrameId()`, `holdReasons()`). `srv_render_host.cpp` `HoldReport` writes the
  slot: after `decide` returns false (the fog's values, 0/0 = clear), cleared when `decide` admits a frame (before its
  draw), when a new composite restarts the frame ids, and at the loop's start; one note per hold episode ("frame N held
  for EXE inputs (reasons 0x...)", "hold cleared after K held frame id(s)"). `V3Sink::init` zeroes the three fields. The
  hold is written in the same pass as `decide`, before `Scene::idle` (whose `prepareHeld` may bake for seconds).
- **The composite** (`shell_composite.cpp`, "ticket 0691 (MC #11169)" section, `LkHeld` / `LkHeldSkip` /
  `LkHeldRunEnd`): for a lockstep frame F not yet drawn it reads the hold before the wait and in every poll of the wait
  (both modes). Held: no wait (or the wait ends), nothing acquired, no server world in that frame (never an older one),
  CSV kind 5 `held` (`taken_f` 0), one `LOUD: lockstep: held_skip: the server holds frame F for EXE inputs (reasons
  0x... names): not waited for (X ms until the hold was seen), no server world in this frame (held run: n frames, t ms)`
  line per held frame, `held_skip` in the 1 s line, `heldSkips` in the publish block. A lockstep frame not held ends the
  run ("held run ended at frame F: n frames held, t ms"). A run longer than `lockstep_hung_ms` (the EXE presenting
  meanwhile) is a fault: one `LOUD: lockstep: the server's HOLD is STUCK: server held frame F for EXE inputs for X s
  while the EXE presented n frames (hold reasons ...)` line, `held_faults`, and the proxy's sink once per process (the
  same `VPShell_SetHungSink` -> `ProxyRtInt::ServerHung` route as a hung server, D118/D240, with this reason text); after
  the fault the held_skip lines come once per second. Normal play after the first world frame: `held_skip` must be 0
  (to be shown live).
- **Stand-in** (`lockstep_server.exe --hold-for-exe N,M [--hold-on PATH] [--hold-unreported]`): holds the first camera
  >= N (after PATH exists) and every camera after it until the camera block's `presentCount` moved M on (M < 0: never),
  reporting each held id with `VPHOLD_TEST | VPHOLD_FOG_NO_TABLE` in `<section>.Frame` unless `--hold-unreported`.

**Results** [L stand-in, workstation, 2026-11-23 03:44-03:47, the shell build of this change and one of the head
4b1936ea9 (+ the new stand-in), v3 `lockstep_server`, `lockstep_hung_ms=3000`]:
- Regression (`test_exact_held_frames_are_not_waited_for`, hold for 40 presents): on the new build 40 held frames
  (frames 794-833), each not waited for (the hold was seen before the insertion in all 40), 40 held_skip LOUD lines, the
  run ended after 78 ms, every frame after it exact, `hung_ends` 0, no proxy call. On the head the same test FAILS:
  `hung_ends` 1 (the first held frame waited 3 s, "no frame delivered for 3.0 s, heartbeat alive", the proxy's call),
  i.e. the live deadlock in miniature.
- The same hold unreported (`test_exact_unreported_hold_deadlocks_into_the_hung_route`): the hung route once (frame 772,
  "no frame delivered for 3.0 s, heartbeat alive"): the stand-in's hold is a real deadlock without the report.
- NEGATIVE CONTROL, a hold that never clears (`test_exact_stuck_hold_takes_the_failure_route_once`): every frame held
  (no world), one "HOLD is STUCK: server held frame 2263 for EXE inputs for 3.0 s while the EXE presented 1473 frames
  (hold reasons 0x80000001 fog-no-table test)", the sink call 8 ms later on the control thread, `held_faults` 1,
  `hung_ends` 0. On the head it FAILS (no held_skip, no fault).
- The 10 earlier exact cases unchanged and passing with the 3 new ones (13 passed, 152 s).
- Server: `test_fow_scene.exe` case `hold_report` (no table -> NoTable|Install with the frame id; the next held frame
  reported; table, no pair -> NoPair|Install; pair -> drawn, 0/0; in play a table of another size -> TableSize without
  Install; size matches -> 0/0; an install that failed -> no hold): PASS, 32 of 32 cases.

**LIVE: the load with `exact` no longer deadlocks** [L, 2026-11-23 04:39-04:44, c5h-c (hidden, guard on), EXE 4390913,
C5H-0167-TINY_0008 copy (put in and taken out by hash), harness 30 fps cap, server.exe + proxy of build_server.py at
c5bffa406's tree (proxy sha256 32e2dc2e...), composite of the same tree (cc47f373...), `lockstep_mode=exact` in the laid
ini AT THE LOAD; `live0691.sh` with `ARMS=exact REPEATS=1`, `an0691.py`; outputs outside git in
`C:/c5-64bit-work/0673/0673n/live/`]. The server held frame 24 (reasons 0x11: no plot table + install) and frame 25
(0x18: no overlay pair + install) and reported both ("lockstep: frame 24 held for EXE inputs (reasons 0x00000011)");
the composite skipped them: 2 held_skip LOUD lines, the run ended at frame 26 after 794 ms ("fog: installed at frame
26 after 649 ms of the fog's own wait"). Every later UpdateViews frame exact (2,298 of 2,298, `taken_f` = `pub_f`);
`hung_ends` 0, `dead_ends` 0, `giveups` 0, no failure report, `held_skip` 0 in all 79 later 1 s lines. The pan series
(0684's keys 150-1200 ms and drags, one repeat): F5csv 100 % in every series (351 of 351 camera-move frames), waits
p50/p90/p99/max 0.77/3.46/10.0/357 ms, 29.6 fps (one 364-ms frame in keys 150, as the 364-ms frame of 5.7's capped
run: not root-caused). Before the fix the same load waited 30 s and ended with exit 3 (3.2, pan-coherence.md 5.7).
**Found live:** each held frame still waited until the server's render thread read its camera: 1,126 ms for frame 24
and 558 ms for frame 25 (the server's pass was busy with the load: the field, the bakes and the fog's preparation),
so the EXE's first two map frames took about 1.1 s and 0.6 s. Not a deadlock and under the hung limit; a sticky hold
(skip any frame >= the held one while the hold lasts) would remove it, but it is not the per-frame-id report MC #11169
asked for: lead-0673's call.

**Not verified.** One load of one save at 30 fps (uncapped, other saves, a map change in play and the laptop not run).
The number of held_skip lines in a bigger load is not measured (one per EXE frame while the fog holds); a stuck hold
logs every frame until its fault (`lockstep_hung_ms`, 30 s), seen only on the stand-in. *(A map change in play: 3.2.2.)*

#### 3.2.2 The reload deadlock: the terrain's field gate (beta 3 / cand4, 2026-11-23) [C code; L live, both sides]

**The failure [L].** Beta 3's release candidate (cand4, main `7a40fb016`, installer-laid on c5h-new-a, EXE 26577625,
`lockstep_mode=exact`) failed its D162 journey `release-basic` at step 59: after turn 10 the journey saves, then loads that
save from the game menu (the second map load of the session, server.log `load 2 begins at ICvPlayer1::SetupGraphical`).
The composite ended the game after 30 s: `server-hung: no-frame ... heartbeat alive ... it drew no frame for the
composite's camera` (report `C:/c5-64bit-work/cand4/failure-report-20261123-140324`). The stale-Unit LOUD line in the
report is unrelated.

**The cause [C + L]: a deadlock between exact lockstep and the reload's field gate.**
- The composite's height-field tap copies the new load's field only after its load gate opens. On a RELOAD (a map was
  shown before in the process) the gate needs 30 Presents with the camera OK and 30 scene-clear frames (composite log
  14:02:50 `tap: load gate (grid seq 5): RELOAD rule`); on the first load it needs 3 Presents.
- `Scene::decide` (`srv_render_scene.cpp`) refuses every camera while its field belongs to the old load (the camera
  block's `gridSeq` differs from the field's load seq: `TS_NO_FIELD`, render.log `terrain: state OK -> NO_FIELD` at 473.4 s)
  and, before this fix, reported no hold (the host reported only `FowScene`'s hold bits).
- So at the first map frame after the reload (frame 11375) the composite waited for a frame the server would draw only
  after the new field, and the field could come only after 30 more Presents: no Present came. The host's lines show it:
  `draws=9092 published=9092` frozen, `waits_event` and `wait_timeouts` +10 a second (the 100 ms event waits), the
  composite's last line `held_skip=0 ... ring_pending=1`.
- The first load escaped: its gate is 3 Presents and the fog held (and reported) those frames.

**The fix (commit `93f4f0751`, MC #11763) [C].** `Scene::decide` sets `m_fieldHoldFrame` to the camera's frame id when it
refuses for the field; `Scene::holdFrameId()` / `holdReasons()` give the field hold (`Scene::kHoldNoField` = vp_frame.h
`VPHOLD_NO_FIELD` = 1 << 5, checked by a `static_assert` in the host) before the fog's; the host's `HoldReport` takes the
scene's. The composite names the bit `no-field` and skips the frame as at a load; a held run longer than
`lockstep_hung_ms` still takes the failure route.

**Verified.**
- Offline [L]: `test_fow_scene` `field_hold_on_reload` (the camera's grid turns to load 2 while the field is load 1: held
  with the frame id and `kHoldNoField`; the next refused frame too; the load-2 field: drawn, the hold cleared) PASS; the
  NEGATIVE CONTROL (the line disabled) FAILS with `reload: held frame 0 of 101, reasons 0`; all 34 fog scene cases pass
  (golden 4390913 copy, WARP). Build `C:/c5-64bit-work/0673/hold-nofield/` (render lib, `fowb`, `fowb-neg`).
- Live [L] (c5h-new-b, EXE 26577625, mods548, hidden, guard on, frame-capped; server `a17431a3...` and composite
  `302116f8...` built from a clean checkout of `93f4f0751`; cand4's `VP64Options.ini`; save C5H-0167-TINY_0008 turn 9;
  14:43-14:47): `release-basic.jsonl` steps 52-63 replayed (game menu, save, load that save, the 30 s wait, ENTER, then to
  the main menu) through `server/evidence/0673/reload/reload_check.sh` with a copy of the trace without its checkpoints
  (the save's turn is not the journey's). Composite: `RELOAD rule` at 14:45:14; 34 frames held `0x00000020 no-field`
  (1,508 ms); `tap: copy 2 of load 5 started (... camera OK for 43 Presents, 30 scene-clear frames, reload gate)`;
  `held run ended at frame 2819`; then 30-31 exact frames a second; at the end `hung_ends=0 held_faults=0`, 5,523 exact.
  Restored by hash, released 14:49.

**Not verified.** The full `release-basic` and `release-large` journeys on the fixed build (the MC reruns cand4's
journeys after the merge); EXE 4390913 live; a reload of a bigger save (a held run nearer `lockstep_hung_ms`: the gate's
30 Presents at a slow load's frame rate). Traps for the replay (runbook 3.10): `hi.py replay` opens its own session (a
session started before is refused, exit 2), takes the trace's ticket unless `--ticket` names the claim's, and checks the
trace's checkpoints from `--from` on (exit 6 when the save is not the journey's).

## 4. The hand-over: a ring of shared textures, no keyed mutex, no copy in the EXE

Today (version 3) the server copies its target into one shared texture under a keyed mutex, waits for its GPU, and
the composite acquires the mutex and copies into a local texture: +0.67 ms per EXE frame in process, nearly all the
mutex wait (0.52-0.54 ms) and the copy (0.48 ms GPU) [L, shared-texture-and-compositor.md 15.2; issue 3022].

### 4.1 The ring
- N slots (default 4 since 2026-11-13, was 3: see 4.6; 2..4, the composite's request), each a colour texture (B8G8R8A8_UNORM, render target + shader
  resource) and a depth texture (R32_FLOAT, NDC z), both shared by NT handle **without** keyed mutex, named by
  `VPFRAME_RING_TEX_FMT`. The server renders colour directly into the slot; depth is drawn into the server's private
  D32 buffer and copied into the slot's R32F (a GPU copy in the server, not in the EXE). The composite samples the
  slot directly: no copy in the EXE.
- Video memory: N x 2 x W x H x 4 bytes, 86 MB for N = 3 at 2554 x 1411 (the server's allocation, on the one GPU).
- The slot rule and the slot claim (a Dekker handshake with full barriers) are in `vp_frame.h`. The server never
  overwrites the newest READY slot (the miss fallback) nor a slot whose last use by the EXE's GPU has not completed.

### 4.2 Synchronisation
- **FENCE (preferred) [I until 3302's first check].** The server creates a shared `ID3D11Fence`, signals F after the
  slot's draw and depth copy and flushes; the slot is READY at submit time, not at GPU completion. The composite queues
  `ID3D11DeviceContext4::Wait(srvFence, F)` on the EXE's immediate context before its draw: the EXE's GPU waits, its CPU
  does not, and the server's GPU time (0.78 ms [L, 9039 3.2]) leaves the lockstep turnaround. D3D11 cannot open a fence
  by name, so the server duplicates the handle into the EXE (`OpenProcess(PROCESS_DUP_HANDLE)` + `DuplicateHandle`) and
  publishes the value (`srvFenceHandleInExe`). Premises: the EXE's device answers `ID3D11Device5` /
  `ID3D11DeviceContext4` (the system d3d11.dll on Windows 11 implements them whatever the EXE asked for) and
  `CreateFence(D3D11_FENCE_FLAG_SHARED)` succeeds on the RTX 4080; checked by 3302 in its first 15 minutes.
- **CPU (fallback).** The server publishes READY only after its GPU finished (event query; today's `--gpu-sync 1`);
  the composite needs no GPU wait. Used when the fence is unavailable; also a measured arm.
- **Reverse direction without a fence.** After its draw the composite issues an event query per slot use; it polls the
  query at later frames (`GetData` with `DONOTFLUSH`, never waits) and writes `slotReleased[s]` when it completes.
  With 3 slots and lockstep a slot comes back for reuse two frames later, which the EXE's GPU has normally finished;
  if not, the server skips that camera (counted `skippedBusyTotal`) rather than wait. So no GPU wait on a fence that a
  stopped EXE might never signal (no deadlock when the game is minimised or hung).

### 4.3 Depth on the same frame number
- The slot's depth is the same frame as its colour by construction. The composite draws colour (`SV_Target`, into the
  back-buffer RTV, opaque or straight alpha as today) and depth (`SV_Depth` from the slot's R32F, into the EXE's
  D24_UNORM_S8_UINT view that the scene-clear list cleared to 1.0 [L, shared-texture-and-compositor.md 12]) in one
  full-screen pass, depth function ALWAYS, depth write on, stencil untouched. Where the server drew nothing the value
  is 1.0, the clear value. The EXE's later lists then test their models against the server's terrain.
- The DSV is the one the scene-clear list cleared: the composite already tags that list by its deferred-context
  `ClearDepthStencilView` (`clear_dsv`) [C, lockstep-probe.md 1.2]; it records the view there.
- On a miss the depth of the older frame is written with its colour (consistent with what is shown; D58's objection,
  an older camera's depth cutting wrong edges, applies only on misses, which are counted). Precision: 1-5 D24 steps
  between the server's float z and the EXE's [C offline, shared-texture-and-compositor.md 12]; acceptance D checks
  units on slopes at 3200's cameras.
- Depth is temporary (increment 2 only, Architect 3.2): it is a switch (`depth_write=1`) and adds nothing to the
  protocol.

### 4.4 The ring as built and what the stand-in showed (issue 3302, W2, 2026-11-13) [L stand-in]

Code: `shell/frame/` (`ring_sink.cpp` x64, `ring_source.cpp` x86, stand-ins `standin/ring_producer.cpp` /
`ring_consumer.cpp`, `tests/test_ring.py`, README). Machine: the workstation, RTX 4080 (LUID 0xE0D7), Windows 11
26100, MSVC 14.43.34808; no game, no DLL involved. Tests: 34 passed, 0 skipped, plain and under MSVC
`/fsanitize=address` (no report).

- **Premise 1 [L 05:49].** `shell/frame/tests/premise/premise_fence.cpp`: x64 device, `CreateFence(0,
  D3D11_FENCE_FLAG_SHARED)` + `CreateSharedHandle(GENERIC_ALL)`; the x86 child started suspended, the handle given to
  it with `OpenProcess(PROCESS_DUP_HANDLE)` + `DuplicateHandle` (value 0x4 in the child); the child's device on the same
  LUID answers `ID3D11Device5` / `ID3D11DeviceContext4`, `OpenSharedFence` succeeds (completed value 0); `Wait(fence, 1)`
  followed by a clear and an event query, flushed: no completion for 300 ms (the child's GPU queue is held); after the
  parent's `Signal(1)` + `Flush` the query completed 1.15 ms later and the child read completed value 1.
- **A fence opened by a process that ends is completed to UINT64_MAX [L].** After a consumer that had opened the
  shared fence exited (killed or a normal exit), `GetCompletedValue()` in the server read 0xFFFFFFFFFFFFFFFF while it had
  signalled 11, and stayed there although it went on signalling. From then on every consumer `Wait` passes at once: a
  second consumer read stale pictures (the slot's previous frame in colour, sometimes in depth too) in 2-17 of 2000
  frames per run (5 runs), 0 with CPU sync. Fix: **a new fence per generation** (`RingSink::newFence`; every request,
  every re-open of the composite and every new composite gets one) and a check in `tick()`: completed > signalled ->
  regenerate with the same request; the source refuses a fence it finds at UINT64_MAX and asks for a new generation.
  After the fix: 0 stale frames in 3 x 2000. Why Windows does this is not established [I: device-removal semantics of
  monitored fences, the waiter's device is torn down]; it matters for the product because the EXE restarts and resizes.
- **The queued wait does the ordering.** With the fence, the slot's value had already completed at `acquire` in 1 of
  2000 frames (`fence_done_at_acquire`): READY is published at submit, so the EXE's GPU really waits on the server's.
- **Claims must not be frame ids.** The contract's claim value "the frame id the composite claimed last" cannot tell a
  second take of the same frame (a miss: the previous picture again) from the first after that first use was released:
  `slotInUse == slotReleased` again while the GPU reads it, and the server drew into a held slot (seen as the held frame
  showing a newer picture). The source now writes a counter that grows with every claim into `slotInUse` and keeps a
  per-slot count of open uses; `slotReleased` is written (= the newest claim) only when that count reaches 0. The
  server's rule (`slotInUse > slotReleased` = busy) is unchanged.
- **Give-back restores the previous value**, not `slotReleased` as the contract text says: with a pending older use of
  the slot, `slotInUse = slotReleased` would release it.
- **Event queries need later work to complete.** An event query ended and flushed as the last command of a context
  stayed incomplete under `GetData(DONOTFLUSH)` polling for 500 ms; a further command and `Flush` completed it at once.
  In the EXE every frame brings both; a stand-in or a shutdown drain has to issue them.
- **The wait bound.** `waitFor` sleeps on `<base>.Frm` plus a high-resolution waitable timer and spins the last 0.5 ms;
  300 waits for a frame that never came ended at the 3.4 ms bound, 1 of 300 0.98 ms late with a 0.3 ms spin while
  other teams were building (the 0.5 ms spin is the answer; the test allows 1 % late, none by 2 ms).
- **Slot pressure with the fence.** With READY at submit and slots given back only when the EXE's GPU finished, the
  ring runs out of free slots more often than with CPU sync: in a calibration run (3 slots, 2554 x 1411) the host's
  first `begin()` was Busy for 916 of 2171 cameras. Two answers in the code: the source completes releases inside its
  `waitFor` (so a slot comes back while the composite waits), and the stand-in host retries a Busy camera until the
  camera's deadline (`RingSink::freeSlots()`); with both, no camera was lost (exact 2171 of 2171) [L, one calibration
  run, 06:30]. The render host (3301) should do the same; 4 slots would be the other answer (+29 MB at 2554 x 1411).
- **A new composite numbers frames from 1**: a host that keeps the old composite's newest frame id ignores the new
  one's cameras until its numbers pass the old ones (seen: 89 cameras of 200 ignored). The host resets on a new
  `VPFramePublish::exePid`.

### 4.5 Hand-over cost on the stand-in (issue 3302, 2026-11-13 06:40:48-06:47:10 EST) [L stand-in, grade A]

Metric / tool: `shell/frame/tests/cost_series.py` (commit ccfe18c) under `quietlock.py run --take --who 3300-w2`
(the take waited for one heavy job, 3407's game on c5h-a, which ended at 06:40:48 before the first run). Builds:
`D:/c5-64bit-work/3300/w2/build/bin` (`shell/frame/build.cmd`, plain /O2 /MT). No game, no DLL, no pins involved.
Series: 4 arms x 6 rounds, arm order rotated per round, 12 s per run, the first 300 frames not counted (~6,400 frames
counted per run). Machine load (the release's disturbance summary): 76 samples, total CPU mean 12.5 %, max 20.0 %, 0 new
busy processes; free commit 40.2 GB at start and end. Raw: `D:/c5-64bit-work/3300/w2/cost/` (runs.jsonl, per-run logs
and per-frame CSV, summary.json).

Workload per frame, 2554 x 1411: the consumer (x86, the composite's place) publishes F, queues the "EXE's" GPU work
(a clear and one full-screen pass of a 1024-step shader), spins 1,500 us of CPU (the EXE's UpdateViews-to-insertion
CPU, lockstep-probe.md: 1.0-1.8 ms sparse), waits for F (bound 3.4 ms), takes it, draws it over its target with
straight alpha (no copy for the ring; v3 copies into a local texture first), releases, flushes with at most 2 frames in
flight. The producer (x64, the server's place) wakes on `<base>.Cam`, clears and draws one full-screen pass of a
256-step shader into the slot (v3: into a private texture) and publishes. Arms: **v3** = keyed mutex + copy, server
waits for its GPU before `ReleaseSync(1)` (shell_server.exe's `--gpu-sync 1` path, colour only); **ring_fence** /
**ring_fence4** = ring with 3 / 4 slots and the fence; **ring_cpu** = ring with 3 slots, READY after the server's GPU
finished.

| Per frame (6 runs per arm: mean, sd, min-max, 95 % CI) | v3 | ring_fence (3) | ring_cpu (3) | ring_fence4 |
|---|---|---|---|---|
| Frame time, us | 2041.0, 3.1, 2037.8-2044.6, [2037.7, 2044.2] | 1834.7, 29.7, 1811.9-1874.7, [1803.6, 1865.9] | 1814.5, 3.1, 1811.6-1818.9, [1811.2, 1817.7] | 1865.2, 3.4, 1862.0-1868.6, [1861.7, 1868.7] |
| Hand-over CPU on the consumer (acquire + release + poll), us | 52.9, 2.2 | 4.60, 0.19 | 2.40, 0.05 | 5.23, 0.13 |
| Consumer GPU from the take to the end of the draw, us | 40.4, 1.2 (copy + draw) | 14.6, 0.06 (draw) | 14.5, 0.04 | 14.6, 0.06 |
| Lockstep wait for F at the insertion, us | 409.8, 5.8 | 41.3, 0.7 | 203.2, 2.8 | 1.7, 0.05 |
| Frames taken exact | 99.98-100 % | 99.98-100 % | 99.98-100 % | 100 % |
| Server publish (end of draw to READY), ms | 1.930 (GPU wait + copy + mutex) | 0.075 | 1.113 (GPU wait) | 0.076 |
| Longest frame, ms (per run) | 2.4-3.5 | 2.6-2.7, **two runs 304 and 375** | 3.0-4.2 | 2.2-3.2 |
| Cameras whose first `begin()` was Busy (host retried to the deadline) | - | 17,562 of 39,216 | 6,385 of 39,611 | 0 of 38,516 |

Differences against v3 (Welch, 95 % CI; smallest detectable difference at 80 % power): frame time ring_fence -206 us
[-238, -175] (detectable 42), ring_cpu **-226 us [-231, -222]** (6), ring_fence4 -176 us [-180, -172] (6); hand-over
CPU -48 to -51 us (3); consumer GPU -26 us [-27, -25] (2); wait -369 / -207 / -408 us (8).

Why the numbers moved: (1) the consumer no longer copies (GPU 40 -> 15 us: the 2554 x 1411 copy is ~26 us on an idle
4080; the 0.48 ms of 3207 in the game is that copy behind the game's own GPU queue [I]) and no longer takes the keyed
mutex (CPU 53 -> 2-5 us); (2) the lockstep wait shrinks because the server stops waiting for its GPU before it
publishes (v3 1.93 ms, ring_cpu 1.11 ms of GPU wait in the server; fence 0.08 ms): the frame is READY at submit and the
consumer's GPU, not its CPU, waits for the server's draw. In this stand-in the CPU is the bottleneck (1.5 ms spin +
submission), so frame time follows the CPU wait: v3 -> ring saves ~0.2 ms per frame, and ring_cpu is the fastest arm
here because its wait (0.2 ms) still hides under the frame while the fence arms pay their wait on the GPU, where the
2-frames-in-flight throttle hands it back to the CPU [I]. ring_fence4 waits almost never (1.7 us) yet is 31 us slower
than ring_cpu [I: one more 29 MB slot in the GPU's working set; not profiled]. The bimodal ring_fence frame times
(1812-1818 or 1871-1875 us) and its 304 / 375 ms stalls come with the 3-slot fence ring running out of free slots
(45 % of cameras needed the host's retry): see 4.6. **For the product**: the hand-over cost itself falls from ~93 us
(CPU 53 + GPU 40) to ~17-20 us per frame; which sync wins on frame time depends on where the EXE's frame is bound and
must be measured in the game (3306, acceptance C); the in-game +0.67 ms of v3 (3207) was mostly the mutex wait and the
copy behind the game's queue, both gone in every ring arm.

With the GPU shared (grade C, one 2-s run per arm at 06:55 while team 3405's game ran on c5h-e, no hold): ring_cpu
took the exact frame in 2.8 % of frames (its lockstep wait ran into the 3.4 ms bound: the server's CPU wait for its GPU
now includes the game's queue), ring_fence and ring_fence4 in 100 %, ring_fence with a host that skips Busy cameras
at once in 67 %. So CPU sync is only competitive while the server's GPU work is not queued behind another heavy
process; in the game that process is the EXE itself [I], which favours the fence.

### 4.6 Open: rare long frames with the 3-slot fence ring (issue 3302, 2026-11-13) [L stand-in, cause not established]

*In the game (3306, 2026-11-13, section 4.7): with 4 slots and the fence, the long frames of 30 AI turns (62 over 250 ms,
max 1.2 s) were the EXE's own: a control run without the renderer had about as many (45, max 1.1 s), every one taken
exactly with no wait. No ring stall could be told apart; the cause on the stand-in stays open.*

In the series (4.5) two of six ring_fence (3 slots) runs had one frame of 304 ms and one of 375 ms (and one of 54 ms);
the other arms' longest frames were 2.2-4.2 ms in all 18 runs. A probe of eight more 3-slot fence runs right after the
series (lock free, no hold) had one frame of 46 ms; three more under another team's no-timing hold had none. In the
long frames the lockstep wait and the hand-over calls were short (wait 2-253 us, hand-over CPU 8-11 us): the time went
to the consumer's CPU waiting for its GPU to finish frame F-2 (the stand-in's 2-frames-in-flight throttle), i.e. the
consumer's GPU stood still [I from the per-frame columns; the throttle column `throttle_us` was added to the CSV
afterwards and has not caught a stall yet]. The arm that showed it is also the one whose host needed the Busy retry for
45 % of its cameras. Not established: whether this is the cross-process fence wait being released late by the GPU
scheduler, or the slot pressure. What wave 2 needs: count frames over 20 ms in the joined game run for the fence and
CPU arms (acceptance B/C), and prefer 4 slots (no Busy retries, no long frame in 6 runs) or CPU sync if they recur.

Contract changes the lead may take into `vp_frame.h` (the code works with the header as it is): `slotInUse` /
`slotReleased` carry claim numbers, not frame ids; the give-back restores the previous value; `VPFrameSlot::fenceValue`
is the value of that generation's fence (a counter from 1), not the frame id; "the server creates a new fence per
generation"; `skippedBusyTotal` counts Busy answers; `VPFRAME_DEFAULT_SLOTS` 4 instead of 3 for the fence (4.5, 4.6).

### 4.7 Integration and the joined live run (issue 3306, W6, 2026-11-13) [C code, L stand-in, L live as marked]

Code: `shell/tests/test_join_ring.py` (the two halves together), the contract clean-ups below, `server/tests/render/`
(new cases), live driver `shell/evidence/3306/run_3306.py` (prep / check / run / restore) and
`shell/evidence/3306/summarize_3306.py`. Builds: `D:/c5-64bit-work/3300/w6/` (`build_all.sh`, `tests.sh`).

**The ring end to end on the stand-in** [L stand-in, 2026-11-13 09:16, build of the team branch b0fc286].
`render_host_test.exe` (the render objects server.exe links) with `render_handover=ring` against `test_host.exe` with
the composite at `handover=ring`, `lockstep=1`, `depth_write=1`, the composite's synthetic height-field tap: the render host
switches to event pacing at the composite's first publish, draws into ring slots, the composite takes them exactly in all
but the counted misses, every miss justified (a wait to the deadline or a give-up), every wait within its deadline + 0.5 ms,
depth written. Negative cases: the server hands over by ring while the composite expects version 3 (no frame ever taken,
the composite gives up waiting and goes on, both exit 0), and the render host killed under a fence ring (the composite never
waits past its bound, gives up, host exit 0). The first cpu-sync run failed only in the test: the back-buffer BMP was read
while the DLL still wrote it (PermissionError); the read is retried now.

**Suites on the final code** (builds of 129eeee, b1 plain and a1 MSVC `/fsanitize=address`, 2026-11-13 10:41-11:40, the
machine shared with other teams' games; every run exit 0 unless named): `server/tests/render` 43 passed, 0 skipped (39 of
3301 + 4 of 3306), and under ASan 43 passed; `shell/tests/test_join_ring.py` 4 passed, and under ASan 4 passed (after the
change below); `shell/frame/tests/test_ring.py` 34 passed plain and under ASan, `test_lockstep.py::test_ring_handover_lockstep`
2 passed. ASan reports: none (`D:/c5-64bit-work/3300/w6/asan-logs-a1..a3` empty; a canary with the same `ASAN_OPTIONS
log_path` form wrote its heap-buffer-overflow report). Found on the way: `test_ring.py::test_generation_change_on_resize`
still expected 3 slots after the wave-1 default became 4 (afcf62c) and failed since then (fixed in the test); under ASan
the cpu-sync join arm missed every bound on the loaded machine (0 of 600 exact, every miss justified), so that arm checks
only the miss rule under ASan, as `test_lockstep.py` does.

**Contract clean-ups** (2026-11-13; the code works on the old headers too):
- `frame_sink.h`: optional members with defaults - `setState(state, failReason)`, `camEvent()` (a `void*` HANDLE),
  `freeSlots()` (1 = "begin() decides"), `takeResized()`. The render host calls them on any sink: the down-casts to
  `RingSink` (camEvent, freeSlots) and to `V3Sink` (camEvent, takeResized) are gone; `V3Sink*` stays only for its test
  dump and its overwritten / skipped counters in the per-second line. The busy retry until the camera's deadline now
  applies to any sink that answers Busy.
- `srv_tile_source.h`: `TileCacheSource::wantedTilePlots()` (0 = any) replaces the EXE-cache tap's constructor callback.
  The tap answers the bake tap's tile size while the field's differs; the scene, after each poll, rebuilds the tiles from
  its kept field (never against a fixed `render_tile_plots`) and polls the source again in the same iteration, so the
  manifest is applied as before. Tested: a 1-plot bake tap that appears after a 2-plot field (rebuilt, 36 slots applied)
  and, negative, `render_tile_plots=2` against a 1-plot tap (refused, no cache opened, the field keeps 2-plot tiles).
- `vp_frame.h` `VPFrameServer::pacing` (`VPFramePacing`: 1 event, 2 legacy) from `reserved[0]`: offset 88, size and every
  other offset unchanged (static_asserts). The host writes it every loop iteration.
- The version-3 sink already wrote the depth block's `frameCounter` equal to the frame block's and set `<base>.Frm` after
  `ReleaseSync(1)` (`srv_v3_sink.cpp` `end()`, since 3301; W1's suite checks `frameCounter == depthFrameCounter`): nothing
  to change.
- `RingSource::waitFor` (3302): the slow checks (`serverOk()`: the server's process handle; `complete()`: `GetData` on the
  EXE's context) now run at most every 0.25 ms and never inside the last 0.5 ms before the deadline, as the composite's own
  loop since 3304 (bce6889). The composite does not call `waitFor`; `ring_consumer.exe` does (3302's bound test).

**The feed's changed plots into the tile source** [L stand-in]: the render host already handed `Batch::changedPlots` to
`Scene::plotsChanged` -> `TileCacheSource::onPlotsChanged`; it is now counted where it can be seen (the tap's per-second
`plots_changed=`, the totals `source plots changed` / `lost plots changed`: plots that changed while no source existed, a
device rebuild or before the first device). Tested with `render_host_test --feed-plots 300` (a 64 x 40 snapshot, then 300
improvement patches as GameplayPlotStateChange makes them): 300 reach the source, 0 lost; a repeated patch with the same
value is no change; with the feed off nothing arrives. The adapter for team 3400's `c5r::bake::Residency` is not written:
it is not on `main` (`render/include/c5r/bake/` has no Residency on this branch), so the tile source stays the EXE-cache tap.

**Live, run r1 (2026-11-13 09:22-09:37, aborted in the load, kept as a finding)** [L]. Set-up as run r2 below, but the
server's run folder (`run_dir` of server.cfg, with its database copy `db/Civ5DebugDatabase.1.db`) on `D:`: **every database
write of the server took ~130 ms** (`database: write #N` lines 0.12-0.14 s apart), so the load's database phase ran
669 s against 9040's ~14 s (proxy `DbOps batch #1` at 124 s against 4 s). The instances' folder `%USERPROFILE%/c5-instances`
has the Defender exclusion (COMMON 15) and D: has none. *Corrected 2026-11-13 (issue 0129): the cause is the flush of every autocommit write on D:, a rotating disk (136-177 ms a write with SQLite's defaults, the same on C: with or without the exclusion: 4 ms), not Defender; fixed in the server (`research/03-dll-engine-boundary/proxy-design.md` section 19); replaced: "[I: Defender scanning each write of the journal; not profiled]".* A
server.cfg written by hand (vpproxy.ini `server_config=`) must put `run_dir` inside the instance; the driver does since then.
The game still loaded (turn 350, 09:35:42) and for the 25 s before the safe stop the joined path worked: ring generation 1
(4 slots, 2554 x 1411, fence), 340-390 frames a second, every UpdateViews frame `exact` with depth written, `cmp_diff` 0,
the server's terrain drawn under the EXE's models (`D:/c5-64bit-work/3300/w6/live-r1/r1-ingame.png`, the load screen still
up: the EXE draws its models and the composite the terrain over the load screen's picture there). The render thread polled
the camera block at ~880 Hz (legacy pacing) for the 11 minutes of the database phase and load, until the composite's first
publish [L: `waits_legacy` 636,580]: cheap (one high-resolution timer wait each), but a wake-up rate a product may want
lower before a camera exists [I].

**Live, run r2: acceptance A, D, E, premise 3 (2026-11-13 10:49:19-11:12:27 EST)** [L]. Set-up: instance c5h-a (Goldberg),
DX11 EXE (04CFDC71...), CP/VP/EUI, window 2554 x 1411, RTX 4080; save `C5H-3306-HUGE_0350` (copy of the instance's
`C5H-VP8P-HUGE_0350`, SHA-256 c499d3c6..., turn 350). Builds of this branch at 129eeee (b1, `dirty=0`): server.exe
4461958A..., proxy 1A4D44E5..., composite CE86E00E...; the x64 DLL of the pin layout `x64-76992d23-p1607d42` (FA75F483...,
COMMON 18). Proxy route (set-proxy, vpproxy.ini `mode=on local_answers=1 local_verify=0 lua_mirror=0`), server.cfg by
`server_config=` with the proxy's lines plus `render=1 render_handover=ring render_ring_slots=4 render_sync=fence
render_fallback=constant render_constant_rgba=0xFFFF00FF render_section=Local\VPShell3306` (no shell_server.exe); D33 by the
9040 markers (`tap_live.py config --arm d33`, `crashlogs\shell.helper` naming the composite); composite `mode=composite tap=1
bake_tap=1 insert=tagged tag_rule=after tag_match=clear_bb camera=1 lockstep=1 lockstep_csv=1 lockstep_check=1 depth_write=1
handover=ring`. Driver `run_3306.py run --max-turns 30 --sync fence --reloads 3` (functional: no quiet-lock hold; other
teams' games and builds on the machine). Guard check 0 at launch, in game and at the end; start-up watch ok; harness stop
0 (stop_readiness); instance restored by hash (backup verify equal/absent, provision verify 0). Raw:
`D:/c5-64bit-work/3300/w6/live-r2/` (out/: run.json, logs, per-frame CSV 244,683 rows, shots; summary.json); small
copies in `shell/evidence/3306/` (summary_r2.json, feed-per-turn_r2.tsv, memsampler_r2.txt); shots stay on D: (D83).

- **A: 30 AI turns (350 -> 379), no crash, no dialog, 0 LOUD lines** in server.log, vpproxy.log and render.log; every
  `turns.py end` / `wait` exit 0; 20.3-30.9 s per turn (median 24 s; not a timing result). The load took 2 min 51 s to
  the in-game state (database phase 12 s: batch #1 at 3.9 s against r1's 124 s on D:). No DISABLED or EXCEPTION line of
  the composite. The server ended with the harness's stop as every harness-stopped run does (the EXE is terminated, the
  server reads `PeerDied` and exits 3: no kRt_Shutdown, so no render summary line in server-report.txt; render.log's last
  `host 1s` line: 244,180 draws, 244,180 published, 4 Busy answers, 35 Busy retries that got a slot, 396 cameras skipped
  because a newer one had arrived, 0 errors, 0 lost devices). At the stop the ring sink saw its fence completed to
  UINT64_MAX ("its consumer ended") and made a new generation: 3302's finding, seen live.
- **The feed from the real EXE** (`feed-per-turn_r2.tsv`): `GameplayPlotStateChange` 9,966 by the end of the load turn
  (the EXE lays out nearly every one of the 10,240 plots after a load; 0 in every headless game, 3303), then 7-236 per AI
  turn (median 40); a reload brought 9,809 more (row 380). FOW 985-1,751 per turn (median 1,368); 0 dropped events, 0 exceptions, 14
  records the full refresh found changed (fow) and 0 of terrain, plot type, feature, improvement, owner, river.
- **Lockstep, functional** (`lockstep 1s` lines summed over the 30 turns, no timing claim): 155,598 frames published at
  UpdateViews, 155,582 taken exactly from their own camera, 16 older, 0 none; 35 waits, 3 of them ended more than 0.5 ms
  after their deadline; 0 give-ups; depth written in all 155,598; `noins` 18,036 frames without the scene-clear list
  (leader screens and the like, as 9040's `miss_nomatch`).
- **Premise 3 live: holds.** `lockstep_check=1` compared the camera read at the UpdateViews hook with a read at Present in
  155,598 frames of the turns: `cmp_diff` 0; no row of the whole run's CSV (244,683 frames, views and reloads included)
  has a differing field. The camera read at
  the new point cost 6.8 us per publish (median of 950 `uv_us_mean` 1-s values, 4.3-62.2; p99 median 10.5 us) with the
  game running.
- **D: depth at the cameras of 3200** (load view, `v1_city_default` (100, 30), `v2_city_zoomin` (4 wheel ticks),
  `v3_second_zoomin` (99, 31)), one camera per view (raw camera bytes equal before and after its three shots): shots with
  `depth_write=1`, `depth_write=0` and passthrough. D33's static models (buildings, walls, wonders) are drawn dark red;
  counting only those pixels (R 36-139, G and B < 25), so that animated units and tooltips do not count:

  | View | Model px, passthrough | ... with depth_write=0 | ... with depth_write=1 (the model hides the terrain) | hidden by the server's depth | appear only with depth (noise) |
  |---|---|---|---|---|---|
  | load | 148,539 | 148,233 | 133,150 (89.8 %) | 15,990 (10.8 %) | 907 |
  | v1 | 395,421 | 393,155 | 342,505 (87.1 %) | 51,593 (13.1 %) | 943 |
  | v2 | 542,673 | 541,083 | 460,047 (85.0 %) | 84,136 (15.5 %) | 3,100 |
  | v3 | 607,019 | 604,051 | 555,903 (92.0 %) | 64,527 (10.7 %) | 16,379 (a tooltip) |

  So an EXE model is hidden by the server's terrain (its parts below the surface and behind slopes: wall rings on hills,
  unit feet, building bases) and hides it everywhere else; without the depth write every model pixel is drawn over the
  terrain. Routes (rails) on the ground stay visible with the depth write (v1, v3). **Not measured: whether each hidden
  pixel is one the stock EXE would hide** (a comparison with a stock shot at the same camera; across launches only the load
  view repeats its camera, terrain-surface.md 5) [I: the precision is 1-5 D24 steps, section 12 of
  shared-texture-and-compositor.md]. *Measured since by 3310 (section 4.10, 2026-11-13): an estimated 52-71 % of the
  static-model pixels the server hides are hidden in stock too; other cameras repeat within 0.63 px; the D24 precision
  moves no cut by more than about 0.01 px.*
- **E: memory per turn** (`perf/memsampler` with `--server`, turns 350-379, 30 rows, 0 skipped): server.exe private
  1,362.8 -> 1,413.8 MB, **+1.82 MB per turn [1.57, 2.06]** (second half +0.84 [0.54, 1.13]); working set +1.76; the
  renderer's video memory (local) 172.8 -> 148.4 MB at the turns (the driver demotes unused tiles, as 9040 saw) and 944-952
  MB right after the load and the views. The EXE: private +2.03 MB per turn [1.71, 2.34], largest free block 1,858 ->
  1,770 MB (-1.96 per turn): bug 0102's fix is in the pins (9040 measured +25.1 through the proxy without it). Whether the
  server's +1.8 MB per turn is the DLL's own: the control run with `render=0` (r3, below).
- **The control run r3** (2026-11-13 11:19:02-11:34:53 EST, the same builds, instance, save, D33 and composite settings;
  server.cfg `render=0`: no render thread, no feed; no views, no reloads; one run): A again (30 turns 350 -> 379, rc 0,
  guard 0 / 0 / 0, 0 LOUD, stop 0, restored by hash). server.exe 219.7 MB private after the load (r2: 1,329.0 MB: **the
  renderer adds ~1.1 GB private** on the huge save, the field copy, the tiles and the device; video 944 MB), then
  **+1.03 MB per turn [0.75, 1.32]** (memsampler; second half +0.76 [0.36, 1.17]); the EXE +1.72 MB per turn [1.43, 2.01].
  Against r2: the server grew **+0.79 MB per turn more with the renderer over all 30 turns** (about [0.4, 1.2], from the two
  intervals) **and +0.08 [about -0.4, +0.6] over the second half (turns 365-379)**: the extra growth is in the first turns
  after the load and is not measurable in the second half [L, one run per arm; I: heap and driver pools settling after the
  load, as 0121's first reload step]. Verdict for E: no growth per turn in the server beyond the DLL's own once the first
  ~15 turns after the load have passed; a longer series would tell whether the first-half extra stops for good.
- **The stall question answered by the control**: the control run, with no server frames, no ring and no fence, had about
  as many long frames in its 30 turns: over 20 ms 2,191 (r2 2,232), over 100 ms 519 (680), over 250 ms 45 (62), max
  1,093 ms (1,185), p99 60.0 ms (58.9). So the long frames are the EXE's own (AI turns through the proxy); no 0.3-s stall of
  the 3-slot fence ring (3302, 4.6) can be told apart in the game with 4 slots. r2 had more frames over 100 ms (+161) and
  over 250 ms (+17) than the control [L, one run each on a shared machine: no timing claim; 3307's series under the quiet
  lock should count frames over 100 / 250 ms per arm].
- **Reloads in server.exe (bug 0121 under acceptance E)**: three loads of the save in the same process after the turns
  (`tap_live.py reload`), server.exe private after each settled load 1,413.8 (before) -> 1,702.9 -> 1,707.5 -> 1,707.1 MB:
  one step of +289 MB at the first reload, then +4.6 and -0.4: flat after the first (0121's shell_server.exe stepped up over
  the first 2-4 reloads, +333 to +432 MB, then 0-28 MB). Video memory 948 -> 1,096 -> 1,096 MB. Field rebuilt in 1.7-2.0 s
  each time (128 x 84 tiles of 1 plot).
- **The stall question (design 4.6): frames over 250 ms occur, but not in the hand-over.** In the 30 turns 2,232 frames
  took over 20 ms, 680 over 100 ms, 62 over 250 ms (max 1,185 ms; p50 2.86 ms, p99 58.9 ms). For every frame over
  250 ms: the composite took the exact frame without waiting (wait 0, age 0), 92 % of the time lies between the previous
  Present and the next UpdateViews, 8 % between UpdateViews and the insertion (26-55 ms), and 0.1 ms from the insertion to
  Present. The fence ring's 3302 stall had the consumer waiting on its own GPU with the hand-over short; these frames are
  the EXE's main thread busy before it renders (AI turn processing through the proxy) [I], to be compared with the
  control run's frames. Present's own duration is inside "previous Present to UpdateViews" (the row's Present time is
  taken before the original Present), so a GPU wait at Present cannot be excluded from this CSV alone.

### 4.8 Residency in server.exe, per-boot names, idle pacing, the product-path run (issue 3308, W8, 2026-11-13) [C code, L as marked]

Code: `server/src/render/srv_residency_source.*` (the `TileCacheSource` adapter over epic 3400's `c5r::bake::Residency`),
`srv_render_options.cpp` (`[Renderer] BakedTileCameraRadius` from `SrvOptions()`), the scene and the host (`srv_render_scene.*`,
`srv_render_host.*`), `proxy/src/proxy_shell.*` (per-boot names, the product's hand-over line), one block in
`shell/composite/shell_composite.cpp` (the composite follows `VPSHELL_SECTION`), tests `server/tests/render/test_residency_source.py`
and new cases in `proxy/tools/rt_tests/shell_unit.cpp`; live driver `shell/evidence/3308/run_3308.py` and
`summarize_3308.py`. The calls follow `shell_server.exe --scene terrain --bake` (team 3400's TerrainInit / ApplyField /
TerrainFrame / TerrainPump at 9a2a63e; that file is unchanged).

- **The source** [C]: `render_tiles=bake` (the new default; `tap` = the EXE-cache tap as the test reference, `none` = the
  fallback colour). Per device the source opens the install (`render_bake_install`, default server.cfg `game_dir`), reads
  `TerrainDef.csv`, opens the EXE's shader section (`render_exe`) and makes a `PipelineBaker` (`GpuTiming::Deferred`) or,
  with `render_baker=standin`, the stand-in; then `Residency::init`. A device rebuild makes a new source (bakes again from the
  kept field). A source that cannot start is one LOUD line; the scene then draws without caches (counted `source failures`
  in the totals). `wantedTilePlots()` = 1: the scene builds 1-plot tiles for it. `beforeDraw` gets the scene's camera
  (`ResidencyView`) and `render_bake_budget_ms` (default 4, shell_server's `--bake-pump-ms`); after each frame the host calls
  `idle(budget)`, with 0 when a newer camera block is already waiting; while bakes are pending (`pending()`, read at most
  every 100 ms because `Residency::stats()` walks every tile) the thread waits at most 2 ms instead of 100.
- **Plot indices** [C, residency.h]: the feed's changed plots (`SrvFeed::Batch::changedPlots`, the DLL's `GetPlotIndex` =
  y * width + x of the game's map) are exactly the residency's index (rows without the field's 2 + 2 margin rows); they are
  passed through unchanged. Plots that change before the field exists are dropped (the field's first bake covers them).
- **Radius** [C]: `SrvOptions().BakedTileCameraRadius()` (the options file the server read from `options_file=`, one-off
  9042), unless server.cfg `render_bake_radius` names one; logged with its source at the host's start. Corrected
  2026-11-13 by issue 3312 (4.12): `options/include` is in RENDER_INCLUDES and `srv_render_options.cpp` includes
  `srv_options.h` by name (replaced: the relative include of `vp64_options.h` and a declaration of `SrvOptions()` of its own).
- **Pacing before the first publish (3306's 880 Hz)** [C]: the 0.5 ms camera poll of the legacy path now runs only while
  the scene has a camera to draw (state OK: an old composite that writes camera blocks without the event). Otherwise (no
  composite, database phase, load screen) the thread waits on `{stop, <base>.Cam}` with a `render_idle_poll_ms` timeout
  (default 20; 0 = the old rule), so the first publish wakes it at once.
- **Per-boot names** [C]: the proxy can boot more than once in one EXE process (unloaded and loaded again: its globals
  restart) while the composite it loaded stays. The boot count therefore lives in the EXE process's environment
  (`VP64SHELL_BOOT`, read, plus one, written at each `Configure` that hands out names; a value that is not 1..1000000 counts
  as 0, logged). Boot 1 keeps 3305's names (`Local\VP64Shell_<pid>`, the server's default texture base); boot N >= 2 gets
  `<base>` = `Local\VP64Shell_<pid>_<N>` and server.cfg `render_texture=Local\VP64ShellWorld_<pid>_<N>`. The composite's
  control thread compares `VPSHELL_SECTION` with its section every 500 ms and, when it differs, re-reads its configuration
  as after an ini change (`ApplyConfigIfChanged` closes and re-opens the section). Not exercised live: no second proxy boot
  in one EXE process was made (the reloads below are loads inside one boot).
- **The product's hand-over line** [C]: the proxy's server.cfg lines are now `render=1`, `render_section=<base>`,
  `render_handover=ring` (+ `render_texture=` from boot 2). Without it the product route ran the version-3 keyed mutex (the
  server's default stays v3 for direct runs and the tests). The composite's ini must say `handover=ring` too; since issue
  3312 (2026-11-13, 4.12) the shipped `shell/composite/shell_composite.ini` says `handover=ring lockstep=1 depth_write=1`
  (replaced: the v3 / 0 / 0 defaults).

**Tests** (2026-11-13; builds of this branch; workstation, no hold) [L]: `server/tests/render` plain 12:01-12:06: all passed,
0 skipped, exit 0 (the 43 earlier cases with `render_tiles=tap`, which they were written for, and the 26 new ones:
stand-in bake of the whole synthetic map with no gray frame after the bake-ahead and no fallback pixel in the frame; the
radius from the options file and its override; a bad options value = the default; 20 changed plots raise and re-bake
their tiles (12 on the map, the rest counted and ignored); a failing tile plain and counted; the pipeline without an install
= one LOUD line, terrain still drawn, exit 0; `none` / `tap`; 15 refused keys (exit 2); **the pipeline on 3401's Tiny tap
and the install: bake-ahead of 1,352 land tiles in 8.7-9.9 s, 0 failed, 328-381 ms of baker GPU time**; idle pacing 46-50
wake-ups in ~2 s before a camera (the 20 ms timeout waits ~40 ms with the default timer resolution), then the 0.5 ms
poll; `render_idle_poll_ms=0` = the old rule; the first publish ends an idle wait at once).
With the feed filter (below): the feed tests 5 passed (12:35); **under MSVC `/fsanitize=address`** (`build_render_tests.py
--asan`, the ASan runtime of MSVC 14.43 on PATH, `shell/tools/memcap.py --mb 6144`) the whole `server/tests/render` suite
12:41-12:46: 71 passed, 2 failed, **0 ASan reports** (the empty report folder; peak commit 1,187 MB): `test_idle_pacing_...`
counted 37 idle wake-ups where the test wanted at least 40 (a 20 ms wait lasts up to ~47 ms at the default 15.6 ms timer
resolution; the bound is now 20) and 3306's `test_resize_request` took 3 of 4 frames before the cameras ended (ASan on a
loaded machine); both passed when run again under ASan (12:47, 4 passed). The pipeline case baked Tiny under ASan too.
Proxy (`proxy/tools/test_proxy_rt.py --only-shell --asan`, build b2, 12:31): `shell_unit` **145/145** plain and under MSVC
`/fsanitize=address` x86 (canary reported, no report; 23 new checks: boots 2 and 3 get their own names and the texture
line, the first boot's names unchanged, six bad `VP64SHELL_BOOT` values count as 0, Reset clears the count), the loader
cases 25/25 (shell-on: `render_section=Local\VP64Shell_<pid>` with the real server).

**Live run r1: the product path, 10 AI turns and five reloads (2026-11-13 12:09:18-12:28:38 EST)** [L]. Instance c5h-b
(Goldberg), DX11 EXE 04CFDC71..., CP/VP/EUI, window 2554 x 1411, RTX 4080; save `C5H-3308-HUGE_0350` (copy of the
instance's `C5H-VP8P-HUGE_0350`, SHA-256 c499d3c6..., turn 350; removed after). Builds of this branch b2 (commit
128d03e, before the feed filter below): server.exe 001CD7A8..., proxy 26EEE361..., composite A96E9246...; the x64 DLL of
the pin layout `x64-76992d23-p1607d42` (FA75F483..., COMMON 18). **Product path**: `VP64Options.ini` `[Renderer]
ServerRenderer = 1` (radius left at its default -1), the composite and its ini beside the proxy with the manifest of
`shell_manifest.py write` (check passed), no `crashlogs\shell.helper`, no viewmask marker, no `server_config=`: the proxy
wrote `render=1`, `render_section=Local\VP64Shell_9068`, `render_handover=ring`; D33 by darkred.py's list **without
Overlay** (D93); composite ini `mode=composite tap=1 bake_tap=0 insert=tagged tag_rule=after tag_match=clear_bb camera=1
lockstep=1 lockstep_csv=1 depth_write=1 handover=ring ring_sync=fence` (no `section=`: from the environment). Functional
run (no quiet-lock hold; W7's and W10's games and other teams' builds on the machine). Driver
`shell/evidence/3308/run_3308.py`; guard check 0 at launch, in game and at the end, start-up watch ok, harness stop 0,
instance restored by hash (backup verify equal, provision verify 0). Raw: `D:/c5-64bit-work/3300/w8/live-r1/`; small copies
`shell/evidence/3308/summary_r1.json`, `feed-per-turn_r1.tsv`, `memsampler_r1.txt`.

- **10 AI turns (350 -> 359), 0 LOUD lines** in server.log, render.log, vpproxy.log and shell_composite.log; every
  `turns.py end` / `wait` exit 0 (27-37 s per turn; not a timing result). No crash (bug 0127 not seen: no pans, Overlay on).
- **The server's bake on the huge save**: field 128 x 84 tiles, 4,922 land tiles (150 class 2 + 4,772 class 3); **the
  bake-ahead finished 15.8 s after the field** (4,922 bakes, **0 failed**: the 4 Gibraltar tiles of 3408 bake now; 352.7 MB of
  caches in 362.6 MB of arrays, 1,172 ms of baker GPU time); **gray frames: 23, all before the bake-ahead finished; 0 after**
  (71,486 frames counted to the end, including the five reloads). Each reload: a field of the same size, compared: 17-27
  tiles changed per reload, the bake-ahead "complete" 174-267 ms after the field (only the changed tiles re-baked).
- **Re-bakes from the feed (found here, fixed after the run)**: 10,211 re-bakes in the run, about 1,500 per minute during the
  AI turns (per-second `bakes_sec`), 29,540 tile raises from 844 changed plots: the adapter passed every plot the feed named,
  and the feed names fog and owner changes (1,000-1,750 `GameplayFOWChanged` per AI turn, 3306). Each plot raises 35 tiles
  (2 columns, 3 rows each way), so the server re-baked ~700 tiles per turn at 2.4 ms of CPU each, in the render thread's
  `idle` slices. No gray frame came of it (a stale tile keeps its old bake until re-baked). **Fix (committed after r1, not
  run live)**: `ResidencySource` keeps a copy of the feed's records (`SrvFeed::CopyPlots`) and passes on only changes the EXE's
  tile invalidation reacts to: terrain, plot type, feature, river / natural-wonder bits (once a getter had read the plot),
  route, resource, improvement and their states and pillage bits (once a layout had been seen for the plot); fog, owner and
  the city bit never; a new table (load) is the baseline. Tests: improvement and terrain changes pass (20 of 40, the first
  round filtered), fog / owner / a first layout pass nothing (0 raises, 0 re-bakes).
- **Lockstep (functional)**: 29,641 frames published at UpdateViews over the turns, 28,343 taken exactly (95.6 %), 1,298
  older, 0 none; 1,257 waits, 526 ended later than 0.5 ms after their deadline (the pre-0150 `late` counter, late starts included: not
  all of them overstays; issue 0150, 2026-11-13), 7 give-ups and 7 recoveries; depth written in
  all 29,641. 3306's r2 (the EXE-cache tap, no residency) had 16 older in 30 turns. [I: the render thread's 4-ms `idle` bake
  slices during the re-bake stream delayed the draw of a camera that arrived meanwhile past its bound; the feed filter
  removes most of that work; to be checked by a run with the filter and by 3307's series under the quiet lock.]
- **GPU time per draw** (render.log `terrain 1s: gpu_ms`, timestamp queries around the terrain draw; 537 seconds with
  draws): **median of the 1-s means 0.833 ms**, draw-weighted mean 0.868 ms, p10 0.697, p90 1.206, max 16.0 (load); in the
  turns (12:13:30-12:18:59) 0.876 ms median with bakes in that second and 0.880 without; CPU 0.59 ms per draw; 102 tiles
  drawn (1-plot tiles). Against 3207's 0.775 ms: +7 % on a machine shared with two other games, no hold: **not a timing
  result, and 3021's 1.8-2.1 ms is not seen in this build** (the server's own bake, 1-plot tiles, ring + fence).
- **Server private bytes** (memsampler `--server`, psutil): 2,043.5 MB 20 s after the load (3306 r2 with the EXE-cache tap:
  1,329 MB; the residency's arrays ~363-405 MB plus the pipeline's install, tables and textures); turns 350-359 2,091.8 ->
  2,153.5 MB, **+6.87 MB per turn [4.10, 9.63]** (second half -0.58 [-10.3, +9.1]; the re-bake stream above grew the arrays
  362.6 -> 405.2 MB); the EXE +3.63 [0.66, 6.60]. **Per load (bug 0121, acceptance E)**: before 2,153.5 -> reload 1
  **2,477.0** (+323.5) -> 2 **2,507.1** (+30.1) -> 3 **2,420.4** (-86.7) -> 4 **2,434.8** (+14.4) -> 5 **2,431.7** (-3.1):
  one step at the first reload (3306 r2: +289 MB), then flat within +-90 MB. Video local after each load 1,270-1,490 MB
  (the driver demotes and pages). One run.
- **Pacing before the first publish (item 3), live**: idle-paced from 4.8 s (renderer ready) to 42.4 s (the composite's
  first publish, front end): 1,832 waits (20.6 ms each), against 3306 r1's ~880 Hz poll; event pacing from then on through
  the load screen (82,905 event waits, 5,649 of them 100-ms timeouts).
- **Per-boot names, live**: one boot in this run (`Local\VP64Shell_9068`, the first boot's names); the composite opened
  the section from the environment. A second proxy boot in one EXE process was not made.
### 4.9 Timing series B and C: lockstep records and frame time (issue 3307, W7, 2026-11-13) [L]

**Results (2026-11-13 13:00-14:45 EST, priority window, six launches, one quiet-lock hold each: p1-ring, p1-v3, p1-stock,
p1-ss, p2-stock, p2-ring; grade A inside a launch: 6 interleaved windows per arm, load recorded, the release summaries
quoted below) [L].** Tables: `shell/evidence/3307/results_x100.md` (the figures below) and `results_all_seconds.md`;
windows `shell/evidence/3307/windows/*.json`; raw (per-frame CSV, logs) `D:/c5-64bit-work/3300/w7/live/series/`.

| Frame time, all four phases (ms; mean over 6 windows, sd) | launch 1 | launch 2 | against `d33pt` of the same launch, paired (95 % CI; detectable) |
|---|---|---|---|
| D33 passthrough `d33pt` (no server picture) | ring 3.144 (0.069); v3 3.179 (0.081); ss 3.099 (0.037) | ring 3.126 (0.036) | - |
| **ring + fence, lockstep, depth** | 3.101 (0.064) | 3.147 (0.024) | **-0.043 [-0.156, +0.069] (0.149); +0.021 [-0.007, +0.049] (0.037)** |
| ring + fence, no lockstep | 3.145 (0.055) | 3.180 (0.083) | +0.001 [-0.074, +0.077]; +0.054 [-0.042, +0.149] |
| ring + CPU sync, lockstep | 3.359 (0.102) | 3.354 (0.089) | +0.215 [+0.097, +0.334]; +0.228 [+0.115, +0.341] |
| version 3, lockstep, depth (server.exe) | 3.731 (0.037) | - | +0.551 [+0.462, +0.641] (0.119) |
| today's compositor (3207's arm: shell_server.exe, version 3, newest picture) | 3.610 (0.056) | - | +0.511 [+0.426, +0.595] (0.112) |
| the same with 3207's shell_server.exe 559BA487 | 3.625 (0.066) | - | +0.525 [+0.427, +0.624] |
| stock (D33 off, `render=0`, same proxy route; own launches) | 5.341 (0.175) | 5.298 (0.055) | ring + fence - stock: -2.24 [-2.42, -2.06]; -2.15 [-2.21, -2.09] (not interleaved) |

The two ring launches agree (fence p1 - p2: -0.064 [-0.174, +0.046]; cpu -0.012 [-0.154, +0.129]); so do the two stock
launches (5.34 / 5.30). By phase (ring + fence against `d33pt`): city idle -0.01 / +0.16 [+0.09, +0.24] (launch 2 alone
shows a cost there), city pan -0.07 / -0.02, sparse idle +0.01 / -0.02, sparse pan -0.10 / -0.04 ms.

- **Ring + fence with lockstep costs nothing measurable over D33 without any server picture**: frame time -0.043 ms
  [-0.156, +0.069] against `d33pt` of the same launch (all four phases; detectable 0.15 ms), and every phase inside
  +-0.10 ms. **The version-3 hand-over with lockstep costs +0.551 ms [+0.462, +0.641]** (detectable 0.12 ms), so the ring
  with the fence is **0.595 ms [0.470, 0.719] faster per frame than version 3 at the same features** (lockstep, depth).
  The hand-over cost named: version 3 spends 0.50 ms of the EXE's CPU per frame in the composite (keyed-mutex
  `AcquireSync` + the colour and depth copies; `comp_mean`) plus 0.06 ms of lockstep wait per frame; the ring 0.022 ms
  and 0.0002 ms. (3207 measured the same +0.67 ms with 0.58 ms `comp_mean` for version 3 without lockstep.)
- **Ring + CPU sync costs +0.215 ms [+0.097, +0.334]** over `d33pt`, all of it in the sparse view (+0.33 / +0.38 ms idle /
  pan; city +0.15 / +0.01 n.s.), and is 0.26 ms [0.12, 0.40] slower than the fence. Its waits are rare (0.005 ms per
  frame) and its composite CPU is the ring's (0.027 ms): the cost is not on the EXE's CPU. [I, not profiled: with CPU
  sync the server's draw is flushed and waited for while the EXE's frame is on the GPU, adding a GPU context switch in a
  frame of 2.3-2.6 ms; with the fence the EXE's queue waits for the server's draw at the insertion.]
- **Lockstep with the fence: 99.997 % of frames drawn from their own camera** (67,517 of 67,519; 2 older by one frame;
  0 none), waits in 0.03 % of frames (23; p50 0.72, max 1.02 ms), 1 wait ended 1.27 ms after its deadline. Turnaround
  (publish -> READY, frames taken exactly) p50 **0.767 ms**, p95 0.930, p99 1.044, p99.9 1.241, max 3.52 ms; split p50:
  publish -> server woken 0.015 ms, woken -> submitted 0.753 ms (the draw's CPU and its submission), submitted -> READY
  0.002 ms (the fence: READY at submit). UpdateViews -> insertion p01 1.05, p50 1.89 ms. CPU sync: 99.989 % exact (7
  older, ages 1-2), waits in 2.17 % of frames (p50 0.19, p99 0.84, max 1.40 ms), turnaround p50 1.568 / p99 1.955 /
  p99.9 2.200 ms (submit -> READY 0.96 ms: the server's GPU wait). Version 3 with lockstep: 99.991 % exact (5 older, ages
  1-4), waits in 19.3 % of frames (35 % in the sparse idle view: its turnaround p50 1.58 ms is longer than the sparse
  view's UpdateViews -> insertion of 1.80 ms in many frames), p50 0.41, p99 0.72, max 1.88 ms; 5 late (one 17 ms after
  its deadline), turnaround p50 1.598 / p99 2.103 / max 21.1 ms. Launch 2 repeats it: fence 99.999 % exact (68,098 of
  68,099), 3 waits, turnaround p50 0.776 / p99 1.032 / p99.9 1.193 / max 2.42 ms; CPU 99.995 % (61,827 of 61,830), waits
  in 2.58 % of frames, turnaround p50 1.573 / p99 1.961 ms.
- *(Corrected 2026-11-22 by epic 0673, `pan-coherence.md` 2: with units and models on, the server's frame passes 3,400 us in 46-54 % of frames at 30 fps; the proposed default is 8,000 us. The line below held for bare terrain.)*
- **The wait bound's default from the data: keep 3,400 us.** With the fence the turnaround's p99.9 is 1.24 ms and the
  longest wait 1.02 ms, so any bound above ~1.5 ms gives the same exact share; 3,400 us also covers CPU sync (p99.9
  2.20 ms) and version 3 (p99.9 2.35 ms) and costs only on a real miss (2 in 67,519 frames with the fence). Lowering it
  buys nothing measurable [I from the distributions].
- **Lockstep's own cost** (fence with `lockstep=1` against `lockstep=0`, same launch): -0.045 ms [-0.164, +0.074]: none.
  **Pan lag** (camera age of the inserted picture, from the records; 9038's image method needs the EXE's hex highlight,
  which the D33 set does not draw, and was not run): with lockstep 0 for every exact frame (mean 0.000-0.008 ms over all
  phases; the few older frames 8-75 ms); without lockstep exactly one frame (the camera of the previous Present: 3.9 ms
  at the city, 2.4-2.6 ms in the sparse view), i.e. ~1.7 world units at the pans' 450 world units/s (median camera
  speed, p90 560).
- **Against stock** (separate launch, not interleaved with the D33 launches: grade B for these lines): stock (D33 off,
  `render=0`, the same proxy route) 5.34 ms (sd 0.18; idle/pan city 5.44/6.04, sparse 4.60/5.29) against ring + fence
  3.10 ms: **-2.24 ms [-2.42, -2.06]**; version 3 with lockstep -1.61 ms; `d33pt` -2.16 to -2.20 ms. D33 removes far
  more from the EXE's frame than the server's picture adds (3021: -1.49 ms without the proxy route).
- **Server GPU per draw: not regressed in server.exe** - 0.744 ms (fence), 0.733 (cpu), 0.803 (fence_nols), 0.774
  (version 3 with lockstep) at 106 tiles, against 3207's 0.775 ms (sd 0.004-0.017 over windows; CPU 0.55-0.62 ms per draw).
  The 1.66-1.76 ms of the dry runs (11:55-12:08, machine at 53-74 % CPU with other teams' games) and 3021's 1.8-2.1 ms
  were taken with other games on the GPU: GPU timestamps of a draw count the time other processes' work holds the GPU
  [I]. **No build regression either** (launch ss, 13:52-14:13, 3207's arm in one game, server process switched per
  window): 3207's shell_server.exe 559BA487 0.825 ms (sd 0.008), this branch's B8634672 0.826 ms (sd 0.010), the same with
  `--depth 1` 0.818 ms (sd 0.015) per draw at 106 tiles; frame time of the three equal within 0.02 ms (detectable
  0.15 ms). So the 1.8-2.1 ms of 15.7 (shell-texture file) came from the conditions of those holds (other games on the
  GPU), not from a commit [I: GPU contention not measured directly].
- **Today's compositor reproduced** (3207's arm: D33 + shell_server.exe, version 3, no lockstep, no depth; launch ss):
  **+0.511 ms [+0.426, +0.595]** over `d33pt` of the same game (detectable 0.11 ms; city +0.89 / +0.76 idle / pan,
  sparse +0.13 / +0.27), composite CPU 0.41 ms per frame (3207 on the old pins: +0.670 ms, `comp_mean` 0.58 ms).
  **Ring + fence against it: -0.554 ms [-0.676, -0.432]** per frame (Welch on the two launches' paired differences,
  detectable 0.17 ms; launches not interleaved); ring + CPU -0.295 ms [-0.422, -0.169]; version 3 with lockstep and depth
  in server.exe costs the same as today's compositor (+0.04 ms [-0.07, +0.15]). **Why it moved**: the ring removes the
  composite's keyed-mutex `AcquireSync` and its copy into a local texture (composite CPU per frame 0.41-0.50 ms -> 0.02 ms;
  the EXE samples the slot directly) and, with the fence, the EXE's CPU never waits for the server's GPU (READY at
  submit, the GPU orders the read): the whole hand-over cost of version 3 disappears from the frame.
- **v3 without lockstep is not 3207's arm in server.exe** [C, L]: `v3_today` (`handover=v3 lockstep=0 depth_write=0`) let
  the render host draw only ~10 times a second: `srv_render_host.cpp` (~599) stays in event pacing for good once the
  publish block's seq is non-zero and waits on `<base>.Cam` with a 100 ms timeout; a composite with neither lockstep,
  the ring nor depth writes no publish block, so every camera waits for the timeout. The product never runs that way
  (ServerRenderer always has the source path), but a composite whose ini drops to that state at run time leaves the
  terrain 100 ms stale. `v3_today`'s frame time (equal to `d33pt`) is therefore not a hand-over measurement. (Fixed
  2026-11-13 by issue 3312, 4.12: the host falls back to the camera-block poll when Presents advance without a publish;
  the series above was taken before the fix.)
- **Long frames** (W6's question): in the windows (no turn ended) 2-7 frames over 250 ms per arm in ~180 s, in every
  arm including `d33pt` and stock (seconds with a frame over 250 ms: ring arms 2-7, `d33pt` 3-5, stock 4): the EXE's own,
  0.1-0.9 s, at random points of idle and pan phases. During 3 AI turns (350 -> 353) per launch: ring + fence 101 frames
  over 100 ms and 7 over 250 ms (max 1,085 ms), launch 2 109 / 5 (max 1,052 ms); version 3 + lockstep 104 / 4 (max
  1,041 ms); stock 60 / 8 seconds with such a frame (no per-frame records in stock). In launch 2's windows only one frame
  over 100 ms in all arms (166 ms, fence without lockstep). No hand-over stall: no arm stands out against `d33pt` or stock.
  Because these frames move a phase mean by up to ~0.4 ms in any arm, the frame-time figures above leave out the 1-s
  stats lines that hold a frame over 100 ms (`analyze_3307.py --exclude-long 100`; rule set after p1-ring, applied to
  every arm alike); with every second kept the ring and version-3 results are the same within 0.03 ms except where a
  `d33pt` window held a 4.5-s mean (v3 launch, city idle), which widens that launch's intervals.

Disturbance (release summaries): p1-ring 13:00-13:22 total CPU mean 23.4 %, max 41.2 %, others chrome x9 (50 % of one
CPU), claude.exe (49 %); one-off 9050's one-core busy-wait ~13:16-13:19 (rounds 4-5: without them cpu +0.199, fence
-0.026, fence_nols +0.062 ms against d33pt: unchanged, no repeat needed). p1-v3 13:22-13:39 mean 21.0 %, max 45.3 %, no
other owner. p1-stock 13:42-13:52 (the take waited 13:39-13:42 for one-offs 9050/9051's suites) mean 23.8 %, max 49.3 %.
p1-ss 13:52-14:13 mean 22.5 %, max 39.3 % (chrome x3 to 78 % of one CPU). p2-stock 14:13-14:23 mean 22.0 %, max 34.1 %.
p2-ring 14:23-14:45 mean 22.1 %, max 32.7 %, nothing else busy. No hold was taken with `--overlap`. Free commit 35.8-38.9
GB. Every launch: guard check 0 at launch, in game and at the end, start-up watch ok, harness stop 0. c5h-a restored by
hash at 14:45 (backup verify equal/absent, provision verify 0); the view-mask marker the last D33 launch wrote (not in
the standard backup set) removed by hand. Version 3 was not repeated in launch 2 (the window ended).

**Method.** Driver `shell/evidence/3307/run_3307.py` (prep, check and restore are 3306's driver's, with this issue's
names), analysis `shell/evidence/3307/analyze_3307.py`, chain `series.sh` / `window.sh`. Instance c5h-a (Goldberg, C: with the
Defender exclusion), DX11 EXE 04CFDC71..., window 2554 x 1411, RTX 4080; save copy `C5H-3307-HUGE_0350` (of
`C5H-VP8P-HUGE_0350`, SHA-256 c499d3c6..., turn 350). Builds of the team branch at 9a2a63e (code unchanged by the later
`main` merge): server.exe 42D07160..., proxy 1DA8143E..., composite 5F531FB8..., shell_server.exe B8634672...; the x64 DLL
of the pin layout `x64-76992d23-p1607d42` (FA75F483..., COMMON 18). Proxy route as 3306 r2 (`vpproxy.ini mode=on
local_answers=1`, server.cfg by `server_config=`; the composite and the view-mask write by the 9040 test markers).
**D33 with Overlay kept on** (decision D93, bug 0127: darkred's D33 list with `on=["Overlay"]`) in every D33 launch.
One launch per hand-over the server is booted with; inside a launch the arms are composite ini keys (re-read every
500 ms; a `ring_sync` change makes the ring source ask for a new generation with that sync, which `ring_sink.cpp`
honours: `reqSync`), interleaved per round with the order rotated:
- launch **ring** (server.cfg `render=1 render_handover=ring render_ring_slots=4`): `fence` (ring + fence, lockstep),
  `cpu` (ring + CPU sync, lockstep), `fence_nols` (ring + fence, `lockstep=0`: the cost of the wait), `d33pt` (composite
  `mode=passthrough camera=0`: D33 without any server picture; the server draws nothing);
- launch **v3** (`render_handover=v3`): `v3_ls` (version 3 with lockstep and depth), `v3_today` (version 3,
  `lockstep=0 depth_write=0`: the compositor of 3207's +0.67 ms), `d33pt`;
- launch **stock** (`render=0`, darkred's stock config, no view-mask marker; the composite at `mode=passthrough` only to
  measure, as 3207);
- launch **ss** (`render=0`, D33, 3207's version-3 arm with an external `shell_server.exe --scene terrain` on a hidden
  desktop): `ss_old` = 3207's shell_server.exe 559BA487..., `ss_new` = this branch's B8634672..., `ss_new_d1` = the
  same with `--depth 1`; the server process is switched per window (the GPU-per-draw question).
All composite arms with lockstep or the ring: `depth_write=1`, `lockstep_bound_us=3400`, `lockstep_csv=1`,
`lockstep_check=0`, `insert=tagged tag_rule=after tag_match=clear_bb tap=1 bake_tap=1`. One window per arm and round:
city view (`UI.LookAt` plot (100, 30), 4-s settle) idle 8 s, then panning 8 s (posted RIGHT / LEFT holds of 1.5 s,
alternating, so the view does not drift), then the sparse view (the most open revealed ocean the driver's Lua finds:
plot (0, 10), 10 lists per frame against ~70 at the city) idle 8 s and panning 8 s. 6 rounds. Frame time per phase =
the frames-weighted mean of the composite's 1-s `stats` lines lying wholly inside the phase (3207's metric); lockstep
figures from the per-frame records; the server's GPU / CPU per draw from `render.log` `terrain 1s` lines (draws-weighted).
Then 3 AI turns (350 -> 353) with the launch's first arm (long frames during turns), then the harness's safe stop.
Each launch is one quiet-lock hold (`quietlock.py run --take --minutes 30`).
### 4.10 Depth against the stock EXE at the same camera (issue 3310, W10, 2026-11-13 11:53-12:45 EST) [L shots, C analysis, I causes]

**Set-up.** The workstation, instance c5h-c (Goldberg, install `%USERPROFILE%/c5-instances/c5h-c/install`), DX11 EXE
04CFDC71..., CP/VP/EUI, window 2554 x 1411, RTX 4080. **Asset set (D94): Vibrant Textures present**, `Assets/DLC/VT` 55
files, 47,300,858 B, folder hash `70402AD78740961507F2C806DDD3CC95817AA6D7F44AD846D76E974C78F68BA4`
(= the full pack `45b3b433...` by 9049's method, which the project cites; 4.13, 2026-11-13)
(`shell/evidence/3310/vt_hash_3310.py`: SHA-256 over the sorted lines "relative path TAB file SHA-256"); all arms ran on
this one install, so every comparison below is server against EXE on the same asset set. One save copy
`C5H-3310-HUGE_0350` (of the instance's `C5H-VP8P-HUGE_0350`, SHA-256 C499D3C6..., turn 350) in every launch. Driver
`shell/evidence/3310/run_3310.py` (prep / run / restore per arm; the joined arm through `run_3306.py`'s prep, check and
restore), analysis `compare_3310.py`, `cam_shift_3310.py`, `tables_3310.py`; small results `summary_r1.json`,
`summary_r2.json`, `cam_shift*.json`, `tables.md` beside them; shots and difference images only on
`D:/c5-64bit-work/3300/w10/` (D83). Functional runs under `quietlock.py run` (no hold); guard check 0 at launch, in game
and at the end, start-up watch ok, harness stop 0 and restore by hash (backup verify equal / absent, provision verify 0)
in all five launches; 0 LOUD lines in server.log, vpproxy.log and render.log of both joined launches; no crash (bug 0127
not seen, Overlay on). Three arms:
- **joined**: the proxy route with the renderer in server.exe, as 3306 r2 (W6's builds b1 of 129eeee: server.exe
  4461958A..., proxy 1A4D44E5..., composite CE86E00E..., x64 DLL FA75F483... of `x64-76992d23-p1607d42`), `render=1 ring
  fence`, composite `lockstep=1 depth_write=1 handover=ring`; D33 by the 9040 markers **with Overlay kept on (D93)**:
  darkred.py's list minus Overlay. Shots per camera: depth_write=1, depth_write=0, passthrough (the EXE alone: every
  model's full silhouette on the clear grey (84, 84, 84)); round 1 one of each plus a second passthrough, round 2 three
  of each.
- **black**: the 32-bit pin DLL `i0ship-bbfacca1-ia` (E589306E...), no proxy, no composite; darkred.py's D33 list minus
  River, Arrows and Overlay, **without** the viewmask marker, so the EXE draws its own terrain through its own depth
  buffer while FluidFOW is off (the configuration of terrain-surface.md 5 (2), there with Overlay off).
- **stock**: the same DLL, no proxy, no composite, the instance's own config.ini (D33 off).

**Cameras [L].** Round 1: the load view, 2 and 2 more wheel ticks in at the client centre, LookAt (100, 30), 4 ticks in,
LookAt (99, 31); round 2: the load view, 1 tick in, LookAt (89, 29), LookAt (94, 16). Two ticks already reach the closest
zoom: `load_in2` = `load_in4` and `v1` = `v2` byte for byte in every launch (so v1/v2 here are at the closest zoom, not
3200's default-zoom v1). Only the load view repeats its camera bytes across launches (as 3206 found; also equal to W6's
r2 load camera). The other cameras differ in the view matrix by up to 0.06 (element), but that moves a ground point by
**at most 0.04-0.63 px** on screen (median 0.01-0.45 px; `cam_shift_3310.py`: the plot centres within 16 of (100, 30) at
the ground truth's heights, projected with each launch's camera). So every view here compares within 0.63 px (note to
shared-texture-and-compositor.md 14's "1-2 px misregistration": not what these cameras show [L, 2026-11-13]).

**What could not be separated [L, negative results].**
- *Static models in the black arm*: with Overlay on, the EXE's terrain draws in the FOW base colour (64, 0, 0), exactly
  the colour of D33's static models (5th/50th/95th percentiles of the black shot equal on building pixels, on hidden
  building pixels and on the terrain ring around them). A building pixel looks the same hidden or visible. The black
  terrain of terrain-surface.md 5 (2) needed Overlay off, which D93 forbids. Units, ships and the EXE's UI stay separable
  (normal colours over dark red).
- *Land units across launches*: the figures of a unit stand at other places and in other poses in each launch (the
  machine-gun crew on Asyut, the cavalry at Memphis, the infantry at (86, 31): crops compared by eye), and idle animation
  moves them between shots seconds apart (11-18 % of a textured unit's pixels differ by more than 24 between two shots of
  one state). A per-pixel comparison of land units between two launches measures their placement, not the depth. Ships
  keep place and pose (bobbing aside) and are compared.
- In the black arm the EXE draws no water far from the coast (clear grey, e.g. the north-east of the load view; stock
  draws water there): ship pixels over such ground are left out.
- One joined shot (round 2, `load_in1` depth_write=1, the first of three) holds the server's picture alone, without any
  EXE content or HUD; the next shots of that view are normal: a capture between the composite's insertion and the EXE's
  lists [I]; set aside as `*.anomaly-no-exe-content.png`.

**Results per camera.** Model pixels = stable in every passthrough shot, eroded by 1 px, HUD out. "Hidden by the server"
on the server's flat colours (water (42, 74, 110), magenta fallback) is exact; on cached tiles a pixel counts hidden when
every depth_write=1 value differs by more than 24 from every model observation (depth_write=0 and passthrough shots).

| View | Camera vs joined, max px (black / stock) | Static px | static hidden by the server | Units / ships / UI px | hidden by the server |
|---|---|---|---|---|---|
| load (r1) | 0.0 / 0.0 | 96,249 | 8,701 (9.0 %) | 95,076 | 6,094 (6.4 %) |
| load (r2) | 0.0 / 0.0 | 98,390 | 8,857 (9.0 %) | 82,822 | 4,292 (5.3 %) |
| load_in1 (r2) | 0.036 / - | 115,296 | 10,398 (9.0 %) | 127,965 | 3,684 (2.9 %) |
| load_in2 = load_in4 (r1) | 0.115 / 0.237 | 164,515 | 15,231 (9.3 %) | 141,167 | 6,354 (4.5 %) |
| v1 = v2 at (100, 30), closest zoom (r1) | 0.583 / 0.628 | 466,291 | 70,299 (15.1 %) | 88,793 | 14,288 (16.1 %) |
| v3 at (99, 31) (r1) | 0.019 / 0.454 | 510,193 | 46,073 (9.0 %) | 147,306 | 44,739 (30.4 %) |
| hill (89, 29) (r2) | 0.295 / - | 327,831 | 21,629 (6.6 %) | 133,519 | 2,409 (1.9 %) |
| hill (94, 16) (r2) | 0.352 / - | 156,138 | 22,743 (14.6 %) | 141,566 | 647 (0.5 %) |

The load view gives 9.0 % in both rounds (two launches): the server's cut of static models repeats. (W6's 10.8 % at the
same camera counted without erosion or the stability test.) The "units / ships / UI" column on cached ground carries the
animation noise above (v3: cavalry and workers).

**Static models against stock, one direction.** Where the server hides a static model on a cached tile, depth_write=1
shows the server's terrain; if stock hides that pixel too, the stock shot shows terrain of about that colour (the bake
matches stock's terrain to a median 6-26, terrain-surface.md 5). Share of such pixels within 24 (40) of the stock pixel,
against the same share on plain terrain away from models:

| View | Static px hidden by the server on cached ground | terrain-like in stock | baseline on plain terrain | est. share stock hides too |
|---|---|---|---|---|
| load (r1 / r2) | 1,408 / 1,388 | 0.40 (0.54) / 0.41 (0.56) | 0.58 (0.79) / 0.58 (0.80) | **0.69 (0.68) / 0.71 (0.70)** |
| v1 = v2 | 70,299 | 0.32 (0.45) | 0.62 (0.74) | **0.52 (0.61)** |
| v3 | 45,362 | 0.26 (0.42) | 0.50 (0.64) | **0.53 (0.66)** |

(load_in2/4: 195 px, too few.) So **about 30-48 % of the static-model pixels the server hides are visible in stock**
[L, estimate]; at v1 about 30,000 px of 466,291. The other direction (stock hides, the server shows) is not measurable
with these arms. COMMON 25 numbers for the whole picture (largest channel, joined depth_write=1 against stock, HUD out):
median / p95 / max 68 / 221 / 255 (load), 104 / 232 / 255 (load_in2), 39 / 181 / 252 (v1), 74 / 211 / 255 (v3); stock's
own two shots 0-5 / 1-50 / 225-252. These measure D33's colouring (dark red models, magenta on uncached tiles, the
server's flat water colour) and the systems D33 leaves out, not depth. Difference images:
`D:/c5-64bit-work/3300/w10/cmp-r1/*_absdiff_joined_stock_x2.png`.

**Ships against the black arm (the EXE's own water and terrain depth), same camera.** Pixels of ships (and embarked
units) decided in both arms:

| View | Ship px | visible in both | hidden in both | hidden only by the server | hidden only by the EXE | distance only-server / only-EXE, median / p95 / max px |
|---|---|---|---|---|---|---|
| load (r1) | 31,673 | 28,415 | 192 | 855 | 2,211 | 43 / 105 / 123 ; 47 / 83 / 92 |
| load (r2) | 24,044 | 21,387 | 154 | 474 | 2,029 | 95 / 210 / 220 ; 85 / 176 / 254 |
| load_in1 (r2) | 45,958 | 42,684 | 251 | 1,174 | 1,849 | 71 / 270 / 284 ; 92 / 150 / 160 |
| load_in2 = load_in4 (r1) | 45,517 | 42,039 | 263 | 2,193 | 1,022 | 118 / 339 / 382 ; 96 / 155 / 161 |
| hill (94, 16) (r2) | 31,441 | 30,604 | 238 | 24 | 575 | 43 / 65 / 68 ; 144 / 161 / 181 |

Per ship (`tables.md`): at the default zoom the EXE hides more of the hull than the server for 7 of 9 ironclads and
cruisers near the camera (3.8-7.9 % against 0.5-3.2 %); at the closest zoom the far ships at the top of the screen are
hidden more by the server ((107, 29) 18-23 % against 0-4 %; (109, 29) 16-17 % against 1-4 %). The distance from an
only-pixel to the nearest pixel the other arm hides is tens of pixels: the two arms hide different strips of a hull, not
the same cut shifted by a pixel.

**Units on slopes and the D24 budget.** Units on hills in the joined launch (the server's depth; three shots each in r2):
the machine-gun crew on Asyut (110, 26) 0.2-0.7 % of its pixels hidden at four cameras, the Great War infantry on
(89, 29) 0.5 % and on (110, 23) 3.3-5.6 %, the machine gun and work boat on Akhetaten (94, 16) 1.5 %. The comparison
with the EXE's own cut is not possible for land units (above). **The D24 precision is not a visible factor [C, from the
cameras]:** the projection has near 50.0 and far 4,000 (proj `-1.0127`, `-50.633`), so one D24 step (2^-24 in NDC z) is
a depth step of `w^2 / 50.633 * 2^-24`: 0.00051 world units at the load view (median camera distance w = 661 at the
plots) and 0.00018-0.00028 at the zoomed views (w 391-487); 5 steps = 0.0026 / 0.0009-0.0014 world units = **0.006-0.011
px** on a surface facing the camera (0.143-0.242 world units per pixel). Even a 10-fold magnification at a grazing slope
stays near 0.1 px. The 1-5 step difference between the server's float z and the EXE's D24 (section 4.3) moves no cut by
a pixel; the differences above come from the surfaces themselves.

**Causes, per class.**
1. *Different surfaces under static models* (30-48 % of the server-hidden building pixels are visible in stock; Memphis
   at v1 15 % hidden against 9 % elsewhere). *Settled 2026-11-13 by issue 0134 (4.14): neither (a) nor (b); the server
   drew HeightAtGrid's u16 / 255 * 0.2 where the EXE draws its 8-bit page, (u16 >> 8) * 0.2, so the server's surface stood
   0.05-0.25 higher everywhere (0.19 on flat land). The text below is the hypothesis as written.* [I, not separated] (a) the EXE's city-field manager flattens and blends the
   terrain under city footprints (TF_Small/Large_City stamps, `research/sources/shell-gates/stubs/landmark.md` 4) while the
   server's field comes from the tap's height samples; whether those samples carry the stamps is not established; (b) the
   EXE's LOD choice (LodMip >= 1, terrain-surface.md P17) against the server's mip 0; (c) camera misregistration up to
   0.63 px (small against the tens of pixels seen).
2. *Water*: the EXE hides more of a near ship's hull, the server more of a far ship's. [I] the server draws a flat water
   plane at its water height; the EXE's water surface and its far-water handling differ (in the black arm the deep ocean
   is not drawn at all). Seam: the server's water surface against the EXE's water system.
3. *Intended differences (D81 point 5) seen in the stock picture*: trees (Forest is off in D33 and the server draws none:
   at v3 the stock jungle hides the workers that the joined picture shows), rivers, culture borders, shadows, the magenta
   fallback on tiles without a bake, D33's dark red models and the server's flat water colour.
4. *Not depth*: land units' placement per launch; animation; translucent D33 models (faded far models, a dark red
   translucent feature on the water at Akhetaten), left out of the unit counts.
D93's question: no EXE-drawn overlay over the server's terrain was seen in the joined shots looked at (load, load_in4,
v2, v3, the two hill views; half-size and crops); culture borders appear only in stock.

**Not done.** The reverse direction for buildings (stock hides, the server shows) and the EXE's own cut of land units
need a reference that separates models from the EXE's terrain with Overlay on: a white FOW mask (3011's `whitemask=2`
test DLL) or stock-coloured terrain with dark red models, neither on the pins. The cause of class 1 (city stamps in the
tap's field or not) is one read of the field under a city plot against the EXE's drawn surface.

### 4.11 Final acceptance on the new pins (issue 3311, W12, 2026-11-13 14:54-15:54 EST) [L as marked]

All runs of 4.7-4.10 used the old pin `x64-76992d23-p1607d42`; these use the pins of D96 (COMMON 18). The batch changes
game results, so nothing below is compared across the pin change except where it says so (memory context, labelled).

**Set-up [L].** Workstation, instance c5h-b (Goldberg; `%USERPROFILE%/c5-instances`, Defender exclusion), DX11 EXE
04CFDC71..., CP/VP/EUI, window 2554 x 1411, RTX 4080. **Asset set (D94): Vibrant Textures present**, `Assets/DLC/VT` 55
files, 47,300,858 B, folder hash `70402AD78740961507F2C806DDD3CC95817AA6D7F44AD846D76E974C78F68BA4` by 3310's method (=
`45b3b433...` by 9049's, 4.13) (the same as 3310's
c5h-c). Save copy `C5H-3311-HUGE_0350` (of the instance's `C5H-VP8P-HUGE_0350`, SHA-256 C499D3C6..., turn 350; removed
after each run). Builds b1 of the team branch at bd00d2b (repo_dirty=0), 14:55-14:57, `server/build_server.py --src
D:/c5-64bit-work/9045/src-x64f --deps D:/c5-64bit-work/9045/bF/deps --config release --offline` (the plain source copy's
fingerprint matched 04b1211b8; 9045's inputs work as they are): server.exe `379E4363...`, proxy `F7FFB706...`, composite
`1EE88DA2...` (`shell/build.cmd`); the x64 DLL and libraries of the layout `x64-04b1211b-pa803776` (DLL `74588135...`, equal
to the deps build's own DLL). Driver `shell/evidence/3311/run_3311.py` (3308's product prep / run / restore plus D93's shots,
the residency's counters per turn and the control arm), `summarize_3311.py`, `gpu_windows_3311.py`, `stock_3311.py`;
small results `shell/evidence/3311/summary_{p1,c1,s1}.json`, `memsampler_*.txt`, `feed-per-turn_p1.tsv`,
`gpu_windows_p1.json`; raw logs, per-frame CSV and shots only in `D:/c5-64bit-work/3300/w12/live-*/` (D83). Functional runs
(no quiet-lock hold); W11's suites (3312) ran on the machine at the same time.

**Arms.**
- **p1, the product path** (14:59:25-15:27:38): `VP64Options.ini [Renderer] ServerRenderer = 1` (radius default -1), the
  composite and its ini beside the proxy with the manifest of `shell_manifest.py` (check 0), no `crashlogs\shell.helper`, no
  view-mask marker, no `server_config=`; the proxy wrote `render=1 render_section=Local\VP64Shell_6500 render_handover=ring`;
  the server baked its tiles itself (Residency over the pipeline baker, `render_tiles=bake`) **with the feed filter of 3308
  live for the first time**; composite `mode=composite tap=1 bake_tap=0 insert=tagged tag_rule=after tag_match=clear_bb
  camera=1 lockstep=1 lockstep_csv=1 depth_write=1 handover=ring ring_sync=fence`; **D33 by darkred.py with Overlay on (D93)**:
  the proxy's view-mask write logged config `0xFF040667`, view `0xFF040666` (9046's arm C). After the load: D93's shots, then
  30 AI turns (350 -> 379), then five loads of the save in the same EXE process (`tap_live.py reload`).
- **c1, the render=0 control** (15:28:42-15:46:42): the same product set-up and files, plus `vpproxy.ini server_config=` naming
  the server.cfg the proxy had written for p1 with `render=0` and `run_dir` inside the instance (the proxy logged "render=1
  and render_section=... are NOT added"); the composite loaded as in p1 (no server pictures: every frame `none`); 30 AI
  turns, no shots, no reloads (as 3306's r3).
- **s1, the stock reference for D93** (15:47:30-15:52:51, `stock_3311.py`): the same proxy route, pins and save copy with
  `ServerRenderer = 0` (the proxy logged "no composite, no view-mask write, no render keys") and the instance's own config
  (`darkred.py --arm stock`: only OverlayDebug and ColorKeyEdit off): the EXE draws everything itself. Only D93's shot
  sequence after the load, no turns. Guard 0 / 0 / 0, start-up watch ok, 0 LOUD in server.log and vpproxy.log, stop 0,
  restored by hash (backup verify equal / absent, provision verify 0). (Its `summary_s1.json` says `arm: product`: the
  label of the shared run code; the set-up is the one named here.)

**A [L].** p1: 30 AI turns, every `turns.py end` / `wait` exit 0 (21-38 s per turn, median 27.9; not a timing result),
**0 LOUD lines** in server.log, render.log, vpproxy.log and shell_composite.log, no DISABLED or EXCEPTION line of the
composite, no crash, no dialog; guard check 0 at launch, in game and at the end, start-up watch ok, harness stop 0
(stop_readiness); instance restored by hash (backup verify equal, provision verify 0). c1 (15:28:42-15:46:42): the same -
30 AI turns 350 -> 379, every step exit 0 (22-35 s per turn, median 28.3), 0 LOUD lines in server.log, vpproxy.log and
shell_composite.log (no render.log: no render thread), guard 0 / 0 / 0, start-up watch ok, stop 0; every frame `none` (107,662
at UpdateViews: no server picture, nothing waited for).

**B-lite: lockstep and re-bakes with the filter [L, functional, no timing claim].** p1, the composite's `lockstep 1s` lines
summed over the 30 turns: 105,713 frames published at UpdateViews, **103,432 taken exactly (97.84 %)**, 2,281 older, 0 none;
2,435 waits, 263 ended more than 0.5 ms after their deadline (corrected 2026-11-13, 9055 4.1 / issue 0150: the
pre-0150 `late` counter, which also counted waits that began after the deadline; real overruns past max(deadline,
wait start) + 0.5 ms were 28 of 3,940 waits in the whole p1 file, max 0.95 ms), 16 give-ups and 16 recoveries; depth written in all 105,713;
11,333 frames without the scene-clear list (`noins`). Per turn the exact share was 96.0-99.8 %. 3308's r1 (no filter, old pin)
had 95.6 %; 3307's quiet-lock windows (no turns, no bakes) 99.997 %. **Re-bakes per AI turn with the feed filter: median
331.5, max 809, 11,054 in the 30 turns** (3308 r1 without the filter: ~700 per turn); the filter passed 795 of the 2,142
plots the feed named over the whole run, load and reloads included (1,347 filtered: fog, owner, city bit, first layouts), 3-52 passed plots per turn (median
13.5), each raising up to 35 tiles. **The older frames follow the re-bakes**: over the 30 turns the per-turn exact share
and the per-turn re-bakes correlate with r = -0.67 (turn 353: 84 re-bakes, 99.8 % exact; turns 364/369/374: 519-809
re-bakes, 96.0-96.8 %). [I, as 3308 read it: a camera that arrives while the render thread is in a 4-ms `idle` bake slice
is drawn past its bound; the draw's GPU time is the same in seconds with and without bakes (below), so it is the thread's
time, not the GPU. A lower `render_bake_budget_ms` or a bake slice that yields to a waiting camera would be the test.] *(2026-11-15, issue 0140, 4.18: the yield and the
stale-tile deferral are built and work; on the loaded workstation the exact share did not move measurably (pooled -0.06 pt
[-0.58, +0.46]); the turnaround p50 fell 0.6 ms in both orders; the quiet-machine test is still open.)*
Per-frame records of the whole run (`lockstep_csv_3311.py`, load, shots, turns and reloads; `lockstep_csv_p1.json`):
151,808 frames published at UpdateViews, 97.33 % exact, 3,975 older (age 1: 3,454; 2: 117; 3: 18; 383 of age 39-119, all at
frame 128,489 or later, i.e. in the reload phase [I from the frame numbers]), 87 none; turnaround publish -> READY of exact frames **p50 0.99, p95 1.54, p99 2.28, p99.9 3.80 ms**, max 76 ms
(3307 under the quiet lock without bakes: 0.767 / 1.044 / 1.241 ms): the tail passes the 3,400 us bound, which is where the
older frames come from; waits p50 0.27, p99 1.49, max 1.83 ms.

**Gray frames [L].** p1: bake-ahead after the first load complete 15.4 s after the field (4,922 land tiles, 4,972 bakes,
0 failed, 1,277.5 ms baker GPU time, 352.7 MB of caches in 362.6 MB of arrays); **20 gray frames, all before the bake-ahead
finished; 0 after** (163,758 frames counted to the end, turns and five reloads included). Each reload: a field of the same
size, 18-27 tiles changed, bake-ahead complete 162-223 ms after the field. 0 failed bakes in the run (19,111 bakes, 14,189
of them re-bakes; arrays 405.2 MB at the end).

**GPU ms per draw [L, shared machine, no hold: not a timing result].** render.log `terrain 1s` (timestamp queries around the
terrain draw), 102 tiles drawn (median): the 50 s after the load and before the turns **0.777 ms** median (p10 0.71, p90
0.86; 3207: 0.775, 3307 under the quiet lock: 0.744), the AI turns **1.047 ms** (p10 0.78, p90 1.17; 858 s), the same with
bakes in that second (220 s) 1.054 and without (638 s) 1.045, the reloads and load screens 1.062; CPU per draw 0.55 ms in
every window. The re-bakes do not move it; the turns do [I: GPU timestamps count other GPU work in the interval; W11's
composite suites drew on the same GPU during the turns, and the EXE's own frames during AI turns differ from the idle
view; 3307's finding that 1.8-2.1 ms came from other games on the GPU applies; not a build regression claim].

**E: memory per turn [L, one run per arm].** perf/memsampler (`--server`, 30 rows each, 0 skipped turns):

| | server.exe private after load | server private MB/turn, all 30 (95 % CI) | second half (365-379) | EXE private after load | EXE private MB/turn | EXE largest free block |
|---|---|---|---|---|---|---|
| p1 product, render=1 | 1,956.4 MB | **+3.11 [2.74, 3.48]** | +1.65 [1.11, 2.19] | 1,433.3 MB | +1.42 [1.06, 1.78] | 1,763 -> 1,676 MB |
| c1 control, render=0 | 219.9 MB | **+1.20 [0.97, 1.44]** | +0.58 [0.13, 1.02] | 1,437.3 MB | +1.53 [1.04, 2.02] | 1,790 -> 1,702 MB |

- **The renderer adds ~1.74 GB private on the huge save** (1,956.4 against 219.9 MB after the load; 3308 r1 on the old pin
  2,043.5): Residency's arrays 362.6 MB, the pipeline's install, tables and textures, the field and the device.
- **Growth per turn beyond the DLL's own: yes, with the Residency.** The server grew +1.91 MB per turn more with the
  renderer over the 30 turns (about [1.47, 2.35] from the two intervals) and **+1.07 [about 0.37, 1.77] in the second half**
  (3306's r2/r3 pair with the EXE-cache tap: +0.79 and +0.08, i.e. none in the second half). The Residency's own counters
  account for most of it: arrays 362.6 -> 405.2 MB in turns 350-379 (+42.6 MB, ~1.5 MB per turn, in steps of 10.6-10.7 MB
  each time the used slices pass the capacity: 8,643 -> 9,517 slices used) and caches 360.0 -> 396.4 MB, while the
  resident tile count stays 4,922 [L, render.log per turn]. So re-baked tiles end up holding more slices and more cache
  than their first bake. *(Answered 2026-11-13 by issue 0137, 4.14: every re-bake sets `areaChanged`, so a tile without t8
  gains one; bounded by 2 slices per land tile; fixed by holding the t8 slice from the first bake. Was: "[I: which tiles
  and why - a size class or mip count that differs ..., or slices not returned - ... not read here]".)* After the five
  reloads the arrays stay at 405.2 MB.
- The EXE grows the same in both arms (+1.42 and +1.53 MB per turn), as in 3306 (+2.03 / +1.72 on the old pin).

**Server private per reload (bug 0121) [L].** p1: before 2,089.7 MB -> reload 1 **2,435.9** (+346.2) -> 2 **2,499.3**
(+63.4) -> 3 **2,410.5** (-88.8) -> 4 **2,414.8** (+4.3) -> 5 **2,411.3** (-3.5); video local 1,290-1,504 MB after the loads.
The same shape as 3308's r1 on the old pin (+323.5, +30.1, -86.7, +14.4, -3.1) and 3306's r2 (+289, +4.6, -0.4): **one step
at the first reload, then flat within +-90 MB, the last three within +-5 MB**. The EXE after the loads 1,530.8 -> 1,559.5 MB.
*(2026-11-15, bug 0121: on the pinned release layout `x64-04b1211b-pfdee656` the one-time +300 MB step does not reproduce
(huge after 10 AI turns: +59 MB, then back at the pre-reload size; without turns +84 then a plateau; Tiny +80); what stays
after the first reload is the D3D11 driver's pools and 10-45 MB on the process heap's free list.
`research/04-memory-investigation/server-reload-memory.md`.)*

**D93's checks [L].**
- *What the EXE draws over the server's terrain.* Shots at the capital Thebes (101, 25), one camera (camera bytes equal
  before and after), each state in the composite (the joined picture) and in passthrough (the EXE alone over its clear grey
  (84, 84, 84), 66 % of the picture body): plain; `Events.SerialEventHexHighlight` with the named style "Culture" (one of
  the 43 styles of Highlights.xml, so no 0127 lottery) on the 25 plots around the capital, colour (0, 1, 0, 1); a land unit
  selected (`UI.SelectUnit`, a Worker at (102, 25)). **The EXE draws nothing on the ground in any state: no highlight, no
  selection hex, no movement range** - neither over the server's terrain nor in its own passthrough picture. Every changed
  pixel between the states (19,516 / 33,946 in the composite, 18,882 / 36,739 in passthrough, HUD bands left out) lies on
  animated units, the unit panel and the selected unit's flag frame (difference images looked at, and the crops at the
  city); 0 green pixels changed in the highlight state. City banners, unit flags and the dark red D33 models are the EXE's
  only content over the terrain, as in 4.7 and 4.10. [I, from 0127's reading: the highlight styles draw into the Terrain
  Overlay target that the Overlay start-up creates, and the EXE's terrain pass, which D33's view mask turns off, is what
  shows it; so D33 + Overlay keeps the EXE alive (0127) but shows no highlight at all.] **Product consequence: hex highlights,
  the selection hex and movement ranges (and culture borders, 4.10) are missing from the joined picture; the server must draw
  them, or the Terrain Overlay target must be composited.** *Corrected 2026-11-14 (epic 0167, `overlay-system.md` 1-3, [C+L]):
  the EXE does draw them under D33, into the Terrain Overlay target, which nobody samples because the EXE's terrain draw is
  off; epic 0167 taps that target and the server binds it as terrain t1 and water t9 (route (a), MC #696).* The same calls in the stock arm s1 (the camera of p1's LookAt
  within 0.04 world units of translation, rotation and projection equal; same Lua): the Culture highlights draw green hex outlines around Thebes (**156,350 green pixels changed** against 0 in p1),
  and the selected Worker gets the EXE's pale selection glow on its hex (looked at in crops; the 25 "off" calls left part of
  the outlines on screen at the next shot, a fade or a second style entry [not established]). So the calls work and the
  missing highlights are D33's.
- *Memory of Overlay on (against 9046's R3, "Overlay on alone, -17 MB, noise").* EXE private 20 s after the load, huge save,
  turn 350: p1 1,433.3 MB and c1 1,437.3 MB (new pins, Overlay on); the old-pin runs: 3306 r2 1,440.7 and r3 1,441.2 (D33
  with Overlay off, marker route), 3308 r1 1,423.3 (Overlay on, product path) [L, from those runs' run.json]. Overlay on
  costs nothing measurable in the EXE (all five within 18 MB, Overlay-on runs not higher), the same as 9046's R3.

**Verdict.** A passes in both arms on the new pins; B-lite: 97.8 % exact over the turns with the filter, the misses tied to
re-bakes; gray frames after the bake-ahead 0; E: the EXE grows the same with and without the renderer, the server grows
~1.1-1.9 MB per turn more with the renderer, mostly the Residency's arrays and caches growing with re-bakes (new, against
3306's tap-based pair); reloads flat after the first step (0121); D93: Overlay on costs no memory and keeps the EXE alive,
but the EXE then draws no highlight at all in D33.

**Not done.** A second proxy boot in one EXE process (per-boot names live); why re-baked tiles take more slices and cache
(Residency, `render/`); a re-bake budget test for the older frames; the GPU per draw of these builds under the quiet lock
(only the shared-machine figures above); drawing highlights, selection and movement range in the joined picture.
### 4.12 Follow-up fixes of wave 3 (issue 3312, W11, 2026-11-13) [C code, L stand-in]

**Pacing when the composite stops publishing** (3307's finding in 4.9). Before: once the publish block's `seq` was non-zero
the render host waited on `<base>.Cam` with a 100 ms timeout for good; a composite that went on presenting without
publishing (version 3 with `lockstep=0`, `handover=v3`, `depth_write=0`, which writes no publish block; or a publish path
that stopped) got one draw per timeout. Measured on the stand-in with the render host before the fix (`server/tests/render`
built from the parent of ed9c83d, 2026-11-13 15:01): **16 draws for 80 camera blocks** written at ~50 Hz after publishing
stopped, against 80 of 80 while publishing [L stand-in].
Now (`srv_render_host.cpp` `EventPacing`) [C]:
- the loop reads the camera block's `presentCount` (first) and the publish block's `seq` (second) at every iteration;
- the `presentCount` seen when `seq` last moved is kept; **more than 3 Presents past it with no `seq` change** = the
  composite presents without publishing: the host leaves event pacing and polls the camera block as before the first
  publish (the 0.5 ms timer while the scene has a camera to draw, else the `render_idle_poll_ms` wait that also ends on
  `<base>.Cam`; 3308's rule). 3 because a publishing composite writes the publish block once per frame, before or at that
  frame's Present, so between two looks `presentCount` runs at most 1-2 ahead of the last `seq` change;
- **the next `seq` change returns to the events** at once (the poll sees it within 0.5 ms);
- a composite that neither presents nor publishes (minimised, a load without frames) keeps the events and their 100 ms
  timeout: no 0.5 ms poll without Presents;
- while in the fallback the publish block's frame says nothing about the camera drawn: `DrawFrame` treats the frame as not
  lockstepped (no publish time, no deadline retry, no `camerasSkipped` from the gaps of version-3 frame ids);
- the server block's `pacing` says `VPPACE_LEGACY` during the fallback (the contract's values unchanged);
- counted: `host 1s:` gains `pace_fallback=0|1 pace_fallbacks=N pace_resumes=N waits_fallback=N` (cumulative; the waits of
  the fallback are also counted in `waits_legacy` / `waits_idle`), the summary `pace fallbacks N resumes N`; render.log
  notes `pacing: N Presents without a publish (presentCount A -> B, seq S): the camera block is polled until the
  composite publishes again (fallback K)` and `pacing: the composite publishes again (seq S, presentCount P): woken by
  <base>.Cam (resume K)`, the first ten of each, then every thousandth.
Tests (stand-in, `server/tests/render/test_render_host.py`) [L 2026-11-13]: publishing -> stopped -> resumed, 80 camera
blocks each at ~50 Hz: 80 / 80 / 80 draws, pacing EVENT / LEGACY / EVENT, 1 fallback and 1 resume; the boundary (3
Presents past the last publish keep the events, 4 fall back, the note says 4); Presents stopping with the publishes keep the
events (0 fallbacks, >= 10 timeouts); the event-pacing test now also asserts 0 fallbacks. With the host before the fix the
four fail (the stopped phase drew 16 of 80; the boundary test saw 12 / 19 draws and no LEGACY). The whole
`server/tests/render` suite: 77 passed, 0 skipped, plain (15:36-15:40) and under MSVC `/fsanitize=address` (15:41-15:46,
0 ASan reports; W8's environment, canary reported).

**The options header in the render units** (W8's note in 4.8): `build_server.py` RENDER_INCLUDES has `options/include`;
`srv_render_options.cpp` includes `srv_options.h` by name and declares nothing itself. server.exe built with it from this
branch (2026-11-13 15:03, `--src` 9045's `src-x64f`, fingerprint of 04b1211b8, `--deps` 9045's `bF/deps`, release; DLL
74588135..., server.exe AA977660...). Test: the unit compiles with `render_compile_args` and fails "file not found" without
the options folder.

**The shipped composite ini** (`shell/composite/shell_composite.ini`): `handover=ring`, `lockstep=1`, `depth_write=1`, the
product path's values (the proxy writes `render_handover=ring` for server.exe, 4.8). With the new ini and no test change
the composite regression subset (test_errors, test_lsprobe, test_standin, test_tagged, test_verification_defects,
test_tap, test_lockstep) had 36 failures, all tests that run a version-3 server (shell_server.exe, lockstep_server.exe):
so the two fixtures that start those runs (`run` in `shell/tests/conftest.py`, `lrun` in `test_lockstep.py`) set
`handover=v3 lockstep=0 depth_write=0` first, and each test sets what it wants on top [L 2026-11-13 15:36: 195 passed, 0
skipped, exit 0, shell and shell/frame built from this branch]. New tests: the shipped ini's three keys (each once), the
fixtures' keys, and the shipped ini as it is with `ring_producer.exe` (ring + fence, lockstep at UpdateViews, depth write:
exact frames, no CONFIG ERROR). Not changed and worth a look before packaging: the shipped ini keeps `tap=0`, while the
server's terrain needs the height-field tap (3308's product run set `tap=1`) [I, not run].

**hutest** (`proxy/tools/hutest/build_hutest.py`): the tolerance of a `GameplayNaturalWonderRevealed` LOUD line is gone
(the method is in the borrowed-argument list since bug 0124, issue 3309); only `GameplayUnitCombat` keeps its tolerance.
[L 2026-11-13 15:36-15:37, `D:/c5-64bit-work/3300/w11/hut.txt`; hutest built with 9045's `src-x64f` and `bF/deps`]: with
the proxy and server.exe of this branch (b1 above) **PASS** (46 checks, 0 failed, 0 unimplemented lines, exit 0) and
`--break` PASS (16 expected failures, none else, exit 0); the same hutest binaries with the unfixed wave-2 proxy
(`D:/c5-64bit-work/3300/w6/b-b1`) **FAIL, exit 1**, on exactly one line: the `GameplayNaturalWonderRevealed(ICvPlot1 *)`
LOUD line (46 checks passed there too).

### 4.13 Gray frames with pans, jumps and revisits at radius -1 / 5 / 0 on the product path; closing items (issue 3313, W13, 2026-11-13 15:58-) [L as marked]

**3408's last item, on the product path [L].** Workstation, instance c5h-b (Goldberg), DX11 EXE 04CFDC71..., CP/VP/EUI,
window 2554 x 1411, RTX 4080. **Asset set (D94): Vibrant Textures present**, 55 files, 47,300,858 B, VT hash
`45b3b433...` (9049's method; `70402AD7...` by 3310's, below). Save copy `C5H-3313-HUGE_0350` (of `C5H-VP8P-HUGE_0350`,
C499D3C6..., turn 350; removed after). Pins of D96: x64 DLL 74588135... (layout `x64-04b1211b-pa803776`); builds b1 of the
team branch at 64947df (16:00-16:02, `server/build_server.py --src D:/c5-64bit-work/9045/src-x64f --deps
D:/c5-64bit-work/9045/bF/deps --config release --offline`; `shell/build.cmd`): server.exe `8BA282C1...`, proxy
`AA0F9DE3...`, composite `03764F24...` (the build's DLL 74588135..., equal to the layout's). Product path as 3311's p1
(`[Renderer] ServerRenderer = 1`, composite + manifest beside the proxy, `render=1 render_handover=ring` written by the proxy,
the server's own bake: Residency over the pipeline baker; composite `tap=1 lockstep=1 depth_write=1 handover=ring
ring_sync=fence lockstep_csv=1`; D33 by darkred.py with Overlay on, D93), plus `BakedTileCameraRadius = R` in
VP64Options.ini (server.log: "BakedTileCameraRadius = R (from the file)"); one game launch per radius. Camera script: 3408's
`shell/evidence/3408/script_huge.json` (seed 3408: start at the capital (101, 25), 4 pans of 10 plots, one `UI.LookAt` every
0.15 s, then 24 jumps of which 8 revisit an earlier jump's plot; 29 steps), played with `bake_live.py`'s `do_step`, 4 s per
step, one half-size harness shot per step after the hold. Gray frames = the residency's own counters in render.log's
`terrain 1s:` line (a drawn frame with a visible land tile without its bake), attributed to the step whose window holds the
line; lockstep = the composite's `lockstep 1s:` lines over the same steps. Driver `shell/evidence/3313/run_3313.py`
(prep / radius / run / summarize / feed / restore, from 3311's and 3308's); results `summary_{rm1,r5,r0}.json` and per step
`steps_{rm1,r5,r0}.csv` beside it; raw logs and shots in `D:/c5-64bit-work/3300/w13/live/out-*` (D83). Functional runs, no
quiet-lock hold, nothing else of ours on the machine. Every launch: guard check 0 at launch, in game and at the end,
start-up watch ok, 0 LOUD lines in render.log, server.log, vpproxy.log and shell_composite.log, harness stop 0; no crash
(bug 0127 not seen: the four pans passed in all three launches, Overlay on). Instance restored by hash (backup verify equal
/ absent, `set-proxy --original` 0, provision verify 0).

| Radius (run, time EST) | Bake-ahead after the field | Server frames in the script | Gray frames after the bake-ahead: pans / jumps / revisits | Steps with gray: pans / jumps / revisits | Most per step | Gray tile-frames | Bakes / evicted in the script | Resident at the end | Lockstep exact in the script |
|---|---|---|---|---|---|---|---|---|---|
| **-1** (rm1, 16:05-16:12) | 4,922 tiles in **16.5 s** (5,003 bakes, 0 failed, 1,099.5 ms GPU; 352.7 MB caches in 362.6 MB arrays) | 31,584 (224/s) | **0 / 0 / 0** | 0 of 4 / 0 of 16 / 0 of 8 | 0 | 0 | 0 / 0 | 4,922 | **30,852 of 30,854 = 99.994 %** (2 older) |
| **5** (r5, 16:12-16:18) | 273 tiles in 1.0 s | 28,962 (206/s) | **0 / 717 / 354** (1,071) | 0 of 4 / 14 of 16 / 7 of 8 | 72 | 51,652 | 9,388 / 9,365 | 296 (21.8 MB, 32.0 MB arrays) | 26,019 of 29,263 = **88.91 %** (3,244 older, 28 give-ups) |
| **0** (r0, 16:19-16:25) | 75 tiles in 0.4 s | 30,060 (216/s) | **54 / 824 / 382** (+31 at the start; 1,291) | 4 of 4 / 16 of 16 / 8 of 8 | 72 | 62,332 | 3,934 / 3,923 | 86 (7.2 MB, 10.7 MB arrays) | 27,875 of 29,707 = **93.83 %** (1,832 older, 41 give-ups) |

- **Radius -1 (the default): 0 gray frames after the bake-ahead over the pans, all 16 jumps and all 8 revisits**, and 0
  failed bakes (3408's 4 Gibraltar tiles bake). The whole launch had 19 gray frames, all before the bake-ahead finished
  (42,947 frames to the stop). So acceptance C holds on the product path with the server's exact baker, as 3408 found
  with shell_server.exe's stand-in. Stock at the same 29 cameras had gray tiles after 28 of them, revisits included (3408,
  terrain-bake-residency.md 6.2).
- **Radius 5 and 0: gray after almost every jump and revisit**, 16-72 server frames per step (median about 55, i.e. about
  0.1-0.35 s at 206-216 frames per second [I: frames / rate]; corrected 2026-11-13 by issue 0139, 4.17: measured on the screen at radius 0, the gray pictures after a jump or revisit span 0.27-0.74 s, median 0.51 s, because the gray frames are not contiguous and the rate drops while the residency bakes), about 50-60 gray tiles per gray frame (of ~104 visible land
  tiles): a jump outside the kept rings starts with most of its view unbaked. A revisit is no better than a new jump: the
  rings were evicted when the camera left (3408's stock pool behaves the same). Radius 5 covers the 10-plot pans (0 gray);
  radius 0 shows a few gray frames in every pan (1-25). The three moves without gray at radius 5 land inside the rings kept
  around the view before (radius N counts rings around the visible tiles, not around the camera's plot): steps 16, 17 and 23
  ((96, 34) -> (93, 37) -> (90, 37); (90, 37) -> (84, 28)). Same shape as 3408's shell_server rows (radius 5: 2,013 / radius
  0: 2,687 gray frames on 24,789 / 40,408 frames, 20-98 per jump), somewhat fewer here [I, not separated: 3408 ran wave 2's
  slower pipeline (8.2 ms CPU per tile) and the stand-alone shell_server pacing].
- **Lockstep pays for the small radius**: 99.994 % exact at -1 against 88.9 % (5) and 93.8 % (0). At 5 and 0 the residency
  bakes continuously (9,388 bakes and 9,365 evictions in 140 s at 5: also while the camera rests, the background rings
  re-bake what the last move evicted), and the visible bakes before a draw (burst 16) and the 4-ms `idle` slices delay the
  draw of a camera past its 3,400-us bound [I, as 3311 read the re-bake misses; the start step at radius 5 had 38 older
  frames with 0 gray]. At -1 nothing is baked after the bake-ahead (0 bakes in the script) and nothing is missed.
- Memory (one point each, not a series): server.exe private before the script 1,961 MB at -1, 1,478 at 5, 1,448 at 0; the
  server's video local 1,481 -> 832 MB at -1 (the driver demotes idle tile textures, 3408 6.4), 1,029 -> 1,109 at 5, 1,011 ->
  504 at 0. The residency's own figure: 362.6 / 32.0 / 10.7 MB of arrays.
- **Verdict for the option: -1 stays the default**; 5 and 0 save ~330-350 MB of arrays and server private on the huge map
  at the cost of 0.1-0.35 s of gray after every jump or revisit (0.27-0.74 s measured at radius 0, 4.17) and 6-11 points of lockstep exact share.

**VT hash by both methods (D94, item 2) [L, 2026-11-13 16:00].** `shell/evidence/3313/vt_hash_3313.py` reads each file
once and prints both: c5h-b and c5h-c `install/Assets/DLC/VT`, 55 files, 47,300,858 B each, **9049's method
`45b3b43359279105fd9d0924e71192d413af38ad22c9d73fd00eacc2c73f0a45`** (the full pack, asset-pipeline.md 1.7; what the Visual
Checks feed records), **3310's method `70402AD78740961507F2C806DDD3CC95817AA6D7F44AD846D76E974C78F68BA4`** (4.10 and 4.11).
The two name the same 55 files: 3310's `vt_hash_3310.py` hashes the lines `<relative path, forward slashes, case kept>
TAB <file SHA-256 in UPPER hex> LF` sorted by path (case-sensitive) with a newline after every line, 9049's the lines
`<relative path, forward slashes, lower case> SPACE <file SHA-256 in lower hex>` sorted, joined by LF without a final
newline. So 70402AD7... is the full pack too; cite 45b3b433... from now on (`shell/evidence/3313/vt_hash.jsonl`).

**Visual Checks feed (COMMON 27, item 3) [L].** 3310's and 3311's pictures on D: recorded with
`shell/evidence/3313/record_feed_3313.py` (`visualcheck.record()`, origin `backfill`, time = the picture's mtime, asset set
and DLL hashes read now): **111 new entries** (3310: 90, the 6 of 9051's backfill skipped by picture hash; 3311: 21), each
with what it validates and the run's pins; the joined depth shots of round 1 carry the stock shot of the same camera
(`same`) and `|joined - stock| x2`, the black-arm and round-2 shots the "only joined / only black" figure; 3311's highlight
and unit shots are **fail** (the EXE draws no highlight or selection hex in D33, 4.11) with s1's stock shot (approximate
camera) and the difference image. Not recorded: 3310's `selftest/` (dry run) and `cmp/` (superseded by `cmp-r1/`). Ids:
`shell/evidence/3313/feed_ids_3310_3311.jsonl`. This run's 87 step shots (29 per radius) are in too (`run_3313.py feed`;
radius -1 shots `pass` with the step's gray count 0, radius 5 / 0 `info` with the step's counts in the notes). **They were
not recorded by the harness**: as merged, `shot._is_game` calls `inst.is_game()` on a property, the TypeError is swallowed
and no harness shot of the game reaches the feed (bug 0135, research/06-tooling-and-workflows/visual-checks.md 1).

**Shipped ini `tap=1` (item 4) [C].** `shell/composite/shell_composite.ini` ships `tap=1` (the product path's terrain needs
the height field; 3308's and 3311's runs set it by hand). The shell tests' fixtures (`run` in `shell/tests/conftest.py`,
`lrun` in `test_lockstep.py`) start from `tap=0` with 3312's version-3 keys; the tap tests set `tap=1` themselves (TapRun,
test_lockstep, test_join_ring). New tests: the shipped ini says `tap=1` once; both fixtures start from `tap=0`. The composite
regression subset of 3312 (test_errors, test_lsprobe, test_standin, test_tagged, test_verification_defects, test_tap,
test_lockstep; shell and frame builds b1, `C5_FRAME_BUILD` set) [L 2026-11-13]: 16:28-16:44 **195 passed, 2 failed, 0
skipped**; 16:45-17:00 **196 passed, 1 failed, 0 skipped**; the failures passed when run again (16:44, 2 passed; the whole
test_verification_defects file 17:00-17:04, 33 passed, 0 skipped). They are not the tap: their composite logs say `tap=0`
(the fixture's). `test_stalled_server_giveup_and_recovery` once saw 73.8 % exact frames against its 80 % floor (a load
floor). `test_new_chain_on_the_same_window_is_adopted` failed twice the same way: the stand-in's recreated swap chain got the
**old chain's address** (heap reuse), so no adoption line is logged (shell-foundations-verification.md, flake note of
2026-11-13). **Third full run 17:04-17:19: 197 passed, 0 failed, 0 skipped, exit 0.** (2026-11-13, issue 0141: the
address-reuse flake was a composite defect, fixed: a chain is now known by address and creation number; the flake note
in shell-foundations-verification.md says how, with the negative test that forces the reuse.)

**README (item 5).** `server/README.md` Renderer section names every `host 1s:` field, 3312's `pace_fallback`,
`pace_fallbacks`, `pace_resumes`, `waits_fallback` with their meaning.

**Not done.** The gray frames' duration in milliseconds (the counter counts frames; the seconds above are frames / rate);
a picture of a gray frame (the step stills are taken after the hold); radius 5 / 0 with a smaller `render_bake_budget_ms`
or without the background rings, to see how much of the lockstep loss each part costs; whether the composite refreshes its
per-chain state when a recreated swap chain reuses the old address (the flake above).

### 4.14 The product path's 2-Hz long frame: the EXE's watch on the MODS tree (issue 0148, 2026-11-13 19:44-) [C, L as marked]

**Symptom** (9055 V1, `renderer-in-server-verification.md` 5.3): on the product path a Present-to-Present interval of
~150 ms every 0.50 s (1.4 per second, ~21 % of the wall time in idle), with render=1 and render=0 and without samplers; not
on the marker route. Every C figure of 4.9 and 4.11 drops frames over 100 ms and so does not show it.

**Cause: every write under `<user>\MODS` costs the EXE's main thread a stall of ~120-170 ms on the huge VP save, and the
product composite appended its per-frame CSV into `MODS\(1) Community Patch` every 0.5 s.**
- *The watch* [C, disassembly of a copy of the DX11 EXE, 2026-11-13; D83: VAs and behaviour only]. The EXE imports
  `FindFirstChangeNotificationW` / `FindNextChangeNotification` / `RegisterWaitForSingleObject`. Two call sites register a
  change notification with a thread-pool wait: 0x431DA4 (path and filter from its arguments; the per-folder UI watches
  below) and 0x62E7E5, called once from 0x5A2F18 (the function that also names `UpdateDatabase`, `UpdateUserData`,
  `ExecuteScript`, `SetDllPath`: the modding system's start-up): `FindFirstChangeNotificationW(path, bWatchSubtree = TRUE,
  filter 0x13 = FILE_NAME | DIR_NAME | LAST_WRITE)`, wait callback 0x660440. The callback sets the byte
  `[g_Civ5App] + 0xEF0` to 1 and re-arms with `FindNextChangeNotification`. What the main thread does when it sees the flag
  was not traced (a re-scan of the installed mods is the likely reading [I]); its cost is the long frame [L below].
- *The watched folder* [L, `shell/evidence/0148/dirhandles.py` on the running EXE, c5h-b, 2026-11-13 19:57 and 20:05]: of the
  EXE's ~300 handles with access 0x00100001 (FILE_LIST_DIRECTORY | SYNCHRONIZE, the change-notification handles) exactly
  **one is on `<user>\MODS`** itself; all others are on single UI folders (`install\Assets\UI\InGame`, `...\Popups`, the mods'
  `Core Files\Overrides`, `LUA`, `New UI`, ...; 39 on one folder at most, one per Lua UI context [I]). None is on the user
  folder, `Logs`, `cache` or `MODS\(1) Community Patch` itself, so a write directly in the CP folder reaches only the
  MODS-tree watch.
- *Who wrote there.* The composite's control thread loops `Sleep(500)` and appends the rows of `lockstep_csv=1` to
  `shell_composite.lockstep.csv` beside the DLL (open, append, close), and every log line is `fflush`ed; on the product path
  the DLL is `MODS\(1) Community Patch\shell_composite.dll` (3305's layout), on the marker route it lived in the work folder
  on D: (3306/3307 `WORK/live`), which is why the marker route did not show it. The server's run folder
  (`MODS\(1) Community Patch\VP64\run\<boot>`: vpproxy.log, server.log, render.log with two lines a second, buffered) is
  inside the watched tree too on both routes; its buffered flushes explain the marker route's 0-1 long frames per 10 s [I].

**The test that shows it** [L, 2026-11-13, c5h-b, D96 pins (x64 DLL 74588135, the pinned layout `x64-04b1211b-pa803776`
with server.exe of this branch), builds of this branch, D33 + Overlay (D93), `VP64Options.ini ServerRenderer = 1`, save copy
C5H-0148-HUGE_0350 of C5H-VP8P-HUGE_0350 (turn 350), idle at the capital, no samplers, no Lua call inside a window, no
quiet-lock hold (shared machine; the effect is 100 times the noise); per-frame records' `present_qpc`,
`shell/evidence/0148/longframes_0148.py`]:

| Launch | Window (what wrote every 0.5 s) | Windows x s | Frames over 100 ms per s | median ms | spacing median s |
|---|---|---|---|---|---|
| `live-base` 19:53, main's code (composite in the CP folder) | none (the composite itself) | 3 x 20 | **1.17** | 135 | 0.505 |
| `live-fix-probe` 20:01, composite writing to `<user>\Logs` | none | 2 x 20 | **0** | - | - |
| | a thread of the driver into `MODS\(1) Community Patch\0148-probe.log` | 2 x 20 | **1.15** | 122 | 0.502 |
| | into `MODS\0148-probe.log` | 2 x 20 | **1.20** | 126 | 0.502 |
| | into `MODS\(1) Community Patch\VP64\run\<boot>\0148-probe.log` | 2 x 20 | **1.25** | 132 | 0.502 |
| | into the user folder `<user>\0148-probe.log` (parent of MODS) | 2 x 20 | **0** | - | - |
| | into `D:\c5-64bit-work\0148\...` | 2 x 20 | **0** | - | - |

Windows rotated per round; the writer appends one line and closes the file each time (as LkFlush). So any write anywhere
in the MODS tree, at any depth, makes the stall, and a write next to it (the user folder, its `Logs`) does not. About every
second write of a 2-Hz writer gives a long frame (1.15-1.25 per s, spacing 0.50 s) [L; why not every write: not
established, the main thread's handling of the flag may merge writes].

**The fix (rule: nothing the patch writes at run time goes under `<user>\MODS`)** [C, commits e69e55c6, 92e6a33c]:
- `proxy_shell.cpp` `OutDirFor` / `OnBoot`: for the product composite under a `MODS` folder the proxy sets
  `VPSHELL_OUT_DIR=<user>\Logs` in the EXE's environment before the load (a value already there is kept); vpproxy.log says
  so. `shell_composite.cpp` `ChooseOutDir`: the log (unless `VPSHELL_LOG`), `shell_composite.lockstep.csv`,
  `shell_composite.lsprobe.csv` and back-buffer shots go to `VPSHELL_OUT_DIR` (created one level if missing), else beside
  the DLL as before; the ini and the trigger files stay beside the DLL (read only). The log's second line names the folder.
- `proxy_rt.cpp` `ReadConfig`: the default run root `<x64 dir>\run` becomes `<user>\Logs\VP64\run` when it lies under a
  `MODS` folder (a `run_root=` of vpproxy.ini is kept), and the log of a boot without a run folder (mode=off,
  server_config=) goes to `<user>\Logs\VP64\vpproxy.log`. So vpproxy.log, server.cfg, server.log, render.log, the snapshots,
  `db\` and the DLL's logs under `<run>\Logs` leave the watched tree.
- Readers of the old run path look in both places (`proxy/tools/live/arm.py`, `tools/validation/proxy_long.py`,
  `shell/evidence/3308` and `9040` drivers); the 3306 / 3307 / 3311 drivers put their explicit server run folders under
  `<user>\Logs\VP64\run`; `docs/OPTIONS.md`, `tools/harness/RECIPE-proxy.md`, `research/03-dll-engine-boundary/proxy-design.md`
  corrected. Test drivers of the product path find the composite's log and CSV in `<user>\Logs` (not in the CP folder).
- Tests [L]: `proxy/tools/test_proxy_rt.py` 74/74 and its shell unit 145/145 on the build (20:15-20:20). Added after the
  MC's review (20:45-20:52): **test_proxy_rt `mods` cases** (a layout `<user>\MODS\(1) Community Patch\`: the run folder
  under `<user>\Logs\VP64\run`, nothing written under MODS, `run_root=` kept as written, mode=off's log in
  `<user>\Logs\VP64`) and **shell_unit `OutDir` cases** (under MODS, any case and the last MODS of the path, no MODS, a
  longer name `MODSX`, a path ending in `MODS`, a result that does not fit). This branch: **79/79, shell unit 152/152**.
  Main's code (07b9040c, exported by `git archive`, built the same way): **76/79**, the three new checks that test the move
  fail (run folder, nothing under MODS, mode=off log; the boot check and `run_root=` kept pass there by design), and the
  shell unit with the new cases does not build (`no member named 'OutDir'`).

**Acceptance: a 5-minute idle and pan run before and after** [L, 2026-11-13, the set-up of the table above;
`run_0148.py run --mode accept --rounds 5 --window-s 30`: five rounds of 30 s idle at the capital Thebes (101, 25) and 30 s
pan (`UI.LookAt` one plot further every 0.15 s, 10 plots out and back, as 9055's B phases); every Present interval counted,
frames over 100 ms included; **no quiet-lock hold**: the machine ran four other teams' jobs (two games, an x64 DLL build, a
census), which can only add long frames]:

| Run | Build | Frames in 300 s | Frames over 100 ms | per s | median ms | share of wall time | spacing median s |
|---|---|---|---|---|---|---|---|
| `live-acc-before` 20:16-20:26 | main's code (proxy 3E47B71A, composite FD9588BD) | 38,203 | **589** (idle 290, pan 299) | **1.96** | 157 (idle) / 170 (pan) | 34-37 % | 0.503 |
| `live-acc-after` 20:27-20:39 | this branch (proxy 9C8C7A22, composite 519758BB, server 1059501B) | 63,844 | **0** (idle 0, pan 0) | **0** | - | 0 | - |
| marker route, 3307 p2-ring (old pin, under the hold; 9055 [R]) | | | 7 in 237 s | 0.03 | | | |

So the product path now has fewer frames over 100 ms than the marker route had (0 against 0.03 per s): the acceptance of
0148 holds [L]. The before-run's rate (1.96 per s, nearly every write) is higher than the lightly loaded probes' 1.15-1.25:
on the busy machine almost every 0.5-s write gave a stall [I]. Mean Present interval (all frames) 7.87 ms before, 4.70 ms
after. Files under `<user>\MODS` written during the after-run (from the driver's start): only `shell_composite.ini`, which
3311's driver rewrites before the launch; during the before-run 16 (the run folder's vpproxy.log, server.log, render.log,
CvAssert.log, `Logs\*.log`, `db\Civ5DebugDatabase.1.db`, the composite's log and CSV, the ini). The end shots of all four
launches are in the Visual Checks feed (issue 0148). Raw data on D: (`D:/c5-64bit-work/0148/live-*`, not committed).

**Consequences and open points.**
- The 30-AI-turn long frames of 4.7 ("the EXE's own") were measured with the server run folder inside MODS: part of them may
  have been this stall (the server writes turn files there during turns) [I, not separated].
- Any other writer under MODS costs the same: the harness and test drivers that put files there during a game (trigger
  files such as `shell_composite.shot`, an ini rewrite) cause one long frame each; a mod or tool of the player's that writes
  into MODS too.
- 0129 (the front end's mod activation through the proxy takes 14-15 min): the proxy wrote its db snapshots and log into
  the run folder inside MODS during the activation; whether the MODS watch takes part there is not tested [I].

### 4.15 Server growth per turn with re-bakes: the residency's t8 slices (issue 0137, impl-A-0137, 2026-11-13) [C, L as marked]

Answers 4.11 E's open question ("why re-baked tiles take more slices and cache") and 9055's V6
(`renderer-in-server-verification.md` 7.2).

**Cause [C, `render/src/bakeresidency/residency.cpp` at main a83f7e3ca].** `bakeOne` sets `BakeRequest::areaChanged =
gen > 1`, i.e. on every re-bake, as the contract asks (`baker.h`: the EXE's tile+0x300, "area changed since load", set by the
rectangle invalidation 0x4387D0; `terrain-bake-comparison.md` 6 and 7). `PipelineBaker` then makes t8 whatever the layer list
says (`pipeline.cpp`: `needSecond = needsSecondCache(..., req.areaChanged)`). At the first bake (`areaChanged` false) only
tiles whose list has a spec-mapped layer get t8; the residency allocated two slices per bake and gave the t8 slice back when
the bake made none. So **every re-bake of a tile without t8 took a new slice**, for good (tile+0x300 stays set), and every
256 slices a new chunk. Not a layer-list change, as 9055 7.2 inferred: the re-baked tiles' t8 comes from `areaChanged`.

**The arrays are private bytes [L, 3311's p1 raw data, `D:/c5-64bit-work/3300/w12/live-p1/out/`].** Joining render.log's
residency lines with memsampler's 2-s server samples (`shell/evidence/0137/chunksteps.py`): each of the four chunk
allocations during the turns (15:04:09, 15:06:12, 15:09:05, 15:13:43 EST) is a step of **+11.0 to +11.1 MB** of server
private in the same 2-s sample (a 256-slice chunk at size class 256 is 10.67 MB). 9055 found the same by regression
(about 1 MB of private per MB of arrays). Slices over that run: 8,468 after the bake-ahead (4,922 tiles: 3,546 with t8),
8,704 at turn 350, 9,517 at 15:19 and no change in the last 8 minutes of turns; the ceiling is 2 x 4,922 = 9,844 slices =
39 chunks (415.9 MB). So the growth was bounded (one more chunk at most on this save) but tied to the AI's activity, and
the arrays' size at any moment depended on the game's history.

**Fix [C].** A resident tile now holds two slices from its first bake to its eviction, the t8 one also when its bake made
no t8 (counted as `t8_held` in the stats line, after `slices=`), and a re-bake writes into the tile's own slices
(`inPlace`; a failed bake in place leaves the tile its slices and last bake: both bakers check everything before they
write a slice). Re-bakes allocate nothing in the arrays; the draw copies of a re-baked tile are refreshed through
`drawGen` instead of being given back and taken again. Cost: the arrays reach their ceiling at the bake-ahead, **2 slices
per land tile** (huge save: 9,844 slices, 39 chunks, 415.9 MB, against 362.6 MB before the fix after the load and 405.2 MB
after 3311's 30 turns), i.e. about +53 MB at the load and nothing afterwards. Non-array mode (`arrays = false`, not the
product) is unchanged: a t8 gained on re-bake is a new texture there.

**Test [L, RTX 4080, 2026-11-13 19:44-19:53 EST].** `render/tests/bakeresidency/residency_test.cpp`
`rebake_same_tiles_10000_times_memory_flat`: the 16 x 12-tile test field (168 land tiles), size class 512 (174,752 B per
slice, 44.7 MB per chunk, so that the old code's growth is far above the driver's own swings), chunk 256, a baker with
`PipelineBaker`'s t8 rule (`AreaT8Baker`: t8 only with `areaChanged`, over the stand-in); bake-ahead, then 60 rounds of
raising every land tile, idle until baked, one `beforeDraw` alternating between two views (10,080 re-bakes). Stated bound:
slices and arrays exactly as after the bake-ahead; private bytes (`GetProcessMemoryInfo` PrivateUsage, after a GPU-idle
wait) at the end at most 24 MB plus 192 KB per draw-copy texture the pool added above the reading after the bake-ahead;
and at most 12 MB from the end of round 3 to the end. *(Corrected 2026-11-14, issue 0173: these two private-bytes bounds
failed on a loaded workstation because the readings held the driver's own memory; the test now reads settled private bytes
and its bounds are 8 MB and 6 MB, see 4.15.1. The slice and array checks are unchanged. The results below were taken with
the old readings.)* Results:

| Code | Slices | Arrays | Draw textures | Private, whole run (bound) | From round 3 |
|---|---|---|---|---|---|
| before 0137 (a83f7e3ca's `residency.cpp`) | 168 -> 336 | 42.7 -> 85.3 MB | 44 -> 84 | **+69.5 MB** (+31.5): **fails** (5 checks) | +0.7 |
| with 0137, 5 runs | 336 -> 336 | 85.3 -> 85.3 MB | 44 -> 120 | +21.7, +23.0, +25.3, +27.3, +28.5 MB (+38.2): pass | +3.1, +5.5 |

The ~10 MB above the draw textures in the passing runs comes in rounds 1-2 and stays (a per-round trace with
`C5R_0137_TRACE=1`: rounds 3-60 within a few MB, steps of +-2-8 MB at random rounds, in both directions): the driver's own
memory for the first t8 uploads of the stand-in, not the residency [I]. The same test at size class 256 could not tell the
two apart (old code +3.6 MB with chunk 16; the driver's swings are up to ~8 MB). The draw-copy pool grows once by a second
texture for each tile within one ring of the view that gains t8, up to 2 per tile within one ring of any view seen; it does
not grow with re-bakes after that.

**Live arms (3311's method, D96 pins) [L, 2026-11-13 19:56-20:43 EST, functional, no hold].** Workstation, instance c5h-c
(Goldberg, `%USERPROFILE%/c5-instances`, VT present), DX11 EXE 04CFDC71..., save copy `C5H-0137-HUGE_0350` of the instance's
`C5H-VP8P-HUGE_0350` (C499D3C6..., turn 350; removed at the restore). Builds b1 of `task-0137-residency-growth` at 32303d00
(main 01c17609 merged, the fix 3ba9526d), 19:54-19:56, by 3311's commands (`server/build_server.py --src
D:/c5-64bit-work/9045/src-x64f --deps D:/c5-64bit-work/9045/bF/deps --config release --offline`, `shell/build.cmd`):
server.exe `49F4CB13...`, proxy `75FB2A4E...`, composite `25E63226...`; the x64 DLL and libraries of the layout
`x64-04b1211b-pa803776` (DLL `74588135...`). Driver: `shell/evidence/3311/run_3311.py` through a wrapper that only renames
the holder, the save copy and the control's run folder (`shell/evidence/0137/run_0137.py`, `live.sh`), `--max-turns 30
--reloads 0 --no-d93`; the product arm (render on: `ServerRenderer = 1`, the server's own bake, the feed filter) first,
then the control (the same files plus `server_config=` naming the product run's server.cfg with `render=0`; the proxy
logged "render=1 and render_section=... are NOT added"; no render.log). Both: 30 AI turns 350 -> 379, every step exit 0,
guard 0 / 0 / 0, start-up watch ok, 0 LOUD lines in every log, harness stop 0, restored by hash (backup verify, provision
verify 0). Other agents' games (0148 on c5h-b, 0129 on c5h-e) ran on the machine meanwhile (control 40.6 s per turn,
product 30.1 s). perf/memsampler, server private bytes at each autosave:

| | server private after load (turn 350) | server MB/turn, all 30 (95 % CI) | second half (365-379) | EXE MB/turn |
|---|---|---|---|---|
| **product, render on, with 0137** | 2,058.4 MB | **+1.39 [1.04, 1.73]** | +0.24 [-0.24, 0.72] | +1.56 [1.08, 2.05] |
| **control, render=0, same builds** | 246.1 MB | **+1.67 [1.33, 2.00]** | +1.32 [0.04, 2.59] | +2.18 [1.78, 2.58] |
| 3311 p1 / c1 (before 0137, same pins and save) | 1,996.4 / 219.9 MB | +3.11 [2.74, 3.48] / +1.20 [0.97, 1.44] | +1.65 / +0.58 | +1.42 / +1.53 |
| 9055 product / control (before 0137) | 2,000.7 / 246.9 MB | +3.15 [2.77, 3.52] / +1.33 [1.09, 1.58] | +1.65 / +0.59 | +1.90 / +1.33 |

(`summarize_3311.py`'s own fit over its per-turn series gives product +1.38 [1.19, 1.57], control +1.49 [1.30, 1.68]: the
same reading.) **The render-on growth per turn is inside the render=0 control's 95 % interval** (and inside 3311's and
9055's control intervals): the excess of +1.91 MB per turn is gone; in this pair the product grew 0.28 MB per turn *less*
than its control (one run per arm; the intervals overlap). Residency over the turns (render.log): arrays **415.9 MB, 39
chunks, 9,844 / 9,984 slices from the bake-ahead (17.6 s after the field, 4,930 bakes, 0 failed) to the end**, through 11,433
re-bakes (11,162 in the 30 turns, median 331.5 per turn, max 809: the same game as 3311's); `t8_held` 1,135 after the first
588 re-bakes, 327 at the end (= 9,844 - 9,517, 3311's last slice count: the same tiles gained t8); caches 352.7 -> 395.9 MB
inside the held slices; draw-copy textures 202 -> 214; gray frames 20, all before the bake-ahead finished, 0 after. The
price is at the load: server private 2,058.4 MB at turn 350 against 3311's 1,996.4 (+62 MB; the arrays +53.3 MB of it).
Pictures: the two end-of-run shots (product: the joined picture behind a wonder popup at turn 380; control: no server
picture) are in the Visual Checks feed as `info` (`vc-20261113-201535-workstation-608e`, `vc-20261113-204125-workstation-0d9d`).
Small results and the driver in `shell/evidence/0137/` (`summary_{product,control}.json`,
`memsampler_{product,control}.txt`, `feed-per-turn_product.tsv`, `run_0137.py`, `live.sh`, and the two scans of 3311's raw
data, `chunksteps.py` and `scan3311.py`); raw logs and per-turn CSVs only in `D:/c5-64bit-work/0137/live-{product,control}/out/`.
The driver and the scans ran from `D:/c5-64bit-work/0137/` (the committed copies differ only in their paths).

**End state on the huge save, old against new [L, memsampler's first and last autosave rows].** The renderer's share is the
product arm's server private minus its own control's at the same turn (the three pairs ran at different moments of a
shared machine; the controls themselves differ by up to 9 MB):

| Pair | product turn 350 -> 379 | control turn 350 -> 379 | **renderer's share, turn 350 -> 379** | arrays at 379 |
|---|---|---|---|---|
| 3311 (before 0137) | 1,996.4 -> 2,089.7 | 248.2 -> 286.4 | **1,748.2 -> 1,803.3 (+55.1)** | 405.2 MB, 38 chunks, 9,517 slices used |
| 9055 (before 0137) | 2,000.7 -> 2,100.3 | 246.9 -> 288.3 | **1,753.8 -> 1,812.0 (+58.2)** | 405.2 MB |
| 0137 (with the fix) | 2,058.4 -> 2,118.0 | 246.1 -> 295.5 | **1,812.3 -> 1,822.5 (+10.2)** | 415.9 MB, 39 chunks, 9,844 held |

So the fix's cost at the load (+58 to +64 MB of share) is what the old code had reached by turn 379 (1,803-1,812 MB), plus
one chunk: at turn 379 the fixed server is 10-19 MB above the old one, the 39th chunk that holds the t8 slices of the 327
tiles still without t8. The old code would have allocated that chunk once 212 more tiles gained t8 (9,728 slices in 38
chunks); whether a game ever re-bakes them depends on where the AI acts (tiles far from any change are never re-baked).
From turn 379 on the fixed server's arrays cannot grow; the old code's could by at most that one chunk.

**Remaining.** None measurable on this pair. The ~13 MB (0.45 MB per turn) that 9055 7.2 left unexplained beside the array
steps is not seen here: the product grew less than its control. Not checked: a game longer than 30 turns (with the arrays
fixed, the residency itself cannot grow: its other per-turn state is the draw-copy pool, bounded by 2 per tile within one
ring of any view seen); radius 5 / 0, where tiles are evicted and re-baked from scratch (each first bake now takes both
slices at once, as before 0137, and gives both back on eviction).

#### 4.15.1 The re-bake memory test under load: the driver's memory, not a leak (issue 0173, impl-B-0173, 2026-11-14) [L as marked]

**Symptom.** `rebake_same_tiles_10000_times_memory_flat` failed in a render suite run on a loaded workstation (lead-3600,
2026-11-14 02:52 EST): private bytes +17.3 MB from round 3 to the end against the bound of +12.0; it passed alone at 02:55.

**Cause: the D3D11 driver's own memory under GPU contention [L, RTX 4080, 2026-11-14 12:46-12:57 EST].** The test
submitted its 10,080 re-bakes (each a 174,752-B BC1 upload per slice) and draw-copy refreshes without waiting for the GPU,
and read PrivateUsage after an event-query wait plus 200 ms. Private bytes include the NVIDIA user-mode driver's memory:
the upload and command memory of work in flight, and the pools it grows to hold it, which it keeps after the GPU has caught
up. Build: this branch's `c5r_bakeresidency_test` (clang-cl Release, `render/build.py --build-dir
C:/c5-64bit-work/0173/build`). "Load": three other copies of the same test executable looping beside the measured one
(GPU and CPU contention like a parallel suite or another agent's game); the driver script and the analysis are in
`render/evidence/0173/`.

| Arm (old test, traced, `C5R_0137_TRACE=1`) | Runs | Failed | Whole run | From round 3 | Rounds 30 -> 60 |
|---|---|---|---|---|---|
| alone | 9 | 0 | +23.0 to +28.4 MB | +1.2 to +11.1 MB | -0.4 to +4.5 MB |
| beside 3 load copies | 14 | 12 | +22.2 to **+244.7 MB** | -7.0 to **+226.5 MB** | up to +62.3 MB, still rising |

Every count of the residency stayed exact in every run (slices 336, arrays 85.3 MB, draw textures 44 -> 120, failures 0):
the only failing checks were the two private-bytes ones. Proof that it is the driver's memory and not the process's own:
with `ClearState` + `IDXGIDevice3::Trim` after the end reading, **every one of 24 runs settled at 188.2-200.5 MB** whatever
it had grown to (497 -> 197 MB in the worst run; base 190.7-200.5, alone 188.3-190.0), and a per-round GPU wait alone
(`waitGpu` after each round) removed the large growth (6 of 6 passed beside the load, +17.7 to +28.8 MB). The +17.3 MB
of the original failure is this effect at a lighter load: neither a leak in the residency nor random noise. Alone the
old readings also carried the driver's swings (round 4 steps of +5.9 MB after the round-3 idle; from round 3 up to
+11.1 MB, i.e. the old 12-MB bound was nearly met by noise alone).

**A Trim's memory comes back one pass late [L].** One `ClearState` + `Flush` + `Trim` right after an event-query wait frees
0-2 MB; the next such pass, 100 ms later, frees the 35-70 MB (traces: 238.0 -> 236.8 -> 191.6 -> 191.6 MB). A single
Trim at the reading (the first try of the new test) left the round-3 and end readings 50-60 MB above the bake-ahead one, which
had been trimmed at a moment the driver had already finished: all 8 runs failed. Readings therefore repeat the pass until
two in a row free less than 0.25 MB each (at most 12 passes; in practice 3-4).

**The test now [C, `render/tests/bakeresidency/residency_test.cpp`].** (1) Every round waits for the GPU to finish it (event
query, as a frame would be finished before the next), which bounds the work in flight. (2) The three readings (after the
bake-ahead, after round 3, at the end) are *settled*: GPU idle, 100 ms, `ClearState`, `Flush`, `Trim`, repeated as above,
lowest value. A settled reading holds the process's own memory (heap, arrays, caches, draw copies), which a leak in the
residency would grow and a Trim cannot free. (3) Bounds from the measured spread: whole run at most 8 MB above the draw copies
the pool added (192 KB each), and from round 3 to the end at most 6 MB (0.64 KB per re-bake over 9,576 re-bakes).
(4) A negative test, `rebake_memory_check_catches_a_leak`, runs the same rounds with a deliberate leak of 2,048 B of heap per
re-bake (18.7 MB from round 3) and requires the from-round-3 growth to exceed the bound. `C5R_0173_LEAK_BYTES=N` injects such
a leak into the positive test to try the bound's reach. The run takes 11-17 s for both tests (was about 9 s for one).

Spread of the settled readings, 22 runs (10 alone, 12 beside 3 load copies), 2026-11-14 13:05-13:11 EST:

| | Alone (10) | Beside 3 load copies (12) | Bound |
|---|---|---|---|
| whole run, above the draw copies (14.2 MB) | -1.1 to +2.4 MB | -0.3 to +2.9 MB | +8.0 |
| from round 3 to the end | -1.3 to +1.6 MB | -0.6 to +2.4 MB | +6.0 |
| negative test, from round 3 (leak 2 KB per re-bake) | +18.5 to +22.3 MB | +18.2 to +22.2 MB | must exceed +6.0 |
| highest unsettled reading (driver's share, information only) | +62.2 to +67.5 MB | +61.4 to +84.3 MB | none |

**Acceptance [L, 2026-11-14 13:11-13:19 EST, `render/evidence/0173/acceptance.txt`].** 20 consecutive runs of the whole
module (both tests) beside 3 load copies: **20 passed, 0 failed**; whole run -1.1 to +2.7 MB above the draw copies, from
round 3 -1.7 to +1.8 MB, the negative test +17.6 to +22.5 MB (above the bound every time, so it passed). The bound's reach,
`C5R_0173_LEAK_BYTES` in the positive test beside the same load: **1,024 B per re-bake failed 3 of 3** (from round 3 +10.1
to +11.3 MB, whole run +11.0 to +11.4 above the draw copies: both memory checks); **512 B per re-bake failed 2 of 3** (+4.4
to +6.5 MB: at the bound's reach of about 0.64 KB, as designed). `render/build.py --test -R bakeresidency`: passed, 0 skipped
(13.5 s).

**Not covered.** Other GPUs and drivers (Trim's timing and the size of the driver's pools are the driver's; the suite runs
on the RTX 4080); a leak smaller than about 0.64 KB per re-bake (the bound's reach); a leak of driver objects that Trim frees.
The ASan variant of the suite was not run for this change (a test-only change; the test's ~20,000 re-bakes under ASan were
not timed).

### 4.16 Bug 0134: the server's surface stood above the EXE's; the cause and the fix (issue 0134, 2026-11-13 20:24-) [C, L as marked]

**Cause [C from the knowledge base; L for its effect below].** The EXE does not draw its terrain at the height where it
places models. Its terrain VS reads the tile's 1024 x 1024 **A8_UNORM height page**, whose texel is the **high byte** of the
field's u16 sample (terrain-bake-passes.md 5.0: rule decoded, every captured page equal to it, all 11 mips [C + L]), as
`z = alpha * HexScale.z * 255` with HexScale.z = 0.2 (terrain-bake-passes.md 5.3 [L]; terrain-surface.md P11 for the VS), so
the drawn height of a sample is **`z_exe = (u16 >> 8) * 0.2`**. Models, units and cities are placed with HeightAtGrid
0x6D8B60, `u16 / 255 * 0.2`. c5r::terrain (epic 3200) drew HeightAtGrid's height (alpha = the raw u16, HexScale.z = sz *
65535/65025: terrain-surface.md 2), i.e. **0.2 * (u16 / 255 - (u16 >> 8)) higher than the EXE, at every sample, never lower**:
0.2 * (b + r) / 255 for u16 = 256 b + r, so 0.05-0.25 world units at the usual land heights (z 9-23).
- Per plot on the huge save's stock dump (`shell/evidence/0134/height_offset_0134.py` on epic 3000's
  `i2-ground-truth/huge`, samples within 24 of the plot centre; result `height_offset.json`): **Thebes (101, 25) 0.190
  mean (0.047-0.246), Memphis (100, 30) 0.194, Akhetaten (94, 16) 0.137**; over 508 land plots within 16 of (100, 25): flat
  plots (137, mean z 12.01) **0.193**, hilly plots (371) 0.153. Flat land on this map lies at u16 ~15,325 = 59 * 256 + 221:
  just below a byte step, so the server stood ~0.22 above the EXE on almost every flat plot. That fits 9055's picture
  (renderer-in-server-verification.md 6): strips along the near-camera lower edges of buildings and the city wall,
  ~60 % of the server-hidden building pixels visible in stock at the flat capital, 4-11 % at the hill city Akhetaten [I for
  why models are cut over several pixels by 0.2 world units: city models carry near-horizontal ground pieces at their base,
  which a surface 0.2 higher covers over their whole depth].
- Not the cause [C, L]: a city flattening in the CPU field (the city-field manager's TF_Small/Large_City stamps,
  `research/sources/shell-gates/stubs/landmark.md` 4): the stock dump of 3004 and the D33 tap of 3203 agree within 1 u16,
  and the captured height pages equal the rule from the tap byte for byte (terrain-bake-passes.md 5.0), so whatever the
  stamps do, they do not change the heights the EXE draws from. The D24 precision (4.10) and the camera (4.10, 0.2 px) are
  not it either.

**Fix [C].** `c5r::terrain` now writes alpha = `(u16 >> 8) * 257` (as UNORM16 exactly byte / 255, the value of the EXE's
A8 texel) and HexScale.z = sz, so the VS gives byte * sz, the EXE's drawn height; `c5r::terrain::drawnHeightAtSample` is the
rule (render/include/c5r/terrain/terrain.h). The residency's per-tile z bounds (culling) use the same rule
(render/src/bakeresidency/residency.cpp). Everything that reproduces the EXE's **CPU** use of the field keeps u16 / 255:
the bake's vertex jobs, slope classes and grid normals (terrain-bake-blend.md, terrain-bake-passes.md 5.1; bit-exact
against the EXE), the residency's size classes (0x7212B0's thresholds), HeightAtGrid for placement and picking. The
height pages of the bake (3406's `buildHeightPage`) already were the high byte. The stand-in sources of
`render/src/bake/pipeline.cpp` (`fittedHeightPage`, `gridVertexHeights`, labelled stand-ins, not used by the server) are
left as they were. Still different from the EXE: the EXE's LOD (terrain-surface.md P17): the module draws L = 0 (mip 0,
LodStep 1); *measured 2026-11-14 by issue 0154 (end of this section): at the default zoom on the workstation the EXE draws
L = 1 (LodMip 1, LodStep 2), on average 0.05-0.12 world units lower, at most ~0.28 (p95 of |dz| on land in view). The
sentence replaced here said "its stock value is not read yet".*

**Tests [C, run 2026-11-13 20:35, RTX 4080].** `c5r_terrain_test` 18 run, 0 failed, 0 skipped: the ramp and wrap tests now
check the surface of the drawn heights (the ramp's low byte is chosen so that a draw from the raw u16 fails by 0.13-0.33);
`drawn_height_is_the_high_byte` (the rule, wrap, outside); `draw_city_plot_at_the_exe_drawn_height` (a flat field at u16
15,325, Thebes' height: every pixel z 11.80000, HeightAtGrid 12.01961); `game_surface_under_a_city_plot_huge` (the huge
ground truth at its v2 camera: 9,481 sampled pixels within 24 samples of Memphis' plot centre, 0 off the drawn surface by
more than 0.01, on average **0.1886** below HeightAtGrid's surface, min 0.0478). The Tiny v1 terrain share drops from
2,152,064 to 2,142,865 px (the surface is lower against the same water plane; `shell/tests/test_terrain_scene.py` allows
0.015 of share, this is 0.0026). Whole render suite (`render/build.py --test`, 2026-11-13 21:11-21:13, bake, bakeblend,
bakegrid, bakeheight, bakelayers, bakepost, bakeresidency, terrain and the rest): **17 of 17 passed, 0 skipped**.

**Live set-up [L, 2026-11-13].** Workstation, Goldberg instance **c5h-d** (`D:/c5-64bit-work/instances`), DX11 EXE
04CFDC71..., CP/VP/EUI, 2554 x 1411, RTX 4080. **Asset set (D94): VT present, 55 files, 47,300,858 B, folder hash
70402AD7...** by 3310's method (= 3310's and 3311's; the feed records it as `45b3b433...` by 9049's). Save copy
`C5H-0134-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (removed after each run). Pins of D96: x64 DLL 74588135...
(`x64-04b1211b-pa803776`); builds of branch `task-0134-city-surface` at eb364bbd (main merged): **b1** server.exe
6D9E8722..., proxy E4EFA720... (the terrain fix); **b2** server.exe 40437F66..., proxy E91C8BBB... (b1 plus the residency
bounds); composite 1A1D2FA9... for both. Driver `shell/evidence/0134/run_0134.py` (3311's prep / launch / guard / stop /
restore, 9055's three cameras: the load view, `UI.LookAt` the capital Thebes (101, 25), `UI.LookAt` Akhetaten (94, 16));
every launch: guard ok, stop 0, restore by hash 0; b2: 0 LOUD, DISABLED or EXCEPTION lines in server.log, render.log,
vpproxy.log and shell_composite.log. Arms: product b1 20:40-20:47 (D shots), stock 20:53-20:59 (the same proxy route,
`ServerRenderer = 0`, the instance's own config), stock without Decal and Forest 21:02-21:08 (to test colour
contamination), product b2 21:14-21:22 (D shots, then two rounds of 9055's lockstep phases). Shots and difference images
in `D:/c5-64bit-work/0134/live-*/out/dshots` and `dana-*`; every shot in the Visual Checks feed under 0134. Before = 9055's
product shots (renderer-in-server-verification.md 6: c5h-b, the same pins, save and asset set; the load view's camera is
identical across launches, the LookAt cameras within ~0.2 px) against this stock arm.

**The stock game's CPU field in play [L].** Read by PID at 20:57 in the stock arm (`tap_live.py rawdump`, `hfbake decode`):
equal to epic 3000's stock dump in all but 29 samples (each by 1 u16), 0 differences under Thebes, Memphis, Akhetaten. The
field carries no city flattening that the tap could have missed.

**Depth against stock, 9055's metric (`d_analyze_0134.py`; "visible in stock" = 1 - share(stock ~ joined on hidden
building pixels) / share on ground; global baseline, thresholds 24 / 40; `d_ring_0134.py` also with a baseline in a ring
of 2-12 px around the models).**

| Camera | static px hidden by the server, before -> b1 -> b2 | visible in stock, before | b1 | b2 | b1 against stock without Decal/Forest |
|---|---|---|---|---|---|
| load view | 12.4 % -> 11.25 % -> 11.26 % | 8.7 / 12.9 % | 1.5 / 7.9 % | **1.3 / 9.0 %** | -1.5 / 5.8 % |
| Thebes (capital) | 10.7 % -> 9.49 % -> 9.38 % | 65.5 / 59.2 % | 57.8 / 53.2 % | **57.1 / 52.3 %** | 62.5 / 57.2 % |
| Akhetaten | (9055's dw0 unusable) -> 9.19 % -> 8.88 % | - | 8.3 / -0.2 % | **8.7 / 0.3 %** | 5.9 / 0.9 % |

With the ring baseline: load 5.5 / 13.4 %, Thebes 51.8 / 48.1 %, Akhetaten 18.7 / 7.7 % (b2). By this metric the load view and
Akhetaten meet the MC's ceiling of 10 % (global baseline) and Thebes does not.

**Why Thebes stays high on that metric: the colours, not the depth [L].** Looked at (crops `D:/c5-64bit-work/0134/dana-b1*/cap_q3.png`,
`dana-pre/cap_q2.png`): the hidden "rims" at Thebes are the lower edges of mine and quarry rock piles and of city
buildings, 4-8 px; in stock the ground around them is a grey-blue quarry / city ground texture that the server's bake does
not have (it stays with **Decal and Forest off** too, so it is part of the EXE's terrain bake, not a decal [I]), so a stock pixel
there is never "terrain-like" against the server's yellow ground and every hidden pixel counts as visible. A geometric
test that compares no colours across pictures (`rim_edge_0134.py`): for each column's hidden rim (r0 = the server's cut,
r1 = the model's own bottom in passthrough; rims of >= 3 px), the row of the strongest vertical colour step of a picture in
r0 - 2 .. r1 + 2 is that picture's edge; its place on the rim, calibrated by the controls dw1 (the server's own cut: shows
2.5-3.7 % of the rim) and dw0 (the whole model: 87-92 %), gives the share of rim pixels the picture shows:

| Camera | rims (px) | stock shows of the rim, before | b1 | b2 | b1, stock without Decal/Forest |
|---|---|---|---|---|---|
| load view | 1,388-1,471 (10,421-11,186) | 24.7 % | 19.3 % | 21.3 % | 18.5 % |
| Thebes | 5,046-5,224 (33,564-37,691) | 25.9 % | 21.7 % | 21.1 % | 20.7 % |
| Akhetaten | 1,362-1,406 (10,806-11,355) | - | 19.3 % | 23.0 % | 18.1 % |

(stockb, the second stock shot, gives the same within 0.3 points, except Akhetaten without Decal/Forest at 51 %: one shot
with something else on screen, not looked into.) So **at the geometry level Thebes is like the other two cameras**: the
server's cut lies where stock's cut lies on ~78 % of rims at every camera, and the fix moved stock's edge on the rim from
t = +0.10 / +0.125 (median, before) to -0.05 / -0.04 (after), i.e. the server's cut came down to stock's. 9055's "~60 % at
the capital" (and 3310's 30-48 %) was mostly this colour effect; the depth part was the global 8-bit offset now removed.
What the remaining ~20 % is: *separated 2026-11-14 by issue 0154 (below): mostly the method's floor; the real geometric
difference (the EXE's LOD 1 against the server's 0) accounts for about 3-5 % of the rim pixels by a wall model and does not
tell the rims stock "shows" from the rest. Replaced text: "[I, not separated]: the method's floor ... or a further real
difference (the EXE's LodMip, P17; its TessFactor at LOD < 0)".* The rim method was not validated on a picture with a
known cut.

**No whole-map shift, lockstep [L, b2].** Ground texture block matching of the joined picture against stock (`shift_map_0134.py`,
32-px blocks with < 20 % model, NCC >= 0.6): 90-99 % of 51-155 matched blocks per camera at dy 0 or 1 px, dx median 0 (the
same before the residency change, b1). Lockstep in the idle and pan phases (2 rounds x city idle, city pan, sparse idle,
sparse pan, 20 s each; `lk_analyze_0134.py`): **38,504 of 38,792 frames exact (99.26 %)**, 288 older, 0 none; city idle
98.4 %, city pan 99.2 %, sparse idle 99.5 %, sparse pan 99.6 %; turnaround of exact frames publish -> ready p50 ~1.0 ms,
p99 ~2.1 ms (not a timing result: functional run, no quiet-lock hold).

**Not done / open.** *The first two items were done on 2026-11-14 by issue 0154 (next block).* ~~A depth reference that
needs no colour match at the capital to tell the method floor from a real residual; the EXE's LodMip and TessFactor in
stock frames (TR+0x2424 / +0x2428 during a stock draw);~~ the server's bake lacks the quarry and city ground textures that
the EXE's bake has around improvements (colour, issue for epic 3400's bake).

**The EXE's LOD in stock frames and a colour-free reference for the rims (issue 0154, 2026-11-14 16:47-17:20 EST) [C, L as
marked].** Evidence and tools: `render/evidence/0154/` (`terrain_draws_0154.py`, `tr_read_0154.py`, `lod_surface_0154.py`,
results in `results/`); no picture taken, no game file committed (D83).

*1. What the EXE draws, from stock captures [L].* The terrain draws of five existing RenderDoc captures of the 32-bit EXE
(workstation, RTX 4080, 2554 x 1411, the owner's graphics settings; Tiny saves; 3601's `c5h-c_20261113-235332_frame265572` and
`_frame309938` (stock, `C5H-3600-TINY_0008`), its black arm `c5h-c_20261114-010946_frame14671` (D33 minus River and Arrows,
EXE terrain drawn), 0167's stock-arm `c5h-a_20261114-131137_frame1762` and `_frame2066`; all on `D:/c5-64bit-work/3600/captures`
and `0167/captures`), read from RenderDoc's zip.xml conversion without a replay: a terrain draw is one with a 16-control-
point patch list and a domain shader; TerrainTile (VS/DS cb0) and TerrainFrame (DS cb1) are rebuilt from the frame's Map /
Unmap data. Each frame has two terrain passes: the main view (back buffer, the 2554 x 1411 D24S8 depth target, LESS_EQUAL,
depth write on) and a second pass into a 2554 x 1411 RGBA8 offscreen target without depth at twice the LodStep (not depth-
relevant). Main view:

| Capture | camera height z | LodMip | LodStep | TessFactor | draws |
|---|---|---|---|---|---|
| 3601 camera 2 (3 wheel ticks in) | 261.0 | **0** | **1** | 2 | 58 |
| 3601 black arm; 0167 stock 1762, 2066 (default zoom) | 424.264 | **1** | **2** | 2 | 80; 43; 42 |
| 3601 camera 1 (zoomed out) | 640.3 | **2** | **4** | 2 | 150 |

So the EXE draws at an LOD L per frame with **LodMip = L, LodStep = 2^L, TessFactor = BicubicTerrainTessSubdiv (2)**
(tile+0x1C4 = L on every visible tile, live below); HexScale (1, 0.866, 0.2), BicubicBasis as in terrain-surface.md P11
(weights at t = 0 (1/8, 3/4, 1/8, 0), at t = 0.5 (1/64, 31/64, 31/64, 1/64)); t0 = the 1024 x 1024 A8 page, 11 mips. **9055's
three cameras are at the default zoom: their camera blocks (0134's stock arm) have z 424.264**, the height of the L = 1 captures.

*2. Live on the huge save [L, oldpc].* Testbed job `20261114-165707-5423` (oldpc, instance tb1, its own Steam EXE and DLL,
CP/VP/EUI, save `TB-HUGE_0350` (turn 350), 1914 x 1051, RTX 2080; launch 6.8 s, in game 134 s; stop rc 0, restore and
`provision.py verify` 0), cdb `-pv` (non-invasive) from `tr_read_0154.py`: TR = [0xD165E0 + 0x75FA4].

| Camera (as 9055) | TR+0x242C (LOD) | TR+0x2424 LodMip | TR+0x2428 TessFactor | visible tiles, tile+0x1C4 |
|---|---|---|---|---|
| load view | 1.982 | 1.0 | 2 | 84, all 1 |
| `UI.LookAt` Thebes (101, 25) | 2.016 | 2.0 | 2 | 121, all 2 |
| `UI.LookAt` Akhetaten (94, 16) | 2.023 | 2.0 | 2 | 113, all 2 |

The LOD is the minimum over the visible tiles of 0x6497C0, which divides the tile's sample count by a projected size from
0x744AE0; **0x744AE0 ends by multiplying with the back-buffer width `[0x301056C]`** (live disassembly, cdb `u`, 2026-11-14
[C]), so the LOD falls by log2(2554 / 1914) = 0.42 at the workstation's width: **1.57, 1.60, 1.61 at 9055's three cameras,
L = 1** [I, from the formula and the oldpc reads; consistent with the L = 1 captures at the same camera height and width,
and with L = 0 at z 261 (1.57 - log2(424.3 / 261.0) = 0.87) and L = 2 at z 640 (2.16)]. TR+0x2424 / +0x2428 are written
each frame by 0x4241E0 (P17); the start-up values 1.0 / 2 of terrain-rendering-inventory.md 5.1 are not the stock frame's.
The server (`c5r::terrain`) draws L = 0 everywhere: mip 0, LodStep 1, tess 2.

*3. The two surfaces, without colours [C from the knowledge base's rules, on the huge stock field].* `lod_surface_0154.py`
evaluates both drawn surfaces at their vertices from epic 3000's stock dump (`i2-ground-truth/huge`, equal to the field in
play, above): the server's at every half sample (cubic over 0.2 * (u16 >> 8) at every sample); the EXE's at every sample
(LodStep 2, TessFactor 2) with control heights at even samples from **mip 1** of the page rule (terrain-bake-passes.md 5.0:
truncating 2 x 2 box, then the sharpen; checked against captures byte for byte there), sampled by the VS's addressing at mip-1
coordinate s / 2 - 0.25: 0.75 * M[m] + 0.25 * M[m - 1] per axis. dz = z_exe - z_server at the integer samples on land in view,
and "rows" = dz times the screen rows a point moves per world unit of z at that place (3.25-3.27 at all three cameras: how
far the terrain's cut moves on a vertical model face):

| Camera | land samples in view | dz mean / median / p5 | \|dz\| p95 | cut shift rows mean / p5 | land with EXE lower by > 0.5 / > 1 row | EXE higher by > 0.5 row |
|---|---|---|---|---|---|---|
| load view | 82,954 | -0.121 / -0.125 / -0.283 | 0.283 | -0.39 / -0.92 | 39.7 % / 2.9 % | 0.1 % |
| Thebes | 284,270 | -0.051 / 0.000 / -0.233 | 0.233 | -0.17 / -0.76 | 15.4 % / 0.9 % | 0.0 % |
| Akhetaten | 88,776 | -0.102 / -0.095 / -0.276 | 0.276 | -0.33 / -0.91 | 32.9 % / 2.7 % | 0.1 % |

**The EXE's surface lies at or below the server's almost everywhere** (dz > 0 on almost no sample): mip 1 truncates (the box
drops up to 3/4 of a byte, the sharpen floors), which lowers slopes by about half a byte (0.1 world units) and leaves
uniform areas unchanged (Thebes' flat city plots: median 0). This is a real geometric residual, small: under a pixel row on
97-99 % of the land in view.

*4. The rims against it.* The 0134 rims re-detected from the b2 shots with `rim_edge_0134.py`'s rule (the same counts:
1,471 / 5,046 / 1,406 rims), each with its stock edge t, and per rim the ground under the model's foot (row r1 + 1 cast onto
the server's surface):

| Camera | dz at the foot, mean / p5 | cut shift rows at the foot, mean / p5 | stock "bottom" rims (t > 0.66): n, dz mean | "top" rims (t < 0.34): n, dz mean | share of rim px shown, predicted by the shift / measured (0134 rule, uncalibrated) | corr(predicted t, stock t) |
|---|---|---|---|---|---|---|
| load view | -0.087 / -0.237 | -0.29 / -0.78 | 252, -0.101 | 1,124, -0.084 | **4.0 %** / 21.1 % | 0.06 |
| Thebes | -0.054 / -0.229 | -0.18 / -0.75 | 849, -0.052 | 3,858, -0.055 | **2.7 %** / 22.0 % | 0.04 |
| Akhetaten | -0.113 / -0.261 | -0.37 / -0.86 | 245, -0.127 | 1,059, -0.107 | **4.7 %** / 22.4 % | 0.23 |

The rims stock "shows" (its edge at the model's bottom: 17 % of rims, a bimodal class; stock and stockb agree on 98-99.6 % of
them) stand on the same ground as the rims it hides: their dz differs by 0.003-0.02 world units, and a shift of 0.2-0.4 row
cannot move an edge to the bottom of a 5-7-row rim. A second estimate, from 0134's own before / after: removing a uniform
offset of ~0.19 lowered the measured share by 3.4-4.8 points, i.e. ~18-25 points per world unit; the LOD's mean -0.05 to
-0.11 at the rim feet is then worth ~1-3 points [I]. **So of the ~20 % about 3-5 points are the real LOD difference and the
rest is the rim method's floor on textured stock pictures** [C for the surfaces, I for the attribution: the wall model
under-counts near-horizontal ground pieces of city models, which a small dz uncovers over several rows (above); the per-rim
test shows no such concentration where stock "shows" a rim].

*What this reference is and is not.* It is the stock draw's own inputs ([L] from the captures and the live reads) evaluated
with the EXE's programs as the knowledge base describes them, not a depth buffer read from a stock frame. A captured depth
buffer was not available: (a) the huge turn-350 frame cannot be captured in the 32-bit address space (renderdoc-capture.md
4); (b) the captures hold no rendered target without a replay, and qrenderdoc's Python is held by the owner's
`Python_DebugEnabled` setting (water-system.md 6.2); a scratch USERPROFILE for the child process did not get past it (no
RenderDoc log at all, killed at 90 s; renderdoc-capture.md 8). 3011's `whitemask=2` arm was not run (it needs a D33
configuration and a test DLL on a game instance; the reference above needs neither).

*Consequence for the server (a fix proposed as a new ticket, `tracker/issues/NEW-server-terrain-exe-lod.md`).* For D66 the
server should draw at the EXE's LOD: per frame L from the visible tiles' projected size with the EXE's constants and the
back-buffer width (and its hysteresis), LodMip = L, LodStep = 2^L, TessFactor 2 at L >= 0 (the formula at L < 0), and the
height pages' mips 1-10 by the rule (the bake already builds them, terrain-bake-passes.md 5.0). It moves the drawn surface by
0.05-0.12 world units on average at the default zoom and changes the LOD's popping with zoom to the EXE's; the rims' ~20 %
will not go away with it. *Done 2026-11-14 by issue 0191 (terrain-surface.md 7): the rule decoded in full (projected size
0x744AE0, tile LOD 0x6497C0, the class-3 sphere-visible list, the hysteresis of 0x4241E0), c5r::terrain draws the EXE's pages
with all mips at L per frame, and live on oldpc the server's L and held value equal the EXE's at 9055's three cameras.*

### 4.17 Gray tiles counted per frame in the lockstep record, independent of the residency (issue 0139, 2026-11-13 21:50-) [C, L as marked]

Until 0139 a gray frame (a drawn frame with a land tile without its bake, acceptance C of 3400) was known only from the
residency's own counters (`gray_frames`, `gray_tile_frames` in render.log's `terrain 1s:` line, 4.13), which count the
residency's own visible set before the draw. Now the renderer counts at the draw itself and the count travels with the
frame to the composite's per-frame record [C, branch `task-0139-gray-tile-count`]:

- **Renderer** (`c5r::terrain`, render/src/terrain/terrain.cpp): `FrameStats::landTilesWithoutCache`, with
  `ColorSource::Caches` the tiles drawn without a cache that are LAND by the EXE's class rule (the highest raw u16 of the
  tile and 4 samples around it, / 255 >= 2: the residency's rule, computed at `setHeightField` from the field itself), each
  tile once per draw also when drawn in two wrap copies. `tilesWithoutCache` (unchanged) also counts water tiles and only
  the k = 0 copy, so it is not a gray count.
- **Slot record** (shell/common/vp_frame.h `VPFrameSlot`): `tilesUnbaked` (offset 88) and `unbakedCounted` (92), taken from
  `reserved` (layout, size and VPFRAME_VERSION unchanged; a server before 0139 leaves both 0 = "not counted").
  `vpframe::DrawReport::tilesUnbaked` (-1 = not counted), written by `RingSink::end`; server.exe's render host sets it when
  `render_color` is caches, else -1. `SourceFrame::tilesUnbaked` carries it to the composite (-1 for the version-3 source).
- **Composite**: the per-frame CSV gets a last column `unbaked` (the taken slot's count; -1 when nothing was taken, the
  source is version 3 or the server did not count); readers by column name keep working. The `lockstep 1s:` line gets
  `unbaked_n` (frames that took a counted slot), `gray_frames` (of them with a gray tile) and `gray_tiles` (their sum).
  One row is one EXE frame: an older take repeats the server frame's count, so rows count what the screen showed, distinct
  `taken_f` count server frames.
- **Stand-in**: `ring_producer.exe --unbaked N | frame` (N for every frame, or F % 1000).

**Tests [L stand-in, 2026-11-13 22:12-22:16].** render terrain_test `draw_counts_land_tiles_without_a_cache` (land / water
halves by the class rule, a cache on one land and one water tile, not-Caches = 0, a wrapped field drawn in two copies
counts each tile once; 1-plot tiles) and the existing caches test, 2/2 ctest; test_lockstep
`test_ring_record_carries_the_unbaked_count[7|frame|none]` (every taken row carries its own frame's count, rows without a
take -1, the 1s sums equal the frames that took a slot; without `--unbaked` everything -1 / 0),
`test_ring_producer_refuses_a_bad_unbaked[-1|x|100001|""]` (exit 2), the version-3 exact test now also asserts -1 / 0;
server `test_ring_handover_slots_ready_per_published_camera` reads `unbakedCounted` 1 and `tilesUnbaked == tilesDrawn`
(the synthetic field is all land, the tap gives no cache). Under MSVC ASan (shell and frame `asan` builds, render
`--asan`, 22:30-22:34): the 10 test_lockstep cases above 10 passed (job peak 658 MB, cap 6 GB), render terrain 2/2, 0 ASan
reports. The wider files (test_lockstep, test_join_ring, test_verification_defects, frame test_ring, test_render_host)
under heavy machine load 22:34-22:51 had 10 timing / start-up failures; none followed the branch in an A/B against main
a0419891's builds (shell/evidence/0139/acceptance_0139.txt). One finding on the way: the CSV misses the last < 0.5 s of rows
of a run (the host process ends before the control thread's next flush); the 1s lines do not.

**Live comparison [L, 2026-11-13 22:19-22:29 EST].** Workstation, instance c5h-b (Goldberg), DX11 EXE 04CFDC71..., save copy
`C5H-0139-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, C499D3C6..., removed after), Vibrant Textures present (as 4.13). Builds of
the branch: server.exe `A9C75F0A...` (server.log: repo d00f168c5, dll_pin 04b1211b8, release) with the pinned x64 DLL
74588135... (deps of 9045 bF), proxy `ADDB7913...`, composite `3E157E24...`. Product path exactly as 4.13 (3313's driver,
D33 with Overlay on), **`BakedTileCameraRadius = 0`** (gray in pans, jumps and revisits), 3408's camera script (29 steps, 4 s
hold). Driver `shell/evidence/0139/run_0139.py` (prep / radius / run / compare / restore over run_3313.py; maps the CSV's
QPC to wall time with perf_counter anchors, drift 19 us over the run); results `compare_r0.json` / `compare_r0.csv` and
`lockstep_csv_r0.json` beside it; raw logs in `D:/c5-64bit-work/0139/live/out-r0` (D83). Functional run, no quiet-lock hold.
Guard check 0 at launch, in game and at the end, start-up watch ok, stop 0; restore: backup equal, `set-proxy --original`
0, provision verify 0, save copy removed. Bake-ahead 75 tiles in 0.84 s. Per step the window is the residency's (the last
`terrain 1s:` line before the LookAt to the last before the next step); the record's rows in the same window:

| Over the 29 steps | Residency (its counters) | Record: server frames (distinct taken_f) | Record: EXE frames (rows) |
|---|---|---|---|
| frames | 30,995 | 30,711 (99.1 %) | 31,515 |
| gray frames | **1,419** | **1,341** (94.5 %) | 1,869 |
| gray tile-frames | 68,300 | 61,213 (89.6 %) | - |
| pans (4 steps): gray frames / tiles | 58 / 154 (3 of 4 steps) | 95 / 182 (4 of 4 steps) | 118 |
| jumps + revisits (24): gray frames / tiles | 1,326 / 67,458 (24 of 24) | 1,215 / 60,436 (24 of 24) | 1,706 |

Whole launch: residency 1,437 gray frames (1,419 after the bake-ahead) and 69,067 gray tile-frames on 40,086 frames;
record 1,347 gray server frames, 61,473 tiles on 39,741 server frames, at most 105 gray tiles in one frame.

- **The two counts agree step by step** (every step with gray in one has gray in the other except pan 1, 0 against 3 one-tile
  frames; per jump or revisit the record is 0-10 frames below), so acceptance C can now be checked from the record alone.
- **Why the record is lower after jumps: frames the composite never took.** Over the jumps and revisits the server drew
  168 frames the composite did not take; 111 of the residency's gray frames are missing from the record, about 64 tiles
  each, i.e. the heaviest frames right after the move [L counts; that the missing gray frames are these untaken frames is
  inferred from the totals, not matched frame by frame: the residency keeps no per-frame record]. The bakes before a draw
  delay exactly those frames past the composite's bound, so it takes an older picture (lockstep exact 94.5 % over the
  launch, as 4.13's 93.8 % at radius 0).
- **Why the record is higher in pans: tiles drawn that the residency does not hold visible.** In the pans the renderer drew
  95 frames with 1-4 gray tiles where the residency counted 58 [L]. The renderer culls each tile by its own box (heights
  of all 70 x 70 drawn samples of a 1-plot tile, a 1-sample margin); the residency culls by its tiles' core heights (66 x 66 samples), block by block of 8 x 8
  tiles, before the draw [C both]. Which difference puts the edge tiles in one set and not the other is not
  established [I]; it matters only at radius >= 0 (at -1 every land tile is baked after the bake-ahead).
- **What the screen showed (rows)**: 1,869 EXE frames with a gray tile in the script against 1,341 gray server frames,
  because an older take repeats the gray picture. **Gray period after a jump or revisit, measured on the screen** (first to
  last gray row in the step): 0.27-0.74 s, median 0.51 s (22 steps with more than 20 gray frames; 0.04 and 0.19 s for the
  two that land inside kept rings), against 4.13's 0.1-0.35 s inferred from frames / rate (corrected there).
- Negative result: run_3313.py / run_3308.py look for `shell_composite.log` and `.lockstep.csv` beside the DLL in the mod
  folder, but the composite writes them into the user folder's `Logs\` (`output folder ... (VPSHELL_OUT_DIR)`), so their
  per-step lockstep slices came out empty and the CSV was not copied by the run (copied by hand; run_0139.py now points both
  at `Logs\`). The folder moved after 3313's runs of 16:05-16:25 (which had their slices): issue 0148's rule, 4.14.
  **Fixed 2026-11-14 (issue 0170)** [C]: `run_3308.py` `paths()` gives `clog` / `csv` = `<user>\Logs\shell_composite.log` /
  `.lockstep.csv`; the old place is kept as `clog_mod` / `csv_mod` (only a build before 0148 writes there; 3308's restore
  and 3313's run-start move both places' files into the run's folders, the mod-folder ones prefixed `mod-`). `run_3313.py`,
  `run_3311.py` and every driver built on them (0101, 0134, 0137, 0139, 0153, 9055) take these paths; `run_0148.py`
  names the mod folder itself for its pre-0148 arm (no `--logs`). Offline test
  `shell/evidence/0170/test_logpath_0170.py`: replays a saved Logs folder step by step into a fake instance and checks each
  driver's per-step `ls_slice` (all pass after, 3308 / 3313 / 3311 give 0 lines per step before). Live check of the place,
  2026-11-14 03:2x [L]: the composite logs of c5h-a, c5h-b and c5h-c are all in `<user>\Logs` (second line `output folder
  ...\Logs\ (VPSHELL_OUT_DIR)`), none in `MODS\(1) Community Patch\`. Full saved log of c5h-c (2,166 `lockstep 1s:`
  lines, CSV 1,486,946 rows) replayed in 40 steps: every step's slice equals its lines (54-55 each) for all four drivers.
  Drivers on the test lineage (3306, 3310, 9039, 9040, `shell/tools/*`: the composite loaded through
  `crashlogs\shell.helper` from a work `live\` folder outside MODS, where the proxy sets no `VPSHELL_OUT_DIR`) read beside
  the DLL and are right [C for 3306 / 3310 / 9040 paths and `proxy_shell.cpp` `OnBoot`; I for `shell/tools`, which take
  `--live`]; `tools/testbed/jobs/product_path.py` already looks in both places.

**Not done.** The comparison at radius -1 (19-20 gray frames before the bake-ahead only, 4.13) and at 5; a per-frame record
on the server side to match the untaken frames one by one; the cause of the pan-edge difference above.

### 4.18 Bake slices yield to a waiting camera; stale visible tiles re-baked after the draw (issue 0140, impl-B-0140, 2026-11-15) [C, L as marked]

Answers 4.11 B-lite's open point ("a lower `render_bake_budget_ms` or a bake slice that yields to a waiting camera would
be the test") and 4.13's reading that the visible bakes before a draw and the 4-ms `idle` slices delay a camera past its
3,400-us bound. Branch `task-0140-bake-yield`.

**What held the render thread [C, from the code at main 87ea0a5d].** Two places bake on the render thread while a camera
can be waiting:
- `Residency::idle` (after each draw, `srv_render_host.cpp` `Loop`): bakes until its CPU budget (`render_bake_budget_ms`,
  4 ms) is passed, checked only *after* each bake, so a slice lasts 4 ms plus one bake. The host passes budget 0 when a
  camera is already pending at the call, but a camera that arrives during the slice waits for the rest of it. One
  pipeline bake on the huge save is **2.36 ms of CPU on average, max 38.7 ms** [L, 3311 p1's render.log, last line:
  `bake_cpu_mean_ms=2.362 bake_cpu_max_ms=38.721`], so a slice is 4-6.4 ms typically, against a camera period of about
  8 ms during AI turns (3311 p1: 105,713 frames in about 840 s of turns).
- `Residency::beforeDraw` (inside the draw of the camera itself): bakes the visible tiles that need a bake, up to `burst`
  (16) and the budget, before the draw. At radius -1 after the bake-ahead every land tile has a bake, so **every one of
  these is a re-bake of a stale tile** that would be drawn with its old bake anyway (a stale tile is never gray, 4.8): up
  to 16 x 2.4 ms in the path of the frame the composite waits for.

**Change [C].**
- `ResidencyOptions::yield` / `yieldCtx` (`c5r/bake/residency.h`): `idle()` asks it before each bake and once at the
  slice's end; a "camera waits" ends the slice before the next bake, so a waiting camera is held up by at most the bake in
  progress. server.exe sets it to `Scene::cameraPending()` (the camera block's seq against the last one drawn: a
  shared-memory read) unless `render_bake_yield=0`.
- `ResidencyOptions::staleBeforeDraw` (library default true = the EXE's rule); server.exe passes false unless
  `render_bake_stale_before_draw=1`: before the draw only visible tiles **without any bake** (gray) are baked; stale
  visible tiles are drawn with their old bake and re-baked by the next `idle()`, which takes them first (`raiseTile`
  resets the cursor and the order is rings from the visible set; tested). The change shows one or a few frames later
  than with the EXE's rule; no gray frame comes of it.
- Counters in the residency's part of `terrain 1s:` (appended after `bake_gpu_max_ms`): `idle_slices` (idle calls that
  baked), `idle_slice_max_ms`, `yields`, `yield_wait_max_ms` / `_mean_ms` / `_p99_ms` (per yield the span from the last
  check that found no camera to the check that found one: the camera arrived inside it, so it waited at most that long;
  p99 from 0.25-ms bins, the last bin 8 ms and more), `predraw_bakes`, `predraw_rebakes`, `predraw_max_ms` (the longest
  pre-draw bake time of one frame), `stale_deferred` (tile-frames drawn stale instead of re-baked before the draw),
  `stale_before_draw`, `yield` (0/1).
- server.cfg keys (parsed into globals of `srv_render_host.cpp`, as 3607's water keys, because `srv_render_host.h` is not
  this issue's file): `render_bake_yield` 0|1 (default 1), `render_bake_stale_before_draw` 0|1 (default 0),
  `render_test_bake_cpu_ms` 0..1000 (tests: the stand-in baker's busy wait per bake, `StandinFaults::cpuMs`). Each
  refuses another value with its key (exit 2).

**Tests [L, 2026-11-15, workstation, functional].** render `c5r_bakeresidency_test` (`render/tests/bakeresidency`),
5 new: `idle_yields_before_the_next_bake_once_a_camera_waits` (3 bakes, the 4th check yields; a camera still waiting at
the next call bakes nothing; the bake-ahead completes once it is drawn), `idle_end_check_counts_a_camera_that_came_during_the_last_bake`,
`idle_without_a_yield_check_is_unchanged` (64 bakes = the burst's tokens), `stale_visible_tile_waits_for_idle_when_stale_before_draw_is_off`
(gray tiles still baked before the draw, 0 gray frames, the stale tile drawn stale, then re-baked by one idle call),
`stale_visible_tile_rebaked_before_the_draw_by_default`. `render/build.py --test -R bake`: 7/7 ctest, residency 20 run,
0 failed, 0 skipped (02:47-02:49 EST); the same under MSVC ASan (`--asan`, memcap 6 GB): 7/7, 0 ASan reports, job peak
1,284 MB (02:52-02:55 EST). Server: `server/tests/render/test_bake_yield.py` (render_host_test.exe, the stand-in slowed
to 8 ms per bake by `render_test_bake_cpu_ms=8`, budget 100 ms, cameras at 100 Hz looking off the map so that every bake
is an idle one): with the yield, 33-35 slices of one bake each, 31-35 yields, **the wait bound at most 9.6 ms plain**
(one bake; 15-24 ms under ASan, where one `bakeOne` takes that long; `bake_cpu_max_ms` 9.3 counts only the baker's call),
the longest slice 16-17 ms; without it (`render_bake_yield=0`) 0 yields and **slices of 106-108 ms** (the budget plus
one bake) while the cameras kept coming; seven malformed values refused (exit 2). Plain at d1f8f0dc: test_bake_yield 9/9,
test_residency_source 30/30 (03:01-03:05 EST). Under MSVC ASan: test_bake_yield 9/9 (03:16); three load-sensitive
test_residency_source cases failed with 8 other heavy jobs on the machine (three games, two ASan builds, Ghidra), and in
an A/B against main f2f91ea0's ASan build (archived, `C:/c5-64bit-work/0140/main_asan_ab.sh`, 03:13-03:16) the same one,
`test_idle_pacing_before_a_camera_then_legacy_poll` (idle wake-ups 5 against >= 20), fails on both sides, the other two
pass on both: load, not this branch [L].

**Live A/B [L, 2026-11-15, functional, no hold: an indication, not a timing result (COMMON 23, D106; the MC's answer on
the thread)].** Workstation, instance c5h-c (Goldberg, `%USERPROFILE%/c5-instances`, VT present), DX11 EXE 04CFDC71...,
save copy `C5H-0140-HUGE_0350` of `C5H-VP8P-HUGE_0350` (C499D3C6..., turn 350; removed at each restore). One build of the
branch at d1f8f0dc (main f2f91ea0 merged, repo_dirty 0): server.exe `AEDA67DC...`, proxy `48CB38B4...`, composite
`197FFACD...`, the pinned x64 DLL `74588135...` (layout `x64-04b1211b-pa803776`; deps a C: copy of 9045's bF/deps via
0322's). 3311's product arm unchanged (`ServerRenderer = 1`, radius -1, the feed filter, D33 with Overlay on, ring +
fence, lockstep with depth) through `shell/evidence/0140/live_0140.sh` / `run_0140.py` (3311's driver with 0140's names),
30 AI turns 350 -> 379, no reloads, no D93 shots. The two arms differ only in two server.cfg keys appended by vpproxy.ini
`server_cfg_extra=` (issue 0307, TEST ONLY, three LOUD lines in vpproxy.log in both arms, the only LOUD lines): **old** =
`render_bake_yield=0`, `render_bake_stale_before_draw=1` (the behaviour before 0140), **new** = 0140's defaults. Every arm:
30 turns, every step rc 0, guard 0 / 0 / 0, stop 0, restore ok (backup verify, `set-proxy --original` 0, provision
verify 0). The game repeats: the same end state (the same Alcatraz popup at turn 380) and the same re-bakes in all
(11,407 / 11,392), so the per-turn exact shares are paired by turn. Before each arm `quietlock.py check` listed 1-7 other
owners' heavy jobs (games of 0166, 0322, 3906; ASan and plain suites of 3911 and 0166; Ghidra passes of 0338; 3910's
exports; the arm's `jobs.txt`); each arm waited the 20 minutes and then ran with them.

Two pairs, the order reversed in the second (pair A: old 03:33-03:52, new 04:10-04:26; pair B: new 04:44-05:02, old
05:19-05:40 EST, game start to end). Small results in `shell/evidence/0140/` (`summary_*.json`, `lockstep_*.json`, `compare_{a,b}.json`,
`jobs_*.txt`); raw logs and records in `F:/c5-64bit-work/0140/live-*/out/` (D83, D141).

| | A old | A new | B old | B new |
|---|---|---|---|---|
| exact over the turns (`lockstep 1s`, frames published at UpdateViews) | **94.12 %** (97,153 / 103,224) | **94.81 %** (100,463 / 105,960) | **95.20 %** (91,271 / 95,873) | **94.42 %** (103,517 / 109,634) |
| per turn: median / lowest | 94.05 / 91.24 % | 94.66 / 89.89 % | 95.38 / 91.68 % | 94.40 / 91.02 % |
| paired per turn, new - old (30 turns each) | | **+0.71 pt** [+0.07, +1.34], 21 / 9, z 1.94 | | **-0.83 pt** [-1.56, -0.09], 8 / 22, z -2.03 |
| both pairs pooled (60 turns) | | | | **-0.06 pt** [-0.58, +0.46], 29 / 31, z -0.21 |
| give-ups (each recovered) | 34 | 19 | 29 | 25 |
| seconds per AI turn, median | 28.6 | 24.8 | 31.0 | 27.7 |
| CSV: publish -> READY of exact frames p50 / p95 / p99 / p99.9 ms | 3.01 / 3.90 / 4.47 / 7.27 | **2.43** / 3.58 / 4.12 / 6.07 | 3.43 / 4.36 / 5.15 / 8.65 | **2.82** / 3.82 / 4.44 / 6.45 |
| gray frames, record (`unbaked`, 0139's method): server frames / tiles | 11 / 430 | 11 / 437 | 10 / 402 | 9 / 361 |
| residency: gray frames / after the bake-ahead | 21 / **0** | 23 / **0** | 23 / **0** | 23 / **0** |
| bake-ahead after the field | 18.0 s | 18.6 s | 17.7 s | 19.6 s |
| re-bakes in the run | 11,407 | 11,392 | 11,451 | 11,420 |
| pre-draw bakes (re-bakes among them) / longest pre-draw time of one frame | 564 (523) / 30.5 ms | 43 (0) / 29.2 ms | 489 (447) / 30.1 ms | 44 (0) / 36.6 ms |
| idle slices / longest | 7,221 / 27.3 ms | 10,583 / 25.8 ms | 7,571 / 38.3 ms | 10,324 / 29.4 ms |
| yields; wait bound mean / p99 / max | - | 6,574; 2.49 / 4.0 / 23.6 ms | - | 6,134; 2.68 / 4.0 / 28.6 ms |
| stale tile-frames deferred / stale frames | 0 / 230 | 7,747 / 498 | 0 / 199 | 7,379 / 457 |

(Paired figures: mean of the per-turn differences, percentile bootstrap 95 % of the mean, turns better / worse, Wilcoxon
signed-rank z; `compare_0140.py`. The pre-draw time of the new arms is the load's gray bakes before the bake-ahead.)

- **The mechanism works as built [L]:** with 0140 no stale tile is re-baked before a draw (0 against 447-523 per run),
  about 6,100-6,600 slices per run end at a waiting camera, and the wait bound has **mean 2.5-2.7 ms, p99 4.0 ms** (one
  bake; the max 24-29 ms is one long bake, as the 27-35 ms `bake_cpu_max_ms`), where the old slices ran up to 27-38 ms.
- **The exact share did not move measurably on this machine [L]:** the second arm of each pair was the better one (A:
  new +0.71 pt, B: old +0.83 pt), so the order, i.e. the other teams' load, decided each pair; pooled, -0.06 pt with a
  95 % interval of [-0.58, +0.46]. The exact frames' turnaround p50 was 2.4-3.4 ms here against 0.99 ms in 3311's p1 and
  0.767 ms under the quiet lock (3307), with the 3,400-us bound behind it: most older frames came from the loaded
  machine. The exact share (94-95 %) is below 3311's 97.84 % and 0137's 98.25 % for the same reason [I: the same build
  path and game, more load; not measured apart].
- **The turnaround was shorter with 0140 in both orders [L, indication]:** p50 -0.58 / -0.61 ms, p99 -0.35 / -0.71 ms,
  p99.9 -1.2 / -2.2 ms (A / B); the AI turns were 3.4-3.8 s shorter with 0140 in both orders too [I: no cause
  established; could be the render thread's lower CPU use in the frame path or chance with two pairs].
- **No gray frame added [L]:** 9-11 gray server frames per run in the record, the same in all four (at the load, before
  the bake-ahead [I from the residency's `gray_after_ahead`, 0 in all four; not matched frame by frame]); deferring stale
  tiles makes stale frames (457-498 against 199-230: a re-bake shown one or a few frames later), never gray ones.
- **Not shown: that the exact share during AI turns with re-bakes rises toward the idle figure (99.99 %).** On this
  machine it cannot be: the load moves it by more than the change. It needs the same A/B as an oldpc job through the
  testbed queue (COMMON 23), both settings in one interleaved set; the keys and the drivers here are ready for it
  (`server_cfg_extra=`, `run_0140.py`). *(2026-11-15: run by issue 0349 on oldpc, 4.18.1: with 0140 the share rises
  by 0.22 pt, from about 99.28 % to about 99.49 %, in both orders; not to 99.99 %. Grade B.)*

#### 4.18.1 The same A/B on the quiet machine oldpc (issue 0349, 2026-11-15) [L as marked]

**Method [C, the scripts].** One testbed job on `oldpc` (`tb.py submit --headed --quiet`: the job holds both runner slots,
the MC's word on 0349's thread, #1539), `shell/evidence/0349/ab_0349.py`: four arms in the order old, new, new, old (pair
a = arms 1-2, pair b = arms 3-4) in one instance (`tb1` or `tb2`), each `shell/evidence/0349/pp_0349.py` =
`tools/testbed/jobs/product_path.py` unchanged (D33 with Overlay on, `ServerRenderer = 1`, ring + fence, lockstep with
depth, save copy of `TB-HUGE_0350` = `C5H-VP8P-HUGE_0350`, turn 350, 30 AI turns, back-buffer end shot, safe stop,
restore by hash, `provision.py verify`) with three additions: the arm's keys appended to server.cfg through vpproxy.ini
`server_cfg_extra=` (the same keys as 0140's arms, both named explicitly; the one expected LOUD line about it is counted
apart), the composite's `lockstep 1s:` sums and the residency counters per AI turn (3311's method), and a check that
server.cfg and the residency's `yield=` / `stale_before_draw=` counters show the arm (exit 10 otherwise). The fetched
arms are read by 0140's tools unchanged (`summarize_3311.py`, `lockstep_csv_3311.py`, `compare_0140.py`). Builds from
main f6175f76 (0140 merged), 2026-11-15 05:51-05:54 EST, `shell/evidence/0349/build_0349.sh`: server.exe `bcff6087...`,
proxy `2f40dc98...`, composite `764f27b4...`, the pinned x64 DLL `74588135...` (deps: a C: copy of 9045's bF/deps and
src-x64f). Offline checks of the additions: `shell/evidence/0349/test_pp_0349.py`, 13 passed (negative cases included).

**The run [L, 2026-11-15, oldpc host clock EST].** Job `20261115-055632-147c` (commit 3ed0e71c), instance `tb1`
(oldpc's Steam-installed EXE, VT present), 05:56:47-07:21:08: arm 1 old 05:56-06:17, arm 2 new 06:17-06:39, arm 3 new
06:39-07:00, arm 4 old 07:00-07:21 (each 1,260-1,271 s: prep, 134 s to the map, 30 AI turns 350 -> 379, end shot, stop,
restore). Every arm: 30 of 30 turns, the arm check held (server.cfg and the residency counters show the arm's keys),
stop rc 0, restore by hash ok, `provision.py verify` 0, end state turn 380 with the same Alcatraz popup. Each arm and
the job ended with exit 5, from the proxy's expected per-key lines `LOUD: TEST ONLY: server.cfg += render_bake_...`
alone (pp_0349.py's first filter caught only the summary line; corrected in the script afterwards; no other LOUD line in
any arm). The quiet proof: alone (no other job ran; the runner's status every 2 minutes in `runner_status.log`, one job
of 4202 waited for it), **undisturbed = false by a small margin**: foreign CPU 0.114 cores on average against the limit
0.1 (577 s in 5,062 s: Defender 170 s, svchost 167 s, audiodg 56 s, the game's own audio), total CPU 15.2-15.5 % on
average in every arm (`quiet_per_arm.txt`), available memory at least 6,834 MB. Evidence: `shell/evidence/0349/`
(`results_0349.json`, `compare_{a,b}.json`, `summary_*.json`, `lockstep_*.json`, `run_*.json`, `product_path_*.json`,
`quiet_summary.json`); raw logs and the lockstep CSVs in `F:/c5-64bit-work/0349/oldpc-147c/` (D141).

| | 1 old | 2 new | 3 new | 4 old |
|---|---|---|---|---|
| exact over the 30 turns (`lockstep 1s`) | **99.20 %** (25,204 / 203 older / 0 none) | **99.52 %** (25,534 / 124 / 0) | **99.47 %** (25,372 / 135 / 0) | **99.36 %** (25,189 / 163 / 0) |
| per turn: mean (t 95 %) / sd / median / lowest | 99.19 [99.02, 99.36] / 0.45 / 99.21 / 98.14 | 99.51 [99.41, 99.62] / 0.27 / 99.51 / 99.00 | 99.47 [99.34, 99.60] / 0.34 / 99.52 / 98.64 | 99.36 [99.21, 99.51] / 0.40 / 99.40 / 98.51 |
| give-ups | 1 | 0 | 0 | 0 |
| CSV, whole run with the load: exact share; older / none | 98.74 %; 281 / 59 | 99.10 %; 187 / 59 | 99.13 %; 178 / 59 | 98.92 %; 230 / 60 |
| CSV: publish -> READY of exact frames p50 / p95 / p99 / p99.9 / max ms | 2.28 / 3.02 / 3.24 / **7.65** / 54.3 | 2.28 / 3.02 / 3.21 / **4.09** / 59.4 | 2.28 / 3.02 / 3.22 / **4.50** / 110.1 | 2.28 / 3.02 / 3.26 / **7.56** / 51.6 |
| AI turn, end-turn -> next human turn, median (wall) | 35.35 s | 35.45 s | 35.6 s | 35.45 s |
| gray frames: record (`unbaked`) frames / tiles; residency gray / after the bake-ahead | 6 / 218; 18 / **0** | 6 / 255; 22 / **0** | 6 / 238; 20 / **0** | 6 / 222; 20 / **0** |
| re-bakes in the run; residency frames | 11,267; 29,477 | 11,368; 30,097 | 11,386; 29,927 | 11,359; 29,542 |
| pre-draw bakes (re-bakes) / longest pre-draw time of one frame | 391 (350) / 33.7 ms | 40 (0) / 22.9 ms | 46 (0) / 17.3 ms | 390 (351) / 35.5 ms |
| idle slices / longest | 5,650 / 31.0 ms | 6,021 / **12.2 ms** | 5,970 / **10.7 ms** | 5,701 / 31.9 ms |
| yields; wait bound mean / p99 / max | - | 512; 1.97 / 3.5 / 5.7 ms | 473; 1.97 / 4.75 / 5.3 ms | - |
| stale tile-frames deferred / stale frames | 0 / 113 | 4,021 / 239 | 4,114 / 249 | 0 / 116 |

| Paired per turn, new - old (`compare_0140.py`; `process_0349.py` for the pool) | mean | 95 % (bootstrap) | turns better / worse | Wilcoxon z |
|---|---|---|---|---|
| pair a (old first: 1 vs 2), 30 turns | **+0.32 pt** | [+0.17, +0.48] | 23 / 7 | 3.30 |
| pair b (new first: 4 vs 3), 30 turns | **+0.11 pt** | [-0.03, +0.24] | 21 / 9 | 1.80 |
| pooled, 60 turns | **+0.22 pt** (median +0.24, sd 0.42) | **[+0.11, +0.32]** | **44 / 16** | **3.76** |

Smallest difference the pooled pairs could detect (2.8 sd / sqrt 60): 0.15 pt. The game repeats closely, not exactly:
re-bakes per turn equal in 18 and 16 of the 30 turns, totals within 1.1 %.

- **With 0140 the exact share rises in both orders [L, grade B]:** pooled +0.22 pt [+0.11, +0.32], from about 99.28 % (the
  two old arms) to about 99.49 % (the two new arms). In pair b the new arm ran first and still won, so the order does
  not explain it; the old-new-new-old order cancels a linear drift, and the pairs' difference (0.32 against 0.11) puts a
  drift of the machine or the game, if any, at about 0.1 pt per arm position [I].
- **It does not reach 99.99 % [L].** The old frames fall from 163-203 to 124-135 per 30 turns; about 0.5 % of the frames
  during AI turns are still `older` with the yield, at the same p50 turnaround (2.28 ms) in all four arms. [I: their
  cause is not the bake on the render thread; not measured here. Candidates: the server's frame cost while the feed
  applies a turn's changes, the EXE's own frame pacing at ~24 frames per second on this machine.]
- **The tail and the render thread's stalls shrink [L]:** p99.9 turnaround of exact frames 4.1-4.5 ms against 7.6-7.7 ms;
  the longest idle slice 10.7-12.2 ms against 31-32 ms; 0 pre-draw re-bakes against 350-351. Only ~500 yields per run
  here against ~6,100-6,600 on the workstation (4.18): about 24 composed frames per second on oldpc during the turns against about 125 there (3311 p1: 105,713 frames in about 840 s), so a
  camera arrives during a slice less often.
- **No gray frame added [L]:** 6 gray frames in the record in every arm (at the load), 0 after the bake-ahead in every
  arm. The cost: stale frames 239-249 against 113-116 per run (a re-bake shown one or a few frames later; never gray).
- **Seconds per AI turn: no difference** (medians 35.35-35.6 s, wall time with the window; not a turn-time claim, which
  needs turnbench's metric). The 0140 workstation finding of 3.4-3.8 s shorter turns with 0140 does not show here [L].
- **Grade B** (PERFORMANCE.md 2.6: two interleaved runs per arm on the quiet machine, the quiet proof marginally above its
  foreign-load limit). Grade A, which alone accepts or rejects a change, needs at least six runs per arm: one job of
  twelve arms, about 4 h 15 min of `oldpc` with both slots (the same scripts, `--order` of twelve).
- **Recommendation: keep 0140's defaults** (`render_bake_yield=1`, `render_bake_stale_before_draw=0`). The direction is
  the same in both pairs and both orders and the pooled interval excludes zero; nothing measured got worse except the
  stale frames, which 0140 accepted by design (4.18). A grade-A series is worth running only if a decision ever hinges on
  the size of the gain; it would not change the sign.

### 4.19 The device-loss host tests under load; 0382's reflection target not involved (issue 0405, impl-B-0405, 2026-11-16) [L as marked]

The question (MC-B at 0382's hand-back): `test_render_host.py::test_device_lost_rebuilt_in_process` failed 2 of 11 on
0382's branch against 6 of 6 on main, and `test_three_lost_devices_fail_the_renderer_not_the_process` once under ASan.
0382 (merged 2026-11-16 05:25, d3c3552f) made the water scene's t4 a 1024 x 1024 RGBA8 render target with a D24S8 depth
(`srv_water_scene.cpp`), so a GPU resource not released or not rebuilt on device loss would show here.

**Method.** Two arms built from source with `server/tests/render/build_render_tests.py` (plain clang-cl and MSVC
`/fsanitize=address`): **before** = 4c38b6ff (main before the merge, d3c3552f's first parent; tree by `git archive`),
**after** = d3c3552f (main after it). The same test files in both (unchanged by 0382). 30 rounds; in each round the four
arms (before / after x plain / ASan) in a shuffled order (seed 405), one pytest session of the two tests per arm-run, so
both arms see the same machine state. Workstation, 2026-11-16 from 05:38 EST, with 9-15 other heavy jobs registered
(games of 0392, 9073, 9074, 4505, 0340; suites) - a loaded machine, as when the failures were seen. The driver and the
analysis are kept in `server/evidence/0405/`. `render_host_test.exe` SHA-256: before plain c6219e16..., before ASan
fe347260..., after plain 5bdf3761..., after ASan 8ad151f9...

**Result 1: 0382 did not raise the failure rate [L, series 1, 05:38-06:26 EST, 240 arm-runs, 480 test results].**

| Test | Variant | Before: pass / 30 | After: pass / 30 | Failure rate, Wilson 95 % (before; after) | Fisher exact, two-sided |
|---|---|---|---|---|---|
| `test_device_lost_rebuilt_in_process` | plain | 28 | 26 | [1.8, 21.3] %; [5.3, 29.7] % | p = 0.67 |
| `test_device_lost_rebuilt_in_process` | ASan | 24 | 28 | [9.5, 37.3] %; [1.8, 21.3] % | p = 0.25 |
| `test_three_lost_devices_fail_the_renderer_not_the_process` | plain | 30 | 30 | [0, 11.4] %; [0, 11.4] % | p = 1 |
| `test_three_lost_devices_fail_the_renderer_not_the_process` | ASan | 19 | 18 | [21.9, 54.5] %; [24.6, 57.7] % | p = 1 |

All together 19 failures of 120 before and 18 of 120 after. 0382's "6 of 6 on main" was chance: at the measured 7-20 %
failure rate, six passes in a row come out 26-65 % of the time. The rebuild and the start are not slower after 0382 [L,
indication: workstation under load, not a timing claim]: rebuild (device lost -> "created again") median 0.625 s before /
0.612 s after plain, 1.895 / 1.905 s under ASan; start to the first "-> OK" 0.62 / 0.61 s plain, 1.80-1.86 / 1.84-1.88 s
ASan (`extract.py`). The extra 1024 x 1024 RGBA8 target, its render-target view and the D24S8 depth are released in
`WaterScene::releaseDevice` and made again by `init` on the new device (`srv_water_scene.cpp` [C]); in these tests the
water module has no tap and `drawReflection` never runs, so 0382 adds nothing to the draw path the tests exercise.

**The three ways the old tests failed, the same on both arms [L, from the kept render logs]:**
1. **Cameras coalesced (device_lost, plain and ASan).** The test wrote cameras 2..7 at 20 Hz and expected one draw per
   camera; the scene reads one camera slot (`Scene::decide`, newest wins), so a render thread that missed one 50 ms
   window under load drew 2-5 times, never reached the forced loss at draw 6, and nothing wrote another camera
   (example: run 16-after-plain, `blocks=5`, `draws=5`, then 7 s of `drawn=0` until `--seconds reached`).
2. **The 6 s and 8 s process bounds (three_lost under ASan, device_lost).** Under ASan the start takes ~1.8-2.1 s and
   each rebuild ~1.9 s, so start + two rebuilds is about 6 s: the process stopped before the third loss (example:
   10-before-asan, OK at +2.07 s, rebuild 1 done +4.10 s, `--seconds reached` at +6.00 s, rebuild 2 done +6.28 s).
3. **ReleaseDevice took seconds (device_lost, both arms).** From "LOUD: device lost" to "state READY -> REBUILDING" is
   `ReleaseDevice` (scene, sink, `ClearState`, `Flush`, device). Median 0.06 s plain / 0.15 s ASan, but over 1 s 44 times
   in 440 losses and up to 7.0 s (23-after-plain: lost at 06:11:45.437, next line 06:11:52.458), more often in the
   later rounds when 20-25 jobs, several of them games on the same GPU, were registered (`release.py`; before plain 9,
   after plain 11, before ASan 15, after ASan 9 losses over 1 s). Where the time goes inside the release is not measured
   [I: the driver tearing down a device while other processes load the GPU]; it is not 0382's, since before has it as
   often.

**Result 2: the two tests made robust to load (issue 0405, `server/tests/render/test_render_host.py` only).** Both run
with `--stop-event` and `--seconds 240` as a bound only, `-> OK` waited up to 90 s. `test_device_lost_rebuilt_in_process`
writes camera n only after `<base>.Frame`'s `lastDrawnFrameId` shows camera n-1 drawn (or `deviceLostTotal` moved), so
draw n is camera n and the loss lands at camera 6; then waits (90 s each) for the rebuild line and state READY, writes
cameras 10, 11, 12 each after the previous one was drawn, stops, and checks `published >= 8`. `test_three_lost_...`
writes a camera every 0.1 s until "renderer FAILED" (at most 120 s). The assertions on the outcome (state, fail reason,
`deviceLostTotal`, the summary, exit 0, "process alive at exit") are unchanged.

**Series 2: the robust tests on the same four builds [L, 06:26-06:47 EST, 30 rounds, seed 4052, 13-29 heavy jobs
registered]: 240 of 240 pass** (each test x variant x arm 30 / 30; failure rate Wilson 95 % [0, 11.4] % per cell, [0, 1.6] %
over all 240). Longest test 10.8 s (ASan); an arm-run takes 6 s plain and 14 s ASan against 17 s before (no fixed
process length any more). ReleaseDevice still went over 1 s 20 times in 480 losses (up to 4.8 s) and the tests rode it
out. `server/evidence/0405/series2-summary.txt`, `series2-results.csv`.

**The whole `test_render_host.py` with the change [L, 06:47-06:52 EST, after build, 25+ heavy jobs]:** plain 46 passed,
1 failed; ASan 46 passed, 1 failed, 0 ASan reports. The two failures are other timing tests on the loaded machine, not
touched by 0405 and not investigated: `test_ring_handover_slots_ready_per_published_camera` (plain: 15 distinct cameras
seen in the ring against the asserted 20) and `test_frames_per_camera_and_depth_stamp` (ASan: `fake_consumer` did not
open the texture within `consumer_opened`'s 20 s). Both use fixed counts or timeouts of the kind fixed here; named for a
follow-up ticket.

**Not verified:** where ReleaseDevice spends its seconds (no trace taken); the robust tests against a build in which the
rebuild really is broken (the existing assertions would fail through `_until`'s timeout, 90 s, but no such build was
made; done by 0413, 4.19.1); the tests on oldpc or the laptop.

#### 4.19.1 Teeth of the load-robust tests, and the ring and frames-per-camera tests made robust (issue 0413, impl-B-0413, 2026-11-16) [L as marked]

**Method.** Builds with `server/tests/render/build_render_tests.py`, plain clang-cl and MSVC `/fsanitize=address`, into
`C:/c5-64bit-work/0413/b-<name>`: **good** = main bf8fd676 (this branch's sources; the tests change no C++), and three
**mutants**, scratch source trees by `git archive bf8fd676` plus one exact replacement each (`server/evidence/0413/mutants.py`;
never committed, c5render.lib reused from good):
- `norecreate` (the ticket's example): `Rebuild` sets `reason = VPFSF_NONE` instead of calling `CreateDevice`, so it logs
  "rebuild: device and renderer created again" and sets READY with no device, sink or scene;
- `noscene`: on a rebuild (`h.lost > 0`) `CreateDevice` makes the device and the sink but skips `h.scene.init`, so the
  terrain scene is not made again on the new device (and the kept field not re-applied);
- `stamps`: `RingSink::end` writes `r->frameId = f.frameId - 1` into the slot record (latestFrameId and the server block stay
  right), and `V3Sink::publishDepthBlock` writes the depth block's `frameCounter` one behind the colour's.

Series driver `server/evidence/0413/series.py` (as 0405's): rounds, each a shuffled order of arms; an arm = a test tree
(**new**: this branch; **old**: main bf8fd676's `test_render_host.py`, the 0405 version), a build, a variant, and a
`-k` of two tests; one pytest session per arm-run; every start through the quiet-lock gate. Series s1 (from 07:43 EST,
seed 4131): 30 rounds of the **load** arms (new and old tests of ring + frames-per-camera on good, plain and ASan); the
**teeth** arms in the first 10 rounds (the 0405 device-loss tests on `norecreate`; new and old ring + frames tests on
`stamps`) and the first 3 (`noscene`, whose failing runs take 215 s each). Builds 07:18-07:38 EST.

**The two tests as rewritten (`server/tests/render/test_render_host.py`, 0413).** Both run with `--stop-event` and
`--seconds 240` as a bound only; the start-up waits are 90 s.
- `test_frames_per_camera_and_depth_stamp`: the consumer gets `--timeout 150` and 90 s to open the texture; camera n is
  written after the server block's `lastDrawnFrameId` reached n-1 (or `skippedBusyTotal` / `drawErrorsTotal` moved), and
  cameras go on past 40 until the consumer has its 8 frames (at most 120 s more). Every assertion on the outcome is kept;
  the camera stamp bound is the last camera written instead of 40.
- `test_ring_handover_slots_ready_per_published_camera`: camera f is published after the ring's `latestFrameId` reached
  f-1; then the four slot records are read and one READY record (even seq) **of frame f** must be there, with drawResult
  OK, tiles drawn and the 0139 unbaked count. `RingSink::end` writes the record, then `latestSlot`, then `latestFrameId`,
  and `begin` never takes the newest slot, so the record is whole and still there when `latestFrameId` shows f [C
  `shell/frame/ring_sink.cpp`]. The old test sampled the slots 30 ms after each publish and asserted any 20 distinct
  frame ids of 38, without tying a record to its camera; the new one checks all 38 by id and `latestFrameId == 39`.

**Pilot (07:37-07:42 EST, one run each, 12-21 heavy jobs) [L]:** good: both new tests pass (plain 6.5 s, ASan 11 s per
arm-run). `norecreate`: device_lost fails at "state READY again" (the render thread hit the null sink, renderer FAILED
(EXCEPTION), failReason 5) in 31 s; three_lost fails waiting for "renderer FAILED (REBUILDS)" (20 s). `noscene`: after
the rebuild the state is READY but nothing is drawn again (server block `drawsTotal 0`, `lastDrawnFrameId 0`: the
reopened sink resets them), so device_lost fails at "camera 10 drawn after the rebuild" (90 s) and three_lost at "no
FAILED after 1152 cameras" (120 s). `stamps`: frames-per-camera fails at `frameCounter == depthFrameCounter`, the ring
test at "no READY slot record of frame 2".

**Result 1, teeth: every mutant fails both of its tests every time [L, s1 07:43-09:02 EST, 13-28 heavy jobs].**

| Build | Tests | Variant | `device_lost` | `three_lost` | How it fails (every run the same) |
|---|---|---|---|---|---|
| `norecreate` | 0405 device-loss (unchanged) | plain | 0 / 10 pass | 0 / 10 | device_lost: "state READY again: not reached in 30 s", state 4 FAILED, failReason 5 (EXCEPTION: the render thread used the null sink); three_lost: no "renderer FAILED (REBUILDS)" (it failed with EXCEPTION after the first loss) |
| `norecreate` | same | ASan | 0 / 10 | 0 / 10 | the same |
| `noscene` | same | plain | 0 / 3 | 0 / 3 | device_lost: "camera 10 drawn after the rebuild: not reached in 90 s" (state READY, nothing drawn); three_lost: "no FAILED after ~1150 cameras" (120 s: no draw, so no further forced loss) |
| `noscene` | same | ASan | 0 / 3 | 0 / 3 | the same |

| Build | Tests | Variant | `frames_per_camera` | `ring_handover` | How it fails |
|---|---|---|---|---|---|
| `stamps` | new (0413) | plain | 0 / 10 pass | 0 / 10 | frames: a consumer row with `frameCounter != depthFrameCounter`; ring: "no READY slot record of frame 2" |
| `stamps` | new | ASan | 0 / 10 | 0 / 10 | the same |
| `stamps` | old (0405 file) | plain | 0 / 10 | **10 / 10 pass** | frames: the same depth assertion; the old ring test does not see a slot record that names the wrong frame |
| `stamps` | old | ASan | 0 / 10 | **10 / 10 pass** | frames: 9 the depth assertion, 1 "fake_consumer did not open the texture: {"taken": 0}" (the old load failure, round 1) |

So the load-robust device-loss tests of 0405 fail through their own assertions on a broken rebuild, both when it
re-creates nothing (fails in 21-34 s) and when it re-creates the device but not the scene (90-128 s, the `_until`
bounds). The 120 s bound of three_lost is what fails it on `noscene`; a test that ran on fixed time would have caught that
too, a test that only counted "created again" lines would not. Wilson 95 % for 0 / 10: failure probability on the
mutant >= 72 %; for 0 / 3: >= 44 %; every run failed.

**Result 2, load: the rewritten tests 30 / 30 plain and ASan [L, s1, 13-31 heavy jobs, the same rounds].**

| Test | Variant | New: pass / 30 | Old: pass / 30 | Test seconds new (median, max); old |
|---|---|---|---|---|
| `test_frames_per_camera_and_depth_stamp` | plain | 30 | 30 | 2.9, 4.3; 6.6, 7.2 |
| `test_frames_per_camera_and_depth_stamp` | ASan | 30 | 30 | 5.0, 7.5; 6.9, 7.9 |
| `test_ring_handover_slots_ready_per_published_camera` | plain | 30 | 30 | 2.1, 3.3; 6.5, 8.0 |
| `test_ring_handover_slots_ready_per_published_camera` | ASan | 30 | 30 | 3.6, 5.3; 6.7, 9.4 |

Failure rate of the new tests, Wilson 95 %: [0, 11.4] % per cell, [0, 3.1] % over all 120. The old tests also passed
30 / 30 here: their load failures are rarer than 1 in 30 at this load (0405 saw each once, in one whole-file run plain
and one ASan at 25+ jobs; this series saw the old consumer failure once, on the `stamps` build, round 1), so this series does not measure the old
failure rate and Fisher new vs old is p = 1 in every cell. What the rewrite removes is the mechanism, read from the code:
no fixed process length (6 s), consumer life (12 s) or sampling interval (30 ms) remains; every wait is on a count with a
90-150 s bound. The new tests are also faster (no fixed 6 s process) and stricter: the ring test checks every camera's
own slot record (the old one passes on `stamps`), the frames test every camera drawn before the next one is written.
`server/evidence/0413/s1-results.csv`, `s1-summary.txt`.

**The whole `test_render_host.py` on the good build with the change [L, 09:02-09:07 EST, 10-31 heavy jobs]:** plain 47
passed (119 s), ASan 47 passed (146 s), 0 ASan reports.

**Not verified:** the tests on oldpc or the laptop; a mutant per assertion (one broken behaviour per test pair was
built; the `noscene` arm ran 3 rounds, not 10, because each failing run takes 215 s); the old tests' failure rate at this
load (too rare to measure in 30 rounds).

#### 4.19.2 three_lost under ASan after 0405: the failures seen were the old test; one state race closed (issue 0445, impl-B-0445, 2026-11-16) [C/L as marked]

**Where the failures came from [C, git].** Epic 0375's merge request listed `test_three_lost_devices_fail_the_renderer_not_the_process`
failing under MSVC ASan with load "on the base as well" (from 4464's merge check, 2026-11-16 ~06:50 EST: "6 s host run; under
ASan and load the third device rebuild does not come in time", `server/evidence/0375/4464/base_host.sh`). Both trees that
check ran - 4464's base b4d309fa (05:42 EST) and 4464's branch - predate 0405's commit a4a8287f (06:28 EST), which replaced
the 6 s process with `--stop-event` and a camera every 0.1 s until FAILED (4.19). The failure is mechanism 2 of 4.19,
already removed on `main`; 0375's merge (d7dbb94b) carries 0405. The same holds for the ASan rerun note in
`terrain-decals.md` 15.5 (2026-11-15, before 0405).

**One race left in 0405's version [C, `srv_render_host.cpp` at ef9b829a; L, teeth below].** On the third loss `Rebuild`
calls `ReleaseDevice` first; the v3 sink's close in it writes `state = VPFS_STOPPED` (5) and `magic = 0` into the
`<base>.Frame` server block (`srv_v3_sink.cpp`, "tell clients there is no live server"). Then `Fail()` writes the line
"LOUD: renderer FAILED (...)" with `RNote` and only after it `SetState(VPFS_FAILED, reason)` writes `failReason` and
`state` (and the magic) again. The test left its camera loop when the line appeared in the log and read the block once: a
render thread preempted between `RNote` and `SetState` (a loaded machine, ASan's slower code) leaves the block at
STOPPED and the assertion `state == 4 and failReason == 4` fails ("assert (5 == 4)") although the renderer did fail
correctly. `deviceLostTotal` is written at the start of `Rebuild`, before the line, so it was never at risk. Not seen to
fail in 0405's 120 ASan runs or this series (below); closed in the test because the render host is granted to epic 0431.
A side observation [C, not changed]: on every rebuild the block also reads STOPPED with magic 0 between the sink's close
and `SetState(VPFS_REBUILDING)`, a short window in which the composite sees "no live server".

**The change (`server/tests/render/test_render_host.py`, 0445).** The camera loop ends on state FAILED in the block (or the
line); then the block is waited on for FAILED with `_until` (30 s; as device_lost waits for READY) and the REBUILDS line
with `wait_log` (90 s, was 20 s). Every assertion is unchanged: state 4, failReason 4 (REBUILDS), `deviceLostTotal` 3,
exit 0, "--stop-event set after", "process alive at exit", summary FAILED with 3 lost devices.

**Series (`server/evidence/0445/series.py`, `load.py`, `summary.py`).** Builds from this branch (= main ef9b829a + the test
change; the C++ is main's) with `build_render_tests.py` into `C:/c5-64bit-work/0445/b` (plain and MSVC
`/fsanitize=address`, 2026-11-16 17:30-17:35 EST). Two arms on the ASan build: **old** = main ef9b829a's test file (`git
archive` into a scratch tree), **new** = this branch's; 30 rounds, each a shuffled order of the two (seed 445), one pytest
session of the one test per arm-run, each through `quietlock.py run`. Beside it, `load.py` runs the whole ASan
`test_render_host.py` (without three_lost) session after session, the "parallel suite" of the acceptance, until the series
ends.

**Result 1, load [L, s1 2026-11-16 17:35:06-17:43:36 EST, 8-16 heavy jobs registered at the starts (median 10; games of
0325, 0398, 4602, a server, a Ghidra pass, plus this task's parallel suite)]: old 30 / 30, new 30 / 30 pass**; failure rate
Wilson 95 % [0, 11.4] % per arm, Fisher p = 1; test 6.0 s median (max 6.7) under ASan in both. The parallel ASan suite ran
4 whole sessions beside it, 184 passed, 0 failed; 0 ASan report files in either. So on `main` the test no longer fails
under ASan at this load: what 0375's merge request saw was the pre-0405 file.

**Result 2, teeth [L, 17:48-17:51 EST, 8-12 heavy jobs]:** a scratch ASan build in which `Fail()` sleeps 500 ms between
the line and `SetState` (`server/evidence/0445/mutant.py`, never committed), 10 rounds: **old 0 / 10** (every run
"assert (5 == 4)": block STOPPED, `deviceLostTotal` 3), **new 10 / 10** (Fisher p = 1.1e-5). The new test waits the window
out and still fails on a renderer that never reaches FAILED: its loop's 120 s bound and `_until`'s 30 s are unchanged in
kind from 0405's, whose teeth 4.19.1 showed (`norecreate`, `noscene`).

Evidence: `server/evidence/0445/s1-results.csv`, `s1-summary.txt`, `teeth-results.csv`, `teeth-summary.txt`,
`load1-sessions.csv`, `suites.txt`.

**Not verified:** the race seen in an unmutated run (its window is the time of one `RNote`); the tests on oldpc or the
laptop; a fix in `srv_render_host.cpp`, not made.

**Open, optional (MC #2957, 2026-11-16: passed to epic 0431, which holds the file):** in `Fail()`
(`srv_render_host.cpp` lines 236-237 at ef9b829a) swap the two lines, `SetState(h, VPFS_FAILED, reason)` before the
`RNote("LOUD: renderer FAILED ...")`, so the block shows FAILED before the log says so (as `test_device_lost` already
assumes for READY). The test passes either way; recorded here in case 0431 does not take it.

## 5. Configuration (wave 1; the product form of 3305 derives names and switches from `VP64Options.ini`)

Corrected 2026-11-13 by issue 0150 (9055 V7) against the code at main b1d9198f [C]: the table gave the wave-1 defaults
(`render_ring_slots` 3) and did not say which default it meant. Now "code" is what applies when the key is absent and
"product" is what the product path runs with (the proxy's server.cfg lines, `proxy_shell.cpp`; the shipped
`shell/composite/shell_composite.ini`, 4.12 / 4.13).

| Where | Key | Default: code / product | Meaning |
|---|---|---|---|
| server.cfg | `render` | 0 / 1 (proxy) | 1 = start the render thread and the feed |
| server.cfg | `render_section` | `Local\VPShellSection` / `Local\VP64Shell_<EXE pid>` (proxy) | the version-3 section name = `<base>` of `vp_frame.h` |
| server.cfg | `render_exe` | (the game folder's DX11 EXE) | where the terrain shaders are read from (read-only) |
| server.cfg | `render_handover` | `v3` / `ring` (proxy) | `v3` (keyed mutex) or `ring` |
| server.cfg | `render_ring_slots`, `render_sync` | 4, `fence` (`srv_render_host.h`; slots 2..4, the composite's request wins) | ring sink |
| server.cfg | `render_luid` | 0 | adapter by LUID, 0 = the EXE's (from the section) |
| shell_composite.ini | `lockstep` | 1 (code = shipped ini since 0490; code was 0) | 1 = publish at UpdateViews and wait at the insertion |
| shell_composite.ini | `lockstep_mode`, `lockstep_hung_ms` | `exact`, 30000 (ticket 0691, 2026-11-23) | `exact`: no bound, no give-up, only a dead or hung server ends the wait (3.2); `bounded`: the two keys below |
| shell_composite.ini | `lockstep_bound_us`, `lockstep_giveup` | 8000 (0675; was 3400), 8 | `bounded` only: the bound, misses in a row before no wait |
| shell_composite.ini | `handover` | `ring` (code = shipped ini since 0490; code was `v3`) | `v3` or `ring` (the FrameSource used) |
| shell_composite.ini | `ring_slots`, `ring_sync` | 4, `fence` (`VPFRAME_DEFAULT_SLOTS`, `VPSYNC_FENCE`) | the ring the composite asks for |
| shell_composite.ini | `depth_write` | 1 (code = shipped ini since 0490; code was 0) | 1 = write the server's depth into the EXE's D24S8 |
| shell_composite.ini | `lockstep_csv` | 0 | 1 = per-frame records |

**The compositor's configuration has one truth (ticket 0490, 2026-11-17) [C code; L stand-in tests].** Each key is read
from `VPSHELL_<KEY>`, else `[composite]` of `shell_composite.ini` beside the DLL, else the built-in default
(`shell/composite/shell_composite.cpp` `struct Config`). Until 0490 seven defaults were the old test values and not the
shipped ini's: `insert` clear (ini `tagged`), `tap` 0 (1), `lockstep` 0 (1), `handover` v3 (`ring`), `depth_write` 0 (1),
`shadow_tap` 0 (1), `ice_tap` 0 (1) (found by 0464, MC #3830); a compositor without its ini (the installer shipped none)
would have run a configuration no test ran. Now every default equals the shipped ini's value, and
`shell/tests/test_config_defaults.py` runs the DLL on the stand-in with the shipped ini and with no ini and compares the
logged values key by key (fails when either changes alone; checked by a mutation, `ice_tap=0` in the ini, 2026-11-17).
What the DLL logs, at load and at each re-read: `values used i/N (K keys): key=value ...` (every key, the ini's spelling;
62 keys; the shipped ini carries 61 of them since the MC's answer on 0490: `water_tap=1`, `cut_tap=1`, `bake_tap=0`,
`fault_test=`, `hook_test=` were added with their built-in values, and only `tag_anchor` stays out, because its absence
is itself a value, the tag rule's own default anchor; the test fails on any other key the DLL reads that the ini lacks),
then either
"the ini's values equal the built-in defaults (K keys compared)" or "the ini differs from the built-in defaults in n of K
keys: key=value (default x) ..." (the ini alone, without the environment), or, once per state, `CONFIG ERROR <path> is
missing | cannot be read (error N) | has no [composite] section: every key not set by a VPSHELL_<KEY> variable takes the
built-in default`, and "is readable again" when it comes back. `model_fow` is logged and compared there too
(`FOW_MODELS_DEFAULT` 1); `fow_models.cpp` and the proxy (`proxy_shell.cpp` `VPSHELL_MODEL_FOW_DEFAULT`) still read it
themselves, unchanged. The installer ships the ini beside the compositor (installer-notes 12.4b). Keys a module reads
itself and the shipped ini does not carry (`decal_budget_ms`, `ice_settle_frames`, `fow_models_wait_ms`, ...) keep their
defaults in the module headers and are not in the comparison.

**`shell/tests/test_lockstep.py` was failing on main (found by 0490, 2026-11-17) [L baseline; I for the start].** 28 of
its 36 tests failed on main f4ff50b84 (a build of main's own sources, the same 28 ids as on the 0490 branch before the
fix): `LsRun.finish` refuses any `DISABLED` or `EXCEPTION` line, and the stand-in's fake EXE is refused by every tap that
validates the real EXE's bytes (water: "FAILED REFUSED ... EXE refused - DISABLED"; cut and decal: "pattern ... does not
match (fault) - DISABLED"; overlay, shadow, ice and the model fog refuse too). Start [I, from the history, not rebuilt]:
`water_tap` became 1 by default in 72add72c3 (3608, 2026-11-14 03:27); overlay (4a6257124, 2026-11-14 13:55), decal
(b901aa013, 2026-11-15 02:51) and cut (ff8c3e9ee, 4554, 2026-11-16 18:15) followed. Why nothing caught it: there is no
runner of the whole shell suite; each ticket runs the test files of what it touched, the tap tickets ran their own
`test_*_tap.py`, and the last recorded `test_lockstep.py` run is 0139's acceptance of 2026-11-13
(`shell/evidence/0139/acceptance_0139.txt`). Fixed in 0490: the `lrun` fixture and the shipped-ini test set the taps that
need the real EXE off (`EXE_TAPS_OFF`); `finish` still refuses any other DISABLED line. Running again, two tests showed
load sensitivity [L 2026-11-17, workstation with 5-8 other heavy jobs]: `test_ring_record_carries_the_unbaked_count`
asserted that the "lockstep 1s:" sums cover every CSV row, but both ends are cut (the CSV after the control thread's last
0.5 s flush, the 1s lines after the render thread's last stats interval), and it lost that race in 4 of 6 runs; the
check now allows one interval's frames (6 of 6 after). `test_ring_handover_lockstep` (late waits over 2 ms at most 1 %)
and `test_publish_at_updateviews_exact` failed once each under load or ASan and passed on reruns; left as they are.

Product names (3305): `<base>` = `Local\VP64Shell_<EXE pid>`, written by the proxy into server.cfg and passed to the
composite at load; the composite loads from the patch's own folder, switched by `VP64Options.ini` and hash-checked.

## 6. The scene and the seam with epic 3400

- 3301 brings the terrain scene of `shell/server/shell_server.cpp` (tap poll, bake-tap poll, the draw with depth, the
  per-second line) into `server/src/render/` as a copy with its origin named; `shell_server.cpp` itself is not edited
  by team 3300 (team 3400 runs its scene there until its residency is merged). After both epics have merged, the
  `--scene terrain` path of `shell_server.exe` is either a thin wrapper over the server's scene or retired (MC's call).
- Tile caches come through `srvrender::TileCacheSource` (`server/src/render/srv_tile_source.h`, a proposal to team
  3400 through the MC): the EXE-cache tap of 3200 is the first implementation; team 3400's baker and residency
  (`c5r::bake::TileBaker`, D88's all-tiles-resident default, the radius option) is the second, written in `render/`
  as a library so that `shell_server.exe` and server.exe share one copy. The feed gives that source the plots whose
  content changed (routes, improvements, features) for re-bakes (epic 3400 acceptance D), from the DLL's own
  notifications instead of EXE memory.

## 7. File ownership (team 3300)

| Path | Owner | Note |
|---|---|---|
| `shell/common/vp_frame.h`, `shell/frame/frame_sink.h`, `frame_source.h`, `server/src/feed/srv_feed.h`, `server/src/render/srv_tile_source.h` | lead | contracts; workers implement against them |
| `server/src/render/*` (render host, v3 sink, scene), `server/src/remote/srv_remote_main.cpp` (start/stop of the render thread, server.cfg keys), `server/build_server.py` (render units, c5render link), `server/run_server.py` (render keys in `--remote-config`) | 3301 | |
| `shell/frame/*` except the two headers (ring sink, ring source, stand-in producer/consumer, own build script) | 3302 | |
| `server/src/feed/srv_feed.cpp`, the notification overrides in `server/src/remote/srv_remote_objects.cpp`, one call in `server/src/remote/srv_rt.cpp` (LeaveUpdate), `proxy/gen/classify.py`, `proxy/model/consumers.tsv`, `proxy/model/policy-table.tsv` (regenerated) | 3303 | srv_rt.cpp and classify.py: one change each, announced to the MC |
| `shell/composite/*`, `shell/common/shaders.hlsl`, `shell/build.cmd`, `shell/standin/test_host.cpp`, `shell/tests/*` (new tests) | 3304 | 3400 does not edit these (to be confirmed by the MC) |
| `proxy/src/proxy_shell.cpp`, `proxy/src/proxy_rt.cpp` (its one call), `options/` (new keys, with 3400's lead through the MC) | 3305 (wave 2) | |
| not touched by 3300 | | `render/src/bake*`, `render/include/c5r/bake/*`, `shell/server/shell_server.cpp` (team 3400) |

## 8. Premises (`[I]` until the named issue checks them first, at most 15 minutes each)

1. A shared `ID3D11Fence` can be created by the server's device and opened in the EXE's 32-bit process on the EXE's
   device, and `ID3D11DeviceContext4::Wait` works there (3302). **Holds [L 2026-11-13 05:49, two tiny programs on the
   RTX 4080; section 4.4].**
2. Shared textures without keyed mutex, opened by name in the EXE, can be sampled directly by the composite without a
   copy and stay coherent with the fence (3302). **Holds on the stand-in [L 2026-11-13, x64 producer / x86 consumer,
   colour and depth texel read back per frame], with one condition found there: a new fence per generation, because a
   fence opened by a process that ends is completed to UINT64_MAX (section 4.4).**
3. The UpdateViews hook can carry the camera read (the read's cost at Present was measured per Present as `g_camT`;
   3304 reports it at the new point) and the camera is identical to the one read at Present (3304: compare both reads
   for N frames in one run). **Holds live [L 2026-11-13, 3306 run r2, section 4.7]: 0 of 155,598 frames of 30 AI turns
   differed (`lockstep_check=1`); the read at the hook cost 6.8 us (median of the 1-s means).**
4. The EXE's scene-clear DSV can be bound and written with `SV_Depth` at the insertion and the EXE's later lists use it
   unchanged (3304, stand-in first, then live). **Holds live [L 2026-11-13, 3306 run r2, section 4.7]: depth written in
   every one of 155,598 frames (0 refused, 0 without a DSV); the EXE's static models lose 10.7-15.5 % of their pixels to
   the server's terrain at the four views of 3200 and keep the rest; not compared with stock at the same camera.**
5. The server's DLL-side notification overrides see the plot's coordinates through pure getters (3303). **Confirmed
   [L] 2026-11-13** (3303, section 2.2): `ICvPlot1::GetPlotIndex` on the DLL's own wrapper inside the call; FOW events
   carry x, y. Not for cities: no pure getter gives a city's plot (section 2.1).
6. server.exe can create its D3D11 device on the EXE's adapter while started with `CREATE_NO_WINDOW` under the window
   guard (3301). **Holds [L 2026-11-13 07:47, section 1.2]**: proxy-started server.exe with `CIVBG_GUARD=1`, device on the
   RTX 4080 in 1.3 s, no window event in the guard log, 0 LOUD lines (headless: no EXE adapter to match; the live EXE's
   adapter is checked by the joined run of wave 2).

## 9. Tests (product level first, D81 point 4; COMMON 9: negative tests, ASan once)

- 3301: the render host against a fake composite (a test program that maps the version-3 section, writes camera
  blocks and a tap from a C5HF file): frames published per camera, the depth stamp, device-lost rebuild (forced by a
  test switch), render-thread exception -> FAILED while the process goes on, no DLL include in `server/src/render`
  (a build check); a headless proxy-route game with `render=1` and no composite (the render thread idles, the game is
  unchanged).
- 3302: stand-in producer and consumer, both bitnesses: every frame id arrives exactly once, ordering, slot reuse under
  a consumer that holds slots, fence and CPU modes, a killed producer, a killed consumer, generation change; cost per
  frame against the keyed mutex on the stand-in under the quiet lock.
- 3303: the queue (overflow, order, drain), the table (snapshot, patches), the overrides in a hutest-style pair, and
  the determinism test of section 2.
- 3304: on the stand-in: publish -> wait -> exact, miss -> previous picture, bound respected within 0.5 ms, give-up and
  recovery, killed server, `lockstep_bound_us` out of range (negative), depth written where the server drew and 1.0
  elsewhere (read back from the DSV); live: the per-frame records.
- Wave 2: the joined run (acceptance A, 30 AI turns on the huge save through the proxy route with the renderer in
  server.exe and no shell_server.exe), lockstep series under the quiet lock (B), frame time against stock and against
  today's compositor (C), depth at 3200's cameras (D), memory of both processes (E).

## 10. Not decided here

- Whether `shell_server.exe --scene terrain` is retired after the join (MC, with team 3400).
- The exact bound default (3304 measures).
- Fog, borders, overlays and models: later epics; the feed carries their notifications already.
