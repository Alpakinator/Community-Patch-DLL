# Independent verification of the terrain surface drawn by the server (epic 3200), one-off 9038

Written 2026-11-12/13 by one-off 9038, an agent that did not build epic 3200. Subject: `render/`, `shell/` and the
research of epic 3200 at `main` 6623b80 (merge 49fce50, D79), with `main` 1b16d93 merged into the verifier's branch
before the live work (no change to `render/` or `shell/` code in between). Every number below was measured in this
session unless it says "their tool" (the team's own program rerun) or "their data" (the team's raw files, reanalysed).
Scripts: `shell/evidence/9038/tools/`; small outputs: `shell/evidence/9038/`; full data `D:/c5-64bit-work/9038/` (not
committed).

Marks: [C] measured here offline (own code, dump, stand-in run); [L] measured here in the live game; [I] inferred.

**Pins used: the old pins, as the epic.** The current i0-ship pin build (`<pins_dir>/dlls/i0ship-bbfacca1-ia`)
has no ViewMaskSync (the string `viewmask.enabled` is absent from the DLL; the test DLL has it), so the D33 shell
configuration with the Terrain view bit cleared (D7 c1) cannot run on it [C, 2026-11-12 23:16]. Every live result here
is on the test DLL `i2-shell-test` b92373aeb (SHA-256 638B5E21...83B4, `deps/i2-shell-test.lock`, installed with
`provision.py set-dll`), i.e. on i0-ship ccfd6dd4d. Never compare these with numbers on the new pins (COMMON 18).

**Builds:** fresh clone of 6623b80 at `D:/c5-64bit-work/9038/clone`; `render/build.py` into
`D:/c5-64bit-work/render-build/02c9f478ab/`; `shell/build.cmd` into `D:/c5-64bit-work/9038/shell-build`
(`shell_composite.dll` SHA-256 44519EFF...C45, built 2026-11-12 23:20; the team's live DLL was 739CA1AB..., an earlier
build of the same files). Every build with `VSCMD_SKIP_SENDTELEMETRY=1` (section 2, bug 0108).

**Live:** instance c5h-d (Goldberg, claimed under 9038, backed up by SHA-256 at
`D:/c5-64bit-work/9038/backup-c5h-d`, `provision.py snapshot` before), DX11 EXE (TimeDateStamp 0x546CD0A8), CP/VP/EUI,
2554 x 1411, RTX 4080. **Save of the verifier's choice: `C5H-3200-V9038-T0359`** = a copy of c5h-d's
`AutoSave_0359 AD-1939` (huge map, turn 359, written by 3206's soak; SHA-256 bca895b7...ddfd), not the team's turn-350
save. Every launch: guard self-test ok, start-up watch ok, no dialog, no crash, no hang; stopped with `cli.py stop` after
the newest autosave settled; ToggleStrategicView never called.

## 1. Verdict per claim

| # | Claim (epic 3200 merge request, research) | Verdict | Section |
|---|---|---|---|
| 1 | `render/build.py --test` 9/9 from a fresh clone; ASan variant | **confirmed by own measurement** (9/9 plain and ASan, 0 skipped, canary fired, 0 reports) | 2 |
| 2 | shell suite 237 passed, 0 skipped | **stale**: the merged head has 293 tests; own run without test_errors.py 236 passed, 1 failed (timing flake, V1), 0 skipped; test_errors.py not run (bug 0108) | 2 |
| 3 | tap P3 bit-exact on real memory blocks | **confirmed by own measurement**, against an independent reader (epic 3000's gt_reader), on another save, two loads | 3 |
| 4 | tap P4: two loads differ by 1 u16 at a few dozen samples (one of 46) | **wrong in size**: outliers of 240 and 264 u16 (0.21 world units) under water; tol 1 does not hold (corrected) | 3 |
| 5 | cache tap: pre-bake at load only, 135 of 10,752 tiles; caches open in the server | **confirmed by own measurement** on the turn-359 save (135 slots, 135 opened, again after a reload); "no bake in play" not re-tested | 3 |
| 6 | server scene, fallback colour, depth (P8 facts, R32 depth published) | **confirmed by own measurement** (their probe); the live frame is the draw (99.84 % class agreement, own check) | 4 |
| 7 | 30-min run, 0 missed insertions in map frames, misses only on leader screens; no crash | **confirmed by own measurement** (12 min, 3 turns, 200 pans; 1,474 misses all on leader screens); their soak's 1.96-s frame and timeouts explained here (turn stalls; keyed-mutex busy, 2.7 %) | 5 |
| 8 | frame time stock 5.384 vs D33 + terrain 4.323 ms (-1.061, CI -1.246..-0.876), grade A | **confirmed from the raw data with own code** (-1.066 [-1.250, -0.882]); own series not completed (lock refused), one own stock launch 5.141 ms inside their stock range; size depends on stock round 0's load by about 0.1 ms | 6, 11 |
| 9 | in process D33 passthrough 3.530 vs D33 + terrain 4.199 ms (+0.670), keyed mutex + copy | **confirmed from the raw data with own code** (+0.668 [+0.627, +0.709]); no own in-process series | 6 |
| 10 | EXE private 2,039 -> 1,622.5 MB; largest free block 1,201 -> 1,595 MB; server 1,143 MB / 873 MB video; tap section released | **confirmed from the raw data** (equal); own single samples agree (section 11); release 273-519 ms confirmed in the raw log and 573 ms in own run | 6, 3 |
| 11 | pan lag mean 6.1 ms (2.6-13.1), not above 10-22 ms | **conclusion confirmed by own measurement, number misleading**: own run at zoom 17, raw 8.6 ms by their method, but along the pan -1.6 ms [-3.4, +0.3] (W8's data: -0.9 ms); the 6.1 ms is mostly the static offset | 7, 11 |
| 12 | geometry median 0.57-0.97 px; 95 % within 3 px fails at 4 of 6 (reference band disturbed) | **confirmed by rerunning their tool, and by an own metric** (median 0.66-1.09 px, no bias; 95 % fails at 3 of 6 with own binning); **at four own clean cameras: median 0.59-0.88 px, 95 % within 3 px at 3 of 4 (their metric) / 4 of 4 (own)**, which supports the inferred cause | 8, 11 |
| 13 | colour reported, not judged (D67) | **confirmed by rerunning their tool** (table equal); own simpler mask gives worse numbers | 9 |
| 14 | research written | **mostly good**; four statements corrected or completed in place (section 10) | 10 |

## 2. Fresh clone, builds and suites [C]

- `python render/build.py --test` (through `quietlock.py run`), 2026-11-12 23:17:53-23:18:27: **9 of 9 test programs,
  0 skipped**, the game-file tests ran (C5R_GAME_DIR the pure_vp copy, C5R_EXE the Ghidra copy). **Confirmed.**
- `python render/build.py --asan --test`, 23:18-23:19: the canary reported its overflow first, then **9 of 9, 0 skipped, 0
  ASan reports**, peak committed 1,601 MB of a 6,144 MB cap. **Confirmed.**
- `cmd /c shell\build.cmd` (C5_SHELL_BUILD set): BUILD OK, 38 s. **Confirmed.**
- **Shell suite: the merged head has 293 tests, not 237** [C, `pytest --collect-only`]: test_terrain_scene 92,
  test_errors 56, test_tap 55, test_bake_tap 27, test_geom_tail 23, test_camera 16, test_tagged 12, test_camera_offline 6,
  test_build_cmd 3, test_standin 3. The lead's "237 passed" (2026-11-11 14:41-14:56) and W6's ASan "237 of 237"
  (15:04-15:21, `asset-pipeline-verification.md` 3209 part) were true of the tree at that time, which had
  test_terrain_scene at 59 and no test_geom_tail; W5 then added 33 terrain-scene tests and W7b 23 geom_tail tests, and
  no run of the whole suite at the merged head is recorded (W5 ran three files, 174 tests; W7b its 23; W8 its 14 `s7_`).
  So the merge request's "shell suite 237 passed" is **stale, not wrong** (auditor observation 1 of 2026-11-13 explained:
  the two 237s are the old total with test_errors, and, by coincidence, the new total without it).
- **This verifier's run of the merged head without test_errors.py** (bug 0108: it starts `powershell.exe`), 23:20:36-
  23:34:03 through `quietlock.py run`, 807 s: **236 passed, 1 failed, 0 skipped.** The failure:
  `test_bake_tap.py::test_real_path_refuses_an_exe_without_the_patterns` timed out waiting 10 s for the refusal log line;
  in that run the stand-in's hooks took **6,547 ms to commit** (`hooks committed (dummy device): 18 enabled in 6547.2 ms`),
  normally well under a second; the same test passed 3 of 3 alone (23:34:18-23:34:42). A timing flake under machine load
  (another team's x64 DLL build and suites ran then) [L for the times, I for the cause]: the test's 10-s wait does not
  allow for a slow hook commit. Defect V1 (low).
- Not run: `test_errors.py` (56 tests; bug 0108 rule), the shell ASan suite at the merged head (W5 ran
  test_terrain_scene's 92 under ASan on 2026-11-11; the 23 geom_tail tests are pure Python). The **shell ASan build links
  the non-instrumented c5render** (`shell/build.cmd` lines 26-27, its own comment; `asset-pipeline-verification.md` names
  it "not covered"): c5r::terrain inside `shell_server.exe` is ASan-checked only by render's own suite (which passed
  here). Auditor observation 2 confirmed [C]; it is documented, not hidden.
- **VsDevCmd starts PowerShell** [C, `Common7/Tools/VsDevCmd.bat` lines 250-256]: unless `VSCMD_SKIP_SENDTELEMETRY` is
  set, every `vcvarsall.bat` (render/build.py, shell/build.cmd, the harness native build) starts
  `START "" /B powershell.exe -NoProfile -Command ... Send-VsDevShellTelemetry` in the background. Bug 0108's note that
  "the builds do not use PowerShell" is therefore not true for any build through vcvarsall; the build itself does not
  wait for it, but under bug 0108 each build leaves one more hung `powershell.exe`. Set `VSCMD_SKIP_SENDTELEMETRY=1` in
  build wrappers. *(Corrected 2026-11-14, issue 0147 [C, read and grep-checked at branch task-0147-vscmd-telemetry]:
  every tool outside `*/evidence/` that runs vcvars32/vcvars64/vcvarsall now sets `VSCMD_SKIP_SENDTELEMETRY=1` in that
  call's environment or gets the environment from a module that does. Setting it: `ipc/build.py`, `models/build.py`,
  `render/build.py`, `tools/x64-dll/build.py` (`run_vcvars`), `proxy/gen/toolchain.py` (`run_in_env`),
  `proxy/tools/test_proxy_rt.py` (`vcvars_env`), `proxy/tools/test_purity.py`, `options/test_asan.py`,
  `shell/build.cmd`, `shell/frame/build.cmd`, `shell/frame/tests/premise/build_premise.cmd`,
  `tools/harness/native/build.bat` and `wrapper.py`, `server/tests/feed/run_feed_test.py`,
  `server/tests/render/build_render_tests.py`, `server/tests/render/water/build_water_tests.py`; added by 0147:
  `proxy/gen/verify_slots.py`, `proxy/gen/extract_lua.py` (`clang_env`), `tools/drift/build_mathcheck.py`,
  `tools/x64-dll/save-audit/save-width-census.py`, `tools/x64-dll/cvgamedatabase/build_db.py` and
  `closedlib-stubs/build_stubs.py` (`vcvars_env`), `tools/harness/native/tests/test_guard_shapes.py`. Everything else
  that names vcvars inherits from one of these (`server/build_server.py` and the server tests through
  `tools/x64-dll/build.py`; `proxy/proof/build_proof.py` through `toolchain.py`; the CMake toolchain files through
  their `build.py`) or is documentation: 90 files name vcvars, 23 set it, 30 inherit it, 37 are text, 0 uncovered.
  The per-file list (each with its line or module) is the acceptance output of issue 0147's hand-back; to repeat it,
  `grep -ril vcvars` outside `.git` and `*/evidence/` and look for the variable in each code file. Replaced: the 2026-11-13 4000 group C note that
  render/build.py, shell/build.cmd, server/build_server.py, proxy/gen/toolchain.py and options/test_asan.py did not set
  it.)* (At 23:16 the only powershell.exe processes were four of the Claude app, two started at 22:51 and
  22:58 with 9-11 threads, i.e. not the 1-thread hang signature of 0108 [L, psutil, `tools/pslist.py`]; not tested
  further, per the MC's rule.)

## 3. The height-field tap: P3, P4 [C, L]

**Their data, own code.** `tools/hfcmp.py` (numpy, reads C5HF and epic 3000's raw u16 dumps; not hfbake) on the team's
saved files reproduces every count of `shared-texture-and-compositor.md` 11 and 14: run1 and run3 taps = their raw
decodes (0 differing, byte layer 0); run1 tap vs 3004 dump 37 by 1; Tiny 8 by 1; W5's acked tap vs 3004 26 by 1; pairwise
loads 32 / 43 / 29 differing, the one 46-step sample (7211, 3060) in the reload; W5 vs W2 27 by 1
(`hfcmp_team/pair*.json`).

**Own live run (2026-11-12 23:36-23:55, c5h-d, D33 + terrain, the verifier's save) [L].** Tap: `copy 1 of load 3`,
132,120,704 B in 553.3 ms (pool reads 44.1 ms, hash 414.1 ms), READY 23:40:18.942, **RELEASED 573 ms after READY**
(ack from the server, result 0, section closed). Then, independently of the team's flattener, epic 3000's Python reader
(`shell/tools/gt_reader.py heights`, its own directory/pool reassembly, ReadProcessMemory) read the field 1 min later:
- **P3 holds bit for bit**: the server's acked tap of load 3 = gt_reader's read (0 of 44,040,192 samples differ); the
  same for the second load (load 5) [L, C]. **Confirmed by own measurement.**
- **P4 (two loads of one save in one process): not bit for bit, and larger than stated.** Load 3 vs load 5 (a reload
  through `tap_live.py reload`): 31 samples differ, 29 by 1 u16 and **two by 240 and 264 u16** (0.19 and 0.21 world
  units): sample (3072, 814) 9344 -> 9584 in the reload, sample (6921, 1847) 129 in load 3 against 393 in load 5, in the
  3004 dump and in W5's tap. Both lie far below the water plane (u16 12,899), on the ocean floor, as 25 of the 31 do;
  byte layer equal. The team's statement "1 u16 at a few dozen samples, one outlier of 46" and its advice "`hfbake
  compare --tol 1` separates these from real errors" do not hold: single under-water samples jump by up to 264.
  **Wrong in its size; corrected in `shared-texture-and-compositor.md` 11.** The location of every outlier so far is
  under water, where the EXE draws water over the field [I that it is never visible].
- Turn 359 against turn 350 (the 3004 dump, W5's tap): 33 samples differ, the same pattern; the byte layer differs from
  W5's turn-350 tap at 9,990 samples (game-state changes in nine turns [I]).

**The cache tap [L].** On this save too: pre-bake list count 121 (TR+0x231C), the bake tap published **135 slots, all
135 opened by the server, 0 missing** (`bake manifest seq 395 applied: 135 slots, 135 opened`), the same counts as W3's
turn-350 run, and again after the reload (135 slots, 0 missing; the server's `nocache` count at the view unchanged, 44 of
106 tiles). bake_probe's TR fields as W3 (`bake_probe_{1,2}.json`). **Confirmed by own measurement on another save**
(the claim "only at load": not separately re-tested in play; W3's camera-move test stands).
Bake tap cost on the render thread (its own 1000-Present lines, 176 windows over the run, incl. reload, AI turns and
pans): mean per Present **0.067-0.143 ms, median 0.084 ms** [L]. W3's "steady state 0.061-0.079 ms mean" (six windows at
rest) is a little low for a run with movement; not a PERFORMANCE.md measure on either side.

## 4. The server scene, the fallback, depth; the live frame is the draw [L, C]

- Server log (`live1/server_key_lines.txt`): field 128 x 84 plots as 128 x 84 tiles of 1 plot in **2,381.5 ms** (load 3)
  and 2,463.4 ms (load 5); depth texture `Local\VPShellWorldDepth_<pid>_1` R32_FLOAT published (`--depth 1`); state OK,
  one draw per camera block (152,162 draws for 152,162 blocks in the soak), 0 draw errors. `shared-texture-and-compositor.md`
  13's "setHeightField about 0.9-1.1 s on Huge" was measured with 2-plot tiles (2,688 tiles); live with 1-plot tiles it
  is 1.8-2.5 s (W5 1,836.7 ms, here 2,381-2,463 ms); corrected there.
- **Fallback colour:** uncached tiles drawn magenta; at the load view 13.1 % of the drawn terrain pixels are magenta.
- **The live composited frame is the c5r::terrain draw [own check].** At the load view (no camera move; passthrough and
  composite shot at one camera, raw camera bytes equal) terrainshot drew the acked tap at that camera; on the 2,530,705
  pixels the passthrough leaves at the clear colour, the live class (server water colour (42, 74, 110) exactly, or
  terrain) agrees with terrainshot's world image on **99.84 %**, 0 clear pixels left undrawn, waterline median 0 px both
  ways (`class_check_loadview.json`, `tools/class_check.py`). **Confirmed** (W5: 99.76-99.996 %).
- **Depth (P8, issue 3208) [L, their probe, own run]:** `depth_probe=1` for 15 s (23:40:55-23:41:10): per 5 s 1,105-
  2,191 frames; exactly two ClearDepthStencilView per frame, D24S8 (45) 1024 x 1024 and 2554 x 1411, `flags 1 DEPTH
  depth 1 stencil 0`; the lists' depth state LESS_EQUAL, write all, stencil off; every FinishCommandList and
  ExecuteCommandList with restore FALSE (202,896 of 202,896); at the insertion no DSV and the null state. **Confirmed.**
  The offline conversion tests (`test_tap.py test_depth_*`) passed in the suite run of section 2.
- **Server memory over reloads (corrected 2026-11-13 by bug 0121; replaces "+289 MB that stays [L, one reload]", 1,207 ->
  1,496 MB with a 2,143 MB peak, "whether each further load adds the same is not measured").** Two runs of 5 and 9 loads
  of one save in one EXE process [L]: the server's private bytes rise in **steps over the first 2-4 reloads (+333 to +432
  MB in all), then by 0-28 MB per reload, and stop near 1.55-1.6 GB**; nothing grows by a field per reload; video memory
  is flat. *(2026-11-15, bug 0121 on the pinned release layout `x64-04b1211b-pfdee656`, product path: the first-reload
  step of +289 to +432 MB does not reproduce; huge +84 MB then a plateau within 22 MB from reload 4, huge after 10 AI turns
  +59 then back at the pre-reload size, Tiny +80 then +0.6 MB per reload; nothing kept per reload. Current statement:
  `research/04-memory-investigation/server-reload-memory.md`. The numbers below are history, measured in shell_server.)*
  Settled values (60 s after the load's tap was read; psutil private bytes, = the server's `mem periodic` line):

  | load | run 1 server MB | run 1 EXE MB | run 2 server MB | run 2 EXE MB |
  |---|---|---|---|---|
  | 1 (first) | 1,144.8 | 1,618.6 | 1,180.3 | 1,634.1 |
  | 2 | 1,517.1 (+372.3) | 1,635.9 | 1,358.9 (+178.6) | 1,660.4 |
  | 3 | 1,519.6 (+2.5) | 1,656.3 | 1,339.2 (-19.7) | 1,678.9 |
  | 4 | 1,569.2 (+49.6) | 1,668.6 | 1,513.4 (+174.2) | 1,687.6 |
  | 5 | 1,576.9 (+7.7) | 1,663.9 | 1,525.4 (+12.0) | 1,687.7 |
  | 6-9 | - | - | 1,553.7 / 1,564.4 / 1,588.1 / 1,586.1 | 1,695.2 / 1,705.1 / 1,698.6 / 1,701.9 |

  Server video memory (`video_local`) at every settled load 875-884 MB in both runs; peaks during a rebuild: private
  1.58-2.35 GB (2-s samples), video up to 1,628 MB. The EXE's private bytes creep +45 MB (run 1, 4 reloads) and +68 MB
  (run 2, 8 reloads), most of it in the first 3-4 reloads; its largest free block 1,613 -> 1,540 MB and 1,624 -> 1,515 MB.
  Where the server's rise sits [L, VirtualQueryEx of the server at each settled load, run 2]: one 84.0 and one 42.0 MB block
  (by size t.grid's u16 heights and byte layer, 8192 x 5376 x 2 and x 1 B [I]) are there at every load; the growth is in committed private allocations of 4-64 MB (load 1: 29 of
  16-64 MB = 929 MB and 8 of 4-16 MB = 118 MB; load 9: 43 = 1,222 MB and 19 = 213 MB), not in small ones. [I] These are
  allocator or driver pools sized by the rebuild peak (`setHeightField` drops the old tiles first, `ApplyField` keeps the
  old t.grid until the new one is moved in, `--save-tap` holds a third 126 MB copy), kept after the rebuild and reused by
  the next one; not established whether they belong to the CRT heap or to the D3D11 driver. A run longer than 8 reloads
  would tell whether the last 0-28 MB steps stop. Run: c5h-d, i2-shell-test-bbfacca1 (ff648358...), shell build of the
  team branch at a526e57 (`shell_server.exe` 9482CC69..., `shell_composite.dll` 7A8CA2B1...), D33 + server terrain
  (d33t arm of 3207, `--depth 1 --save-tap`), save C5H-4000-0121-T0359 (= 9038's save, bca895b7...), reloads through
  `tap_live.py reload`; run 1 08:24-08:39, run 2 08:46-09:09 EST, another team's game and builds running (no timing
  taken). Data: `evidence/4000-0121/` (per load, 2-s samples, the server's memory and terrain lines, region breakdowns).
  `shared-texture-and-compositor.md` 13 says "the old field is not drawn until the new load's tap arrives", nothing
  about its memory.

## 5. Live run with AI turns and pans; the 3206 soak's open items

**Own run [L]** (`shell/evidence/3200/w5_soak.py`, their driver, on this run's process): 2026-11-12 23:42:59-23:55:01,
3 AI turns (359 -> 362, done 23:45:11, 4 leader screens answered) then 200 pans (key pans 0.5-2 s and UI.LookAt jumps)
for 10 min: 155,373 frames, **1,474 missed insertions, all `miss_nomatch`, all inside leader screens**
(`turns_explain.py`: 1,383 in the default window, all 1,474 with `--post 3`), **0 missed during the pans**; server
152,162 draws for 152,162 camera blocks, 0 stale, 0 hidden, 0 draw errors, GPU per draw mean of seconds 0.83 ms (max
8.96 ms); no crash, no hang. Game private 1,606 -> 1,700 MB over the three turns. **Claim 5 confirmed by own
measurement** (shorter than 30 min: 12 min, 3 turns).

**The 3206 soak's 1,959.9 ms frame and 14,468 timeouts (auditor observation 4) [their data, own code,
`tools/soak_tail.py`, `soak3206_tail.json`].**
- Frames over 100 ms fall in 62 stats seconds; **58 of them inside the AI-turn phase** (15:27:01-15:31:33), 4 in the
  25.5 minutes of pans. The largest, 1,959.9 ms at 15:31:27, and the next six (1,716-1,817 ms) are all in the turns phase:
  the EXE's main thread blocked by end-of-turn processing [I: the same happens in stock; no stock soak compared]. In this
  verifier's run the same: the largest frame (5,060 ms, 23:43:46) and 26 of 35 such seconds are in the turns.
- **Timeouts** (AcquireSync(0) found the server's texture held: the composite drew the previous server frame again):
  14,468 = 2.7 % of 532,951 frames, in 1,287 of 1,785 seconds (median 3, max 50 per second), almost none in missed
  seconds. In this verifier's run 6,246 = 4.0 % of frames (max 69 per second). With one server draw per camera block the
  server holds the keyed mutex for its copy right when the next game frame wants it, so a few percent of frames show the
  previous server picture (one frame older) [I]. Not an error, but it is a second source of pan lag beside 15.4's, and it
  is what issue 3022 (fence or texture ring) would remove. The research did not explain it; added to
  `shared-texture-and-compositor.md` 14.

## 6. Frame time and memory against stock (claims 6, 7)

**Their data, own code** (`tools/recompute_3207.py`, `recompute_3207.json`): own parser of the composite log by process
section, windows by time, own Welch test. Relaunch series d33t - stock **-1.066 ms, 95 % CI [-1.250, -0.882]** (theirs
-1.061 [-1.246, -0.876]; per-window means differ from theirs by at most 0.014 ms, window edges); with the mean of the
per-second means instead of the frame-weighted mean -1.082 [-1.269, -0.896]; Mann-Whitney p 0.0022 (6 vs 6, complete
separation). In-process d33t - d33pt **+0.668 ms [+0.627, +0.709]** (theirs +0.670). Memory: EXE private -416.55 MB
[-431.6, -401.5], largest free block +394.3 MB [+361.2, +427.5]; server private 1,134-1,154 MB, video 873.1-873.2 MB:
**equal to the tables.** **Confirmed from the raw data.**

Auditor observation 3, the load: stock round 0 (20:43, total CPU 44.3 % against 27-38 % in the other launches) was not
repeated, and d33t round 0 ran 16 min later after a failed launch, so round 0 is not a close pair. The stock arm's frame
time follows the machine load (stock 5.42 / 5.68 / 5.43 ms at 44 / 38 / 33 % CPU, 5.20-5.35 ms at 27-28 %). Without
stock round 0 the difference is -1.05 ms, without rounds 0 and 1 -0.98 ms: the conclusion does not depend on it, the
size does by about 0.1 ms.

**Own series:** see section 11 (taken after the lock queue).

## 7. Pan lag (claim 8)

**Their data, own code** (`tools/pan_stats.py`, `tools/pan_vec.py`; `pan_vec_w8*.json`). The headline (raw age mean 6.10
ms, median 5.66, 2.6-13.1 ms) reproduces. But the team's age is `|median offset| / speed` with the offset's **length**,
and at rest the grid's centroid already sits 6.4-9.2 px (median 7.8) from the highlight's: per hex the rest offsets are
(+1.3, -42.7) px for the city hex (100, 30) (the city model covers its band), (-9.6, +9.8) for (100, 31) and 4-7 px for
the others. So the raw age measures mostly the static error, not the lag. Using the offset **vectors**, with the rest
offset of the same hex subtracted, from the static frame of the same key (static_0 = RIGHT, static_1 = LEFT: the same
screen region as that key's pans, x 330-730 and 1,650-2,380), the component along the pan gives an **effective camera
age of -2.4 ms (RIGHT, sd 1.1, n 6) and +0.65 ms (LEFT, sd 2.2)**; all 12: mean -0.87 ms, 95 % CI [-2.35, +0.60]. The
lag is therefore **at most a few ms in either direction and not resolved by this method** (one game frame at the pan's
82-152 fps is 6.6-12 ms). The LEFT/RIGHT asymmetry of the team's figure (8.3 vs 3.9 ms, "cause open") is the static
offset vector adding to the motion in one direction and subtracting in the other. The negative RIGHT value would mean the
grid leads the EXE's highlight, possible if the EXE's highlight is itself drawn with the previous frame's camera [I,
not traced]. The claim "not above 3000's 10-22 ms" holds a fortiori; the stated 6.1 ms is an upper bound of the
static-plus-lag offset, not the lag.

Also: the method runs in the **stock** configuration (the EXE draws no highlight in D33) at 82-152 fps with
`insert=present` and the server drawing a transparent terrain plus the grid, not the D33 + terrain path at 230-400 fps
with the tagged insertion; and at zoom 18.26, 1,518-1,847 px/s instead of zoom 17, 1,900 px/s (auditor observation 3).
The ms figure does not depend on zoom or speed when the lag is a time [I]; the claim is about the pacing, which is the
same server loop in both configurations [C, shell_server.cpp].

**Own pan run:** section 11.

## 8. Geometry (claim 2: median 0.57-0.97 px; 95 % within 3 px fails at four of six cameras)

**Their tool on their data:** `terrain_compare.geometry_highlight` on W5's stock highlight shots and world images gives
exactly the table of `terrain-surface.md` 5 (six cameras, every figure equal).

**Own metric on their data** (`tools/geo_own.py`, own code: signed world-space distance of each band pixel's drawn
surface point to the nearest highlighted hexagon by the hexagonal norm, **outline scale fixed at 0.9** (the team fits it
per camera between 0.85 and 0.95 to minimise the median it then reports), 36 angle bins per hex, bin median converted to
pixels by projecting a unit step along the bin's normal; bins with fewer than half the median pixel count dropped):

| Camera | Team: median / p95 / within 3 px | Own (scale 0.9): median / p95 / within 3 px | Own signed mean (px / world units) | Own best scale |
|---|---|---|---|---|
| Huge v1 | 0.69 / 2.31 / 97.8 % | 0.72 / 2.20 / 97.7 % | +0.02 / +0.001 | 0.900 |
| Huge v2 | 0.91 / 3.74 / 89.7 % | 0.86 / 3.49 / 91.5 % | +0.09 / +0.008 | 0.900 |
| Huge v3 | 0.81 / 3.29 / 94.1 % | 0.96 / 2.44 / 96.5 % | +0.11 / +0.001 | 0.900 |
| Tiny v1 | 0.57 / 1.98 / 98.8 % | 0.66 / 1.85 / 99.3 % | +0.14 / +0.025 | 0.905 |
| Tiny v2 | 0.97 / 5.11 / 89.8 % | **1.09** / 3.11 / 94.0 % | +0.40 / +0.053 | 0.900 |
| Tiny v3 | 0.84 / 8.22 / 90.0 % | 0.83 / 3.42 / 93.6 % | -0.19 / -0.040 | 0.900 |

So with an independent implementation the picture is the same: the **median is about 1 px** (one camera just over),
**no systematic offset** (signed mean within 0.4 px, 0.05 world units: the drawn heights and the camera are right), and
the 95 %-within-3-px criterion **fails at three of six cameras** (huge v2, Tiny v2, Tiny v3) instead of four; the pass
or fail of the tail depends on the binning, because the tail is the disturbed reference band (W7b's analysis, which this
supports: wider bins average over the tooltip and model cut-outs). The fitted scale does not flatter the median
(the best fixed scale is 0.900-0.905 at every camera).

**Own cameras:** section 11.

## 9. Colour (claim: reported, not judged; D67)

**Their tool on their data:** `terrain_compare.py` with the census plot lists and the stock launch's shots reproduces
the colour table of `terrain-surface.md` 5 exactly for the six ground-truth cameras (load view not rerun)
(`tools/colour_rerun.sh`). **Own simpler mask** (`tools/colour_own.py`, `colour_own.json`: passthrough-clear pixels
eroded 16 px, minus water placeholder and magenta, no plot classes removed): MAD per channel 14.5-30.7 (huge), 34.1-63.8
(Tiny, where unrevealed clouds and forests stay in), within 16 on all channels 17-64 %; the best whole-pixel shift of the
live picture is (0, +1) or (0, +2) px at every camera and lowers the mean MAD by about 1. So the colour numbers depend
mostly on what the mask removes, and the team's mask is the more favourable one; under either, colour fails the brief's
numbers. Consistent with D67 (reported, not judged).
*Asset set (D94, one-off 9049, 2026-11-13):* every colour figure here and in terrain-surface.md 5 is from one launch on
c5h-d, the server showing that EXE's own bake caches: one install, Vibrant Textures hash 45b3b433...; nothing to redo
(asset-pipeline.md 1.7).

## 10. Research quality and corrections made

Every figure checked against raw data matched: the 3207 tables (frame time, memory, server memory, tap release times
273-519 ms), the 3206 soak counts, the tap comparison counts, the six-camera geometry table, the colour table, the cache
counts. The research states its premises and inferences with marks and is mostly careful. Corrected or completed in
place, each with a dated note naming 9038:
1. `shared-texture-and-compositor.md` 11 (P4): outliers of 240-264 u16 under water; `--tol 1` does not separate them.
2. `shared-texture-and-compositor.md` 13: setHeightField 0.9-1.1 s was for 2-plot tiles; live 1-plot tiles 1.8-2.5 s.
3. `shared-texture-and-compositor.md` 14: the soak's 1.96-s frames (turn stalls) and 14,468 timeouts (keyed mutex busy,
   2.7 % of frames) explained.
4. `shared-texture-and-compositor.md` 15.1: stock round 0's load and the size's sensitivity (about 0.1 ms).
5. `shared-texture-and-compositor.md` 15.4 and `research/02-exe-internals/camera-and-picking.md` 10: the pan-lag figure
   is mostly the static offset; along the pan the age is not resolved (within a few ms).
6. `terrain-surface.md` 5: own geometry metric beside the table.
7. `terrain-rendering-inventory.md` 5.4: the bake tap's cost with movement.
Not changed (not research): the merge request's "shell suite 237 passed" (section 2); bug 0108's note that builds do not
start PowerShell (section 2; for the MC, who owns 0108).

## 11. Own frame series, pan run and geometry at own cameras [L]

Two quiet-lock holds: 2026-11-13 00:16:52-00:22:22 (one stock relaunch, then stopped by the verifier's own STOP-GAMES
marker for 9039's priority window, MC 00:19; disturbance: total CPU mean 28.6 %, max 42.6 %) and 01:40:19-01:47:45 (the
stock launch below; mean 26.3 %, max 49.5 %). Free commit above 30 GB throughout.

**Stock launch for geometry and pan lag** (`tools/v9038_stock.py`, pid 7948, in game 01:43:51): stock configuration
(`tap_live.py config --arm stock`), composite `insert=present blend=alpha`, `shell_server.exe --scene terrain
--overlay-grid 1` transparent with the grid (W8's arguments) plus `--save-tap`; the composite in passthrough during the
geometry shots, composite during the pans.

**Geometry at four cameras of the verifier's choice** (`gt-own/plan.json`; plots chosen offline from the team's census
for having no route, improvement, unit, city or feature on the five highlighted plots: hills around (106, 36), flat land
around (94, 30); g1, g2 at the loaded zoom, g3 four wheel ticks in, g4 at g3's zoom; every shot pair at one camera).
terrainshot drew the field the server acknowledged in this process (`acked_l3.c5hf`) at each stock camera
(`tools/geo_own.py`, `geo_own_cameras.json`):

| Camera | Team metric: median / p95 / within 3 px (sections, fitted scale) | Own metric (scale 0.9): median / p95 / within 3 px | Own signed mean px / world |
|---|---|---|---|
| g1 hills, loaded zoom | 0.64 / 1.59 / **99.4 %** (312, 0.9025) | 0.66 / 1.61 / 100 % | +0.23 / +0.046 |
| g2 flat, loaded zoom | 0.59 / 1.94 / **99.3 %** (431, 0.8975) | 0.64 / 1.60 / 99.3 % | -0.05 / -0.020 |
| g3 hills, 4 ticks in | 0.72 / 2.71 / **97.4 %** (304, 0.900) | 0.69 / 2.28 / 99.0 % | +0.33 / +0.037 |
| g4 flat, 4 ticks in | 0.88 / 3.29 / **94.0 %** (397, 0.8975) | 0.97 / 2.66 / 97.0 % | +0.16 / +0.009 |

On clean ground the drawn surface meets the brief's geometry criterion at three of four cameras with the team's metric,
including one zoomed-in view (g3, hills), and at all four with the own metric; the median is 0.59-0.97 px everywhere and
there is no bias (signed means within 0.33 px, 0.05 world units). This supports the team's inferred cause of the tail
at the ground-truth cameras (the reference band disturbed by models, tooltip and river water) [L for the numbers, I for
the attribution of g4's remaining tail, not analysed]. Pictures: `g3_hills_zoomin_stock_hl_512.jpg`,
`g4_flat_zoomin_stock_hl_512.jpg`.

**Pan lag, own run** (12 mid-pan frames, 6 RIGHT + 6 LEFT, shot 0.35 s after the key; `zoomto` reached **zoom 17.000**
this time; screen speed 1,486-1,876 px/s, mean 1,648; `pan_stats_9038.json`, `pan_vec_9038_perdir.json`). The team's
method (offset length / speed): raw age mean **8.64 ms**, median 7.13, 5.0-20.6 ms (the 20.6 ms frame has one hex
found), 95 % CI [5.9, 11.4]; rest offsets 10.7 and 11.3 px; RIGHT 6.9 ms, LEFT 10.4 ms. The vector method (section 7;
10 frames with at least 3 hexes): **mean -1.57 ms, 95 % CI [-3.40, +0.27]**, RIGHT -2.0, LEFT -1.1. Both runs agree: the
lag along the pan is within a few ms of zero, below what the method resolves, and the raw figure is the static offset
(here about 11 px). Claim 8 (not above 10-22 ms) is confirmed by own measurement; the 6.1 ms is not a lag.

**Frame series: not completed [L, negative].** Of the planned 3 + 3 relaunches (stock vs D33 + terrain at plot (94, 30)
of the turn-359 save, the team's `w5_series_3207.py relaunch` with the verifier's build) only stock round 0 ran
(00:16:52-00:22, under the lock): **5.141 ms** frame time (60-s window, total CPU 33.1 %), EXE private 2,044.9 MB, largest
free block 1,171 MB: inside the team's stock range (5.20-5.68 ms, 2,026-2,067 MB, 1,201 MB mean). The second hold
(queued 01:47, its turn 02:29) waited the tool's 20 minutes for other owners' heavy jobs and was refused at 02:49:10
(a game of 9043 on c5h-b since 02:01, and three jobs of 4000 that started at 02:45:50, 02:48:55 and 02:49:07, i.e. while
the take was already waiting); the verifier did not take with `--overlap`, which would have recorded a disturbed hold.
So the frame-time claim rests on the recomputation from the team's raw data (section 6). One indicative D33 + terrain
figure, not under the lock and not comparable (load view, machine busy): 5.06 ms over 19 s at the load view.
Memory, own samples one each [L]: stock 2,044.9 MB private / 1,171 MB largest free (this hold); D33 + terrain 1,586.4 MB /
1,642 MB 20 s after load (functional run): -458 MB and +471 MB, the same direction and size as the team's -417 / +394 MB.

**Tooling observation (quiet lock):** while a `take` waits up to 20 minutes for running heavy jobs of other owners, the
gates still let other owners start new heavy jobs (the lock is not yet held), so a waiting take can be starved and then
refused. A take in its waiting phase could block new starts of other owners (perf team's call).

## 12. Defects, not verified

**Defects** (for the MC to file; severity as the verifier judges it):
- **V1 (low)** `shell/tests/test_bake_tap.py::test_real_path_refuses_an_exe_without_the_patterns` waits 10 s for a log
  line that comes only after the stand-in's hook commit, which took 6.5 s under load: a flaky test (failed once in the
  full suite, passed 3 of 3 alone). Fix: wait for `hooks committed` first, or a longer timeout.
  *Bug 0120, 2026-11-13:* a longer wait alone does not help, because the refusal is logged from `BakeTap_OnPresent` and
  `test_host` counts the commit (inside LoadLibrary) toward `--seconds`, so a 6-s host can quit before the next Present
  [C, code]. The four host-only tests of the file now run a 20-s host and wait for `hooks committed (` first;
  2026-11-13 07:35-07:48 EST ten passes in a row beside other teams' shell build, residency drive and shell suite: 10 of 10 [L].
- **V2 (medium; measured by bug 0121, severity for the bugfix lead to set)** the server's private bytes over reloads in one process: up
  +333 to +432 MB over the first 2-4 reloads, then 0-28 MB per reload, near 1.55-1.6 GB after 4-8 reloads; video memory
  flat; rebuild peaks up to 2.35 GB (section 4, 2026-11-13). Not a field kept per reload; the remaining small steps and
  the rebuild peak matter for map changes in the real server. (Was: "1,207 -> 1,496 MB (+289 MB, kept), 2,143 MB peak;
  to confirm with several reloads".) *(2026-11-15, bug 0121: on the pinned release layout the step does not reproduce
  and the server is flat after the first reload; see `research/04-memory-investigation/server-reload-memory.md`.)*
- **V3 (medium, research)** P4 stated too small (outliers of 264 u16, not 46); the tol-1 advice is wrong. Corrected in
  place.
- **V4 (low, research)** the pan-lag figure (6.1 ms) is mostly the static grid-vs-highlight offset; the lag along the pan
  is not resolved by the method. Reanalysed in place; a better method would measure the offset change against a rest
  frame at the same screen position, or the composite's own `age_mean` plus the camera stamp.
- **V5 (low, process)** the merge request's "shell suite 237 passed" was not a run of the merged head (293 tests).
- **V6 (low, tooling, for 0108)** every build through `vcvarsall.bat` starts a background `powershell.exe` (VS
  telemetry) unless `VSCMD_SKIP_SENDTELEMETRY=1`. *(Resolved 2026-11-14: every wrapper sets it now, section 2 and
  issue 0147; replaced "render/build.py, shell/build.cmd and the harness native build do not set it".)*

**Not verified:** an own grade-A (or B) frame-time series stock vs D33 + terrain (one stock launch only; the second hold
was refused by the lock tool after 20 min, section 11); `test_errors.py` (56 tests; bug 0108); the shell ASan suite at the merged head; the Tiny save (no live
run on Tiny here); "no tile bake in play under D33" (W3's camera-move test, not repeated); the in-process D33 passthrough
vs D33 + terrain series (only recomputed); a full 30-minute soak with 10 AI turns (12 minutes with 3 turns here);
the colour criterion (D67, reported only); the load view's colour; whether the 0.4-0.5 ms keyed-mutex cost changes with
the new pins.

## 13. The grey end frame of the huge product-path run is a PrintWindow artefact, not a presented frame (ticket 0401, 2026-11-16)

**Question.** 0383's live run (c5h-f, 2026-11-16 03:01-03:10 EST, huge save, D33 product path) ended with a harness
shot that was one colour, 84,84,84 (`F:/c5-64bit-work/0383/pprun-huge/out/end.png`, Visual Checks
`vc-20261116-030942-workstation-f1af`), about 10 s after the server's `residency: tile styles` line. Gray frames are a
fault the owner named (D85). Does the player see it?

**Answer: no.** The frame the EXE presents is never grey at that moment; the harness's PrintWindow capture is.
- **0383's own logs [L, reread 2026-11-16]:** in the second of its shot (03:09:42) the composite's stats line says
  `frames=30 comp_n=30 ins_checked=30 ins_wrong=0 nodraw=0 cam=OK` and its lockstep line `exact=30 gray_frames=0`: 30
  frames presented, each composited with the server's exact frame, no unbaked tile. The same for every second to the
  stop (03:10:07).
- **Reproduction, 3 runs [L, 2026-11-16 03:33-03:53 EST]:** same instance (c5h-f, Goldberg, F:), same save bytes
  (`C5H-VP8P-HUGE_0350`, c499d3c6..., copied as `C5H-0401P-HUGE_0350`), 0383's recipe and timing
  (`render/evidence/0401/run_0401.py` over `shell/evidence/3308/run_3308.py`: D33 with Overlay kept on, ServerRenderer =
  1, the composite with its manifest, 0 AI turns, 15 s settle, the tile-style line waited for, 10 s, 0383's read-only
  styles step), builds of main f737d216 made by 0383's recipe (`render/evidence/0401/build_pp_0401.sh`: server.exe
  98d2800e..., proxy c579ab63..., x64 DLL 74588135... = the pin's release DLL, composite af1d1293...; 0383 ran server.exe
  19a67f0d... and proxy 7a15f015... of its branch, the same renderer code), client 2554 x 1411. At 0383's shot moment
  each run took 10 pairs, about 8-12 s apart: a harness shot (`cli.py shot --flat-retakes 0`, PrintWindow with
  `PW_RENDERFULLCONTENT`) and right after it the composite's back-buffer dump (`shell_composite.shot` trigger; the BMP is
  written at the next Present, 0.07-0.58 s after the request, i.e. the frame as presented, after composition, with the
  EXE's UI).

  | run | PrintWindow flat 84,84,84 | PrintWindow without the EXE's UI (server world only) | back buffer flat | back buffer without UI | 0383-style end shot |
  |---|---|---|---|---|---|
  | run1 (03:33-03:39) | 4 of 10 (pairs 4, 5, 6, 9) | 2 (pairs 1, 7) | 0 of 10 | 0 | normal (112,119,114) |
  | run2 (03:39-03:46) | 3 of 10 (2, 3, 6) | 0 | 0 of 10 | 0 | normal |
  | run3 (03:46-03:53) | 1 of 10 (0) | 0 | 0 of 10 | 0 | normal |
  | total | **8 of 30** | 2 of 30 | **0 of 30** | 0 | 0 of 3 grey |

  Every back-buffer dump is the full game picture with the interface (mean 114.3-114.7, stddev 55.9-56.2 over all 30:
  the same still frame each time); the 20 normal PrintWindow captures match their pair within 4 levels in the top band,
  the flat ones differ by about 80. In every second of the three capture windows (77, 79 and 105 stats seconds) the
  composite logged frames = comp_n (28-31 a second at the 30 fps cap), `ins_wrong=0`, `nodraw=0`, `cam=OK`, and lockstep
  `none=0`, `gray_frames=0` (`exact` 26-31, the rest one server frame older); 0 LOUD lines in server.log and render.log. So 0383's grey end shot reproduces as a capture result (27 % of PrintWindow calls at this
  camera), never as a presented frame. Contact sheets (DIAGNOSTIC; PrintWindow left, back buffer right): Visual Checks
  `vc-20261116-033848-workstation-55d4` (run1), `-034532-workstation-0f13` (run2), `-035232-workstation-520d` (run3);
  the end shots `-033853-...-a85c`, `-034537-...-bbc2`, `-035238-...-56aa`. Raw pairs and per-pair statistics:
  `F:/c5-64bit-work/0401/run{1,2,3}/caps/`, `caps.json`.
- **Side observation [L], not this ticket's grey frame:** each run's composite counted frames with an unbaked (gray)
  land tile (`renderer-in-server-design.md` 4.17) only in two seconds at the start of the world's drawing: run1
  03:36:48-49 (`gray_frames` 4 and 12, `gray_tiles` 278 and 396), 6 s before the harness's "in game"; run2 03:43:41-42
  (5 and 3; 278 and 38), 5 s after it; run3 03:50:13-14 (2 and 8; 142 and 237), 4 s after it; none later. These are the
  first frames before the bake-ahead's first visible pass (`terrain-bake-residency.md` 6.4 saw the same 4 frames at
  start). Whether they reach the player's screen (the load screen's "continue" button may still cover the world then;
  the harness's "in game" is the game state, not the screen) was not examined: a question for the gray-tile work (D85).
  *Answered 2026-11-16 by ticket 0404, section 14: yes, and before them the player sees a map with no terrain at all
  for 5-12 s.*

**Why PrintWindow sees it [I].** The two wrong kinds of capture are the two intermediate states of the EXE's back buffer
within a frame: just after the scene clear (all the D33 clear colour 84,84,84; under D33 the EXE's terrain is not drawn)
and after the composite inserted the server's world but before the EXE's models and interface. That suggests
`PrintWindow(PW_RENDERFULLCONTENT)` on this window reads the swap chain's buffer while the frame is being drawn rather
than the last presented one; the dump, taken inside Present, cannot. Not established: the swap effect, and why the
share varies so much between runs (8 % to 40 % per run; flat captures came in runs, e.g. run1 pairs 4-6, 20 s). A flat
capture ran the shot command in about 1 s against about 3 s for a normal one: the PNG of one colour is quick to write,
not a different capture path [I].

**Fix (harness, 2026-11-16).** `tools/harness/shot.py` `shot()` retakes a capture of one colour that is not black (black
is reported as before: no picture yet) up to `FLAT_RETAKES` = 4 times, 1 s apart; the result carries `"flat"` (the kept
picture is still one colour) and `"flat_retakes"` (captures dropped); `cli.py shot --flat-retakes N` (0 = the raw
capture); turn-wait progress shots are not retaken. Tests: `tools/harness/tests/test_shot_flat.py` (9 offline tests,
including a retake that times out and the CLI's exit 3 on a failed capture). Live check, run fix1 (2026-11-16
03:55-04:04 EST, same setup, the series with the retake on): 0 of 10 captures and the end shot flat, but none needed a
retake (`flat_retakes` 0 each; the shot took 3.7-6.4 s against 2.6-3.9 s before, under other teams' load), so the retake
itself ran only offline (sheet `vc-20261116-040304-workstation-8a51`). UI-less captures are **not** caught by this
(no reference in the harness); pictures that must show what the player sees come from the composite's back-buffer dump
(rule kept in `06-tooling-and-workflows/run-and-drive-the-game.md`, "PrintWindow on the product path").

**Not verified:** a screen capture of a visible window (all runs on the hidden desktop); the Tiny save (0383's Tiny end
shot was normal; 3702 saw 1 flat of 13 on Tiny); the stock route's share at this camera.

## 14. What the player sees after the load screen on the huge product path: no terrain for 6-12 s, then unbaked tiles (ticket 0404, 2026-11-16)

**Question (D85).** 0401 (section 13, side observation) counted 8-16 composited frames with unbaked land tiles in the
first seconds of each huge load. Does the player see them, past the load screen?

**Answer: yes, and worse: before them the player sees a map with no terrain at all.** When the player closes the load
screen at the first moment its Continue button shows, every presented frame for the next 6.2-11.6 s shows the D33 clear
colour 84,84,84 where the terrain should be, with the EXE's units, cities, improvements and interface drawn over it
(the server has no field yet: lockstep kind `none`). Then the server's first frames come with 58-74 visible land tiles
unbaked, drawn flat in the material fallback colour (`render_fallback = material`: cream and purple, not gray), for
5-19 frames that stay on screen 1.2-15.8 s, because one of them is frozen by an EXE stall (cause 3). The load screen
itself is opaque: no world shows through it in any dump (contrast `proxy-route-compositor.md` 4.5's translucent
screenshot), so nothing drawn before the click is seen.

**Setup [L, 2026-11-16 04:49-05:33 EST].** Workstation (loaded: 9-11 other heavy jobs, several game instances; a
functional run, no quiet lock: every duration below is an indication, not a timing result, COMMON 23), instance c5h-g
(Goldberg, F:), save `C5H-VP8P-HUGE_0350` (c499d3c6...) copied as `C5H-0404P-HUGE_0350`, turn 350, the D33 product path
as 0401 (ServerRenderer = 1, the composite with its manifest, Overlay kept on, frame cap 30), client 2554 x 1411, Vibrant
Textures present. Builds of main e76257f7 by 0401's recipe (`render/evidence/0404/build_pp_0404.sh`): server.exe
9a65b2e4..., proxy b3381fff..., x64 DLL 74588135... (the pin's release DLL), composite 917b8f70.... Driver
`render/evidence/0404/run_0404.py` (over `shell/evidence/3308/run_3308.py`'s prep and restore): launch, then the load
screen probed through the Lua channel (`frontend.probe_in_game`, read only); the moment it says `load_screen_ready` the
screen is closed through the button's own handler (`frontend.dismiss_load_screen`), the earliest click a player can make
(the call returned 0.5-3.5 s after the probe saw the button: the Lua channel answers once per EXE frame at 3-10 fps).
From the server's `terrain: state HIDDEN -> NO_FIELD` line the composite's back-buffer dumps (`shell_composite.shot`: the
presented frame after composition, with the EXE's UI; not PrintWindow, section 13) were taken one after another (runs
1-2) or one every 2 s (runs 3-4; each dump stalls one Present) until 12 s after the click. The per-frame record is the
composite's lockstep CSV (`present_qpc`, `kind`, `unbaked`; `renderer-in-server-design.md` 4.17); `perf_counter` is
QPC / 1e7 on this machine, so the driver's marks and the CSV share one clock. Analysis and contact sheets:
`run_0404.py analyse`; table: `summary_0404.py`; raw data `F:/c5-64bit-work/0404/run{1..4}/` (dumps, out/,
analysis.json). Restored by hash after each prep (backup equal, `set-proxy --original` 0, provision verify 0, save copy
removed).

Seconds relative to the click (the dismiss call's return; the click itself lies up to 0.5-3.5 s earlier, see above):

| run | dumps | EXE fps after the click | world view starts (`NO_FIELD`) | tap copy starts | field built | first server frame | first frame without an unbaked tile | no terrain on screen | unbaked frames (on screen) | max unbaked tiles |
|---|---|---|---|---|---|---|---|---|---|---|
| run1 | 20, dense | 2.8 | -13.8 | +6.7 | +11.4 | +11.7 | +27.4 | **11.6 s** | 19 (15.8 s; frame 95 alone 13.6 s) | 74 |
| run2 | 18, dense | 4.6 | -12.4 | +5.6 | +10.6 | +11.4 | +12.6 | **11.4 s** | 8 (1.2 s) | 74 |
| run3 | 9, every 2 s | 6.9 | -9.5 | +3.1 | +6.9 | +7.1 | +11.0 | **7.1 s** | 6 (3.8 s; frame 119 alone 3.2 s) | 74 |
| run4 | 9, every 2 s | 9.5 | -8.0 | +2.3 | +5.8 | +6.3 | +8.7 | **6.2 s** | 5 (2.5 s; frame 123 alone 1.9 s) | 58 |

0401's four loads (no dumps in this phase; the load screen closed by `ensure_in_game` after `wait_game`; 16-18 EXE fps;
`render/evidence/0404/analyse_0401.py` over `F:/c5-64bit-work/0401`) show the same order: the first server frame 4.8-7.1 s
after the dismissal window in 3 of 4 loads, with 8-10 unbaked frames over 0.5-2.5 s; in 0401's run1 the world view's
scene-clear frames ran under the load screen and the server's first frames came 3-6 s *before* the dismissal, hidden by
the opaque screen [L counts; why that load drew its scene under the screen is not established]. Visual Checks (all
DIAGNOSTIC, info): the four runs' contact sheets and end shots, `visualcheck.py list --issue 0404`; the full-size dump of
an unbaked frame is `F:/c5-64bit-work/0404/run2/dumps/dump_018_backbuffer_050226_18.png` (frame 100, 68 unbaked tiles:
the land around the capital flat cream, the land to the south flat purple).

**Causes, in order [L for the log lines and counts, C for the code, I where marked].**
1. **No terrain for 6-12 s: the field comes only after the click.** Under D33 the EXE draws no terrain (the product rule
   clears Terrain at load, `proxy-route-compositor.md` 4.3), so until the server has a field the map area is the scene
   clear colour. The server's field comes from the height-field tap, which starts only after the camera has been OK for
   `tap_delay_frames` = 30 Presents **and** `tap_scene_frames` = 30 frames with the scene-clear list since the grid
   changed (`shell_composite.cpp` tap gate; `shared-texture-and-compositor.md` "The scene-clear gate") [C]. Under the load
   screen the EXE presents 0.5-2.5 frames a second, nearly all without the scene clear (`noins` in the CSV), so the 30
   scene frames come after the click: the copy started 2.3-6.7 s after it. Then the copy and the server's read took
   1.1-1.5 s and the field build 2.4-3.5 s (`terrain: field ... in 2585-3455 ms`). The scene-clear gate exists for a reload
   in the same process, where the camera stays OK through the load screen (shared-texture-and-compositor.md 9.4); on a
   first load the camera is OK 8-14 s before the click (the `NO_FIELD` line). How long the empty map lasts depends on
   the frame rate after the click: on this loaded workstation 2.8-9.5 fps (6.2-11.6 s); at 0401's 16-18 fps 4.8-7.1 s; a
   quiet player machine was not measured. (Fixed 2026-11-16 by ticket 0408: on a first load the tap and the water tap
   start once the camera comes up on the new grid, under the load screen; section 14.1.)
2. **The unbaked frames: the residency's per-frame budget at the first draw.** At the field nothing is resident; the
   first frame sees 58-74 visible land tiles and the residency bakes at most `burst` = 16 visible tiles per frame (the
   EXE's settled in-play budget, `render/include/c5r/bake/residency.h`) [C], so the visible set needs 5-8 frames; the
   unbaked count falls 74, 72, 68, 43, 16, 9, 1, 1 (run2) [L]. The load's decal wait (0365, `terrain-decals.md` 19.3) is
   not a cause: it holds only the bake-ahead, not the visible tiles ("the visible tiles are baked meanwhile" in every
   run; run2's and run3's sets arrived before the field) [L]. (Fixed 2026-11-16 by ticket 0409: the residency bakes the
   whole visible set and the view's edge at such a draw, `terrain-bake-residency-first-draw.md`.)
3. **Why the unbaked frames stay on screen for seconds: an EXE stall right after the server's first frame.** In every run
   one unbaked frame was the last Present for 0.47-13.6 s, and each stall spans the composite's fow-models first-mask
   set-up: `fow_models: section ... open` to `fow_models: the resample pass is ready` took 13.7 s (run1), 0.67 s (run2),
   3.75 s (run3), 2.5 s (run4), against frozen frames of 13.6, 0.47, 3.2 and 1.9 s [L]. That set-up runs on the EXE's
   render thread and loads `d3dcompiler_47.dll` and compiles the resample pass with `D3DCompile` (`fow_models.cpp`
   `RsInit`) [C]; which part takes the seconds on a loaded machine is not measured [I: the compile or the library load].
   In run1 a back-buffer dump was also requested in that frame.
   **Corrected 2026-11-16 (ticket 0407):** the compile is not the stall (the old library load plus compile cost 10-21 ms; the programs are now built in and the set-up takes 0.3-0.6 ms). In all 8 stalled loads the server's first GPU work after the field waits 8.7-23.4 s inside the NVIDIA driver while the board already held at least 9.9 GB of its 16 GB (4 of 7 such loads, 0 of 9 below, Fisher p 0.019): video memory paging on a board shared by 10+ test clients; the EXE waits on its server in steps of 3.0 s. See [fow-models-compile.md](fow-models-compile.md). (Replaced: "That set-up ... loads d3dcompiler_47.dll and compiles ... which part takes the seconds is not measured".)

**A crash on the way [L].** The fifth load (run5nd, the control arm without dumps, started 05:16:51) ended in the load:
the EXE exited with 0xC0000005 at 0x00F1C6DA (Goldberg's `run/gbe/crashes.txt`, 10:20:16 UTC) with the EXE image at
0x00BA0000 (the composite's camera line, delta 0x7A0000), i.e. **EXE VA 0x77C6DA**, about 9 s after the grid became valid
(`camera: grid constants read (grid seq 3): valid 1` at 05:20:07); no box, so the dialog watcher saw nothing
(`dialog_watch_end ... process ended`) and the driver probed a dead channel to its 900 s deadline (fixed since: it checks
the EXE's pid). The server logged `the engine is gone: ... PeerDied`. Not seen in 0401's 4 or this ticket's 4 other loads
of the same save and build; the address is in no research file. The control arm was not repeated; 0401's loads serve as
the arm without dumps.

**Fix directions (proposed to the MC as tickets; not made here: 0404 is investigation only).**
- Have the field before the click: on a first load start the tap once the grid is valid and the load has finished (the
  load screen's button shown, or the first `ICvGame1::Update`, which comes under the load screen, `proxy-route-
  compositor.md` 4.3), not after 30 scene-clear frames; keep the scene-clear gate for reloads in one process. That moves
  the 3.5-5 s of copy and field build under the load screen.
- Do not let the player past the load screen before the server's first complete frame: keep the Continue button hidden
  (or the screen up) until the server reports a frame with every visible land tile baked. With the first item this
  costs the player little; without it this alone removes both the empty map and the unbaked frames.
- At the first draw of a load (nothing resident) bake the whole visible set before the frame is published (one time,
  58-74 bakes; the bake-ahead's 1.5-9.0 s of baker GPU time for about 5,000 bakes is 0.3-1.8 ms a bake on this machine)
  instead of 16 a frame.
- Move the fow-models resample pass's compile off the render thread (at the composite's start, or precompiled
  bytecode), so the first fog mask does not freeze the EXE.
- File the EXE crash at VA 0x77C6DA during a huge product-path load (no box) as a bug.

**Not verified:** a quiet machine's frame rate after the click (the empty map's length scales with it); the stock
route's picture at the same moment (stock bakes during the load: `02-exe-internals/load-sequence.md`, "terrain bake
7.7"); a later click (the scene-clear gate counts only frames after the click, so waiting on the load screen should not
shorten the empty map [I from the gate's code and 6 of 7 loads; 0401's run1 is the exception]); the Tiny save; which
step of the fow-models set-up stalls.

### 14.1 The tap's first-load gate: the field under the load screen (ticket 0408, 2026-11-16)

**The rule [C, `shell/composite/shell_composite.cpp` "The load gate", `TapGateUpdate`].** Cause 1 above: the tap waited
for 30 scene-clear frames, which the load screen does not have, so the field came only after the click. The scene-clear
gate exists for a reload in the same process, where the camera block stays OK through the load screen
(`shared-texture-and-compositor.md` 9.4). The composite now tells the two cases apart per grid seq:
- **first load**: the camera was never OK on a valid grid earlier in this process, and it was not OK at the first
  Present that saw the new grid valid (the front end has views 0, INCONSISTENT); it comes up later, when the map view is
  set up. Then the copy starts after `tap_first_delay_frames` (3) Presents with the camera OK, with no scene-clear frames.
- **reload rule** (as before 0408): a map was shown earlier in this process, or the camera was already OK when the grid
  became valid (a stale view). The copy waits for `tap_delay_frames` (30) Presents with the camera OK and
  `tap_scene_frames` (30) scene-clear frames.
`tap_first_load=0` gives the reload rule on every load. The water tap follows the same decision
(`WaterTapEnv::firstLoadOpen`, MC #2448: "The player must not see land without its water either"). One log line per
grid seq names the rule (`tap: load gate (grid seq N): FIRST LOAD ...` or `RELOAD rule - <why>`), and the copy's start
line names its gate.

**Why the camera is a safe signal on a first load [C for the order, L for the times].** The field is complete from the
return of CreateTerrain (load stage 2, `asset-pipeline.md` 4.3); gridValid (the ready byte) is set at its start, by
GridHeightmap::Init. The camera block is OK only with the map view set up (GVS present, view mode 1, `g_NumViews` >= 1),
which on a first load happens at StartGPUInitWork (stage 8, which calls UpdateViews; `02-exe-internals/load-sequence.md`
3) [I for "stage 8": the composite sees only the transition]. Live on the huge save the grid became valid 37.0-43.1 s
before the camera came up in this ticket's five loads (49.1 s in 0404's run2), and the camera came up 7.9-9.1 s before the
dismissal. The composite cannot see the load screen's button or the DLL's first `ICvGame1::Update` (only the proxy sees
Update; `proxy-route-compositor.md` 4.3): the camera coming up on the new grid is the signal it has, and after it the load
runs only its last steps (to IAmReady and WaitForNetPlayers, `load-sequence.md` 1) and the wait for the click (in these loads 6.2-8.6 s between the camera and the probe that saw the button).

**Stand-in tests [C] (`shell/tests/test_composite_tap_gate.py`, 9 tests, 2026-11-16 08:14-08:48 EST; build of this
branch).** The composite test key `camera_test=late` (`camera_test_late=N`) reports the camera INCONSISTENT for the
first N reads after each new valid grid, which is what a first load looks like (test_host's own camera is OK at the
first read). First load on the plain host (no scene-clear list, like the load screen): copy after exactly 3 OK Presents
with 0 scene-clear frames, never before the camera's OK line, READY and RELEASED; `tap_first_delay_frames=40`: exactly
40. Negative: the stand-in's camera OK with the grid -> reload rule, no copy; `tap_first_load=0` -> reload rule, no copy;
a reload (`tap_test_load`) whose camera again comes up after the grid -> "a map was shown before", no copy 2 without
scene frames; on `--game` the reload's copy 2 comes with >= 30 OK Presents and >= 30 scene-clear frames. Water: the
first-load copy on the plain host with `water_delay_frames=60 water_scene_frames=30` (0 scene frames), none after a reload,
none with the camera OK with the grid, and the decision also with `tap=0`. With them, `test_tap.py` and
`test_camera.py`: 78 of 80 at the first run on the loaded workstation; the two failures passed alone (a host that had
not ended within 40 s with a water copy still unacknowledged when the host's time ran out: the test now lets it time out
first; `test_grid_drawn_with_fake_camera`: no OK stats line within 5 s under load) [L]. Under MSVC `/fsanitize=address` (`shell/build.cmd asan`, 09:12-09:38 EST): the gate tests and `test_tap.py` 63 of 64, 0 ASan reports; the one failure (`test_tap_new_load_supersedes_and_request_recopies`: copy 3 acknowledged but the host's 20 s ran out under ASan before RELEASED was published) passed alone.

**Live (2026-11-16 08:43-09:10 EST) [L].** Workstation (loaded: 9 other heavy jobs registered at 08:06; a functional run, no quiet lock: the
durations are indications, COMMON 23), instance c5h-i (Goldberg, F:), save `C5H-VP8P-HUGE_0350` (c499d3c6...) copied as
`C5H-0408P-HUGE_0350` and removed after, 0404's driver (copied as `shell/evidence/0408/run_0408.py`, `live_0408.sh`:
the click at the first moment the Continue button shows, back-buffer dumps every 2 s), the D33 product path as 0404.
Builds of this branch at dc4d1293 (main 9669bf74 plus 0408): server.exe 4ff7ba40..., proxy 99575a18... (build_server.py
release), x64 DLL 74588135... (the pin's release DLL), composite 1040bb46.... Three loads with the shipped rule
(run1-3) and two with `tap_first_load=0` (before1-2: the gate before 0408 on the same build, which has 0409's
first-draw bake). Restored by hash after the series (backup equal, provision verify 0, save copy removed). Raw data
`F:/c5-64bit-work/0408/<run>/`; table `shell/evidence/0408/summary_0408.py` (output `summary_0408.txt`, with 0404's
four loads read from `F:/c5-64bit-work/0404`).

Seconds relative to the dismissal (the dismiss call's return). "Map frame" = the first EXE frame with the insertion after
the button showed (the load screen's frames have no scene-clear list); in every 0408 dump that frame still shows the
load screen, so "map without terrain" is an upper bound of what the player sees. Times from the logs carry about 0.1 s
(the dumps' log lines, which set the clock, come after the BMP is written); the frame times are exact (QPC).

| load | rule | camera OK | tap copy | water copy | field built | water draws | map frame | first server frame (terrain and water) | map without terrain | frames | unbaked frames |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0404 run1 | before 0408 | -13.9 | +6.6 | +6.6 | +11.3 | +11.4 | -0.6 | **+11.7** | 12.3 s | 52 | 19 |
| 0404 run2 | before 0408 | -12.5 | +5.4 | +5.4 | +10.5 | +11.2 | -1.7 | **+11.4** | 13.1 s | 55 | 8 |
| 0404 run3 | before 0408 | -9.7 | +3.0 | +3.0 | +6.7 | +6.9 | -0.3 | **+7.1** | 7.5 s | 73 | 6 |
| 0404 run4 | before 0408 | -8.1 | +2.2 | +2.2 | +5.7 | +5.8 | -0.7 | **+6.3** | 7.0 s | 77 | 5 |
| 0408 before1 | tap_first_load=0 | -7.9 | +2.3 | +2.3 | +5.5 | +6.1 | -0.3 | **+6.3** | 6.7 s | 85 | 0 |
| 0408 before2 | tap_first_load=0 | -8.5 | +2.4 | +2.4 | +5.8 | +6.4 | -0.3 | **+6.5** | 6.8 s | 85 | 0 |
| 0408 run1 | first load | -8.6 | -8.3 | -8.3 | -4.8 | +0.0 | -0.4 | **+0.55** | 0.95 s | 6 | 0 |
| 0408 run2 | first load | -9.1 | -9.0 | -9.0 | -5.3 | +0.2 | -0.3 | **+0.54** | 0.80 s | 4 | 0 |
| 0408 run3 | first load | -8.2 | -8.1 | -8.1 | -4.8 | +0.2 | -0.3 | **+0.60** | 0.87 s | 4 | 0 |

So on a first load the copy starts 0.1-0.3 s after the camera comes up (3 Presents), the field is built 4.8-5.3 s before
the dismissal, and the first frame with the server's terrain and water comes 0.54-0.60 s after the dismissal call
returned, against 6.3-6.5 s with the old gate on the same build and 6.3-11.7 s in 0404. The water module draws in the
same first server frame as the terrain in every load (rivers and waves from the water tap, the ocean and coast from the
field): no frame shows land without its water. 0 unbaked frames in all five loads of this build (0409's first-draw bake).

**What remains: 4-6 frames without terrain over at most 0.8-0.95 s [L].** The field is there, but the server's first
frame waits for the first map frames: (a) the fog pass holds frames until the plot table arrives from the feed (`fog:
frames held from frame 42: no plot table from the feed`; the table came 0.3-1.6 s before the dismissal returned, at the
first feed batch after the button; installed at the 4th map frame, frame 46, with the overlay and shadow taps' first
pair, which they copy only from map frames, Present 44); (b) the first draw bakes the 75 visible land tiles and 2 at the
edge before it publishes (255.5-260.6 ms; 497-503 ms in the before arms, where it ran with the bake-ahead's start). With
the old gate the table had come 7 s earlier and the first frame waited only for the field. How many of the 4-6 frames
the player sees is not measured: the first of them still shows the load screen in every dump; the others were not dumped.

**Not verified:** a reload in the same process on the product path (the reload rule is tested on the stand-in only; a
live reload keeps 0404's behaviour by construction); a quiet machine; other map sizes; whether the 4-6 frames of the
first draw are visible.

### 14.2 The frames between the click and the server's first frame: what the first frame waits for (ticket 0424, 2026-11-18)

**Correction to 14.1's "What remains" (2026-11-18, ticket 0424).** 14.1 named two causes: the fog's plot table and the
first-draw bake. Re-read from 0408's raw data (`F:/c5-64bit-work/0408/run{1,2,3}`, the lockstep CSVs and `render.log`;
table by `server/evidence/0424/summary_0424.py`), the order is different, and the plot table is not the binding wait:

| 0408 load | first map frame (CSV frame, s after the dismissal) | its camera published before its Present | fog's plot table | first overlay pair | fog installed (camera stamp) | first-draw bake | first server frame (CSV frame) | map frames without terrain |
|---|---|---|---|---|---|---|---|---|
| run1 | 43, -0.398 | 1.157 s | -0.338 | -0.287 | -0.256 (46) | 260.6 ms | 49, +0.553 | 6 |
| run2 | 43, -0.264 | 1.252 s | -1.576 | -0.153 | -0.116 (46) | 255.5 ms | 47, +0.535 | 4 |
| run3 | 43, -0.270 | 1.179 s | -1.501 | -0.142 | -0.109 (46) | 255.7 ms | 47, +0.596 | 4 |

[L, 0408's loads of 2026-11-16 08:43-09:10 EST, read 2026-11-18; log times carry about 0.1 s, frame times are exact]

1. **The camera moves at the click** [L, all five 0408 loads and 0404's]: under the load screen the camera block shows the
   view (2431.88, -1243.49, 424.26); the first map frame (CSV frame 43 in every load) shows (2975.86, -1183.05, 424.26).
   A bake "at the field, before the click" of the load screen's view bakes tiles the player does not see first: the
   75 visible land tiles of the first drawn frame had no bake in every load although the bake-ahead had baked 539 or more
   tiles by then (run1's `terrain 1s` line at 08:47:51.474).
2. **The in-game camera is known 1.16-1.25 s before the first map frame is presented** [L]: the EXE's first frame after the
   click is slow (its camera block is published at the click, its Present comes 1.16-1.25 s later). The server could draw
   in that window, but the fog holds every frame until it has a plot table and an overlay pair.
3. **The first overlay pair is the copy of the first map frame's own Present** (`overlay: first pair bound: ... copy of
   Present 44`, = CSV frame 43) [L]: it is bound 0.11-0.13 s after that Present. The load screen's frames have no
   insertion, so the overlay tap copies nothing before the click. The plot table came 1.2 s before the first map frame
   in run2 and run3 (with the camera's move: the feed's first `ICvGame1::Update` after the click) and 0.06 s after it in
   run1; in all three loads the pair, not the table, is the last input the fog waits for. (Replaced: 14.1's "the fog pass
   holds frames until the plot table arrives from the feed".)
4. **Then the first draw bakes the visible set** (255.5-260.6 ms, 0409's catch-up) before it publishes, so the first
   server frame reaches the composite 2-3 EXE frames after the install.
5. **Tile styles (since 4555):** the bake's tile style comes from the plot table's continent art types. Before the table
   every tile is Generic; tiles baked then are re-baked when the table restyles them (`onFeedGeneration`). A bake before
   the table is therefore not the player's picture.

**What 0424 changes (R1, MC #4982):** while the fog holds a frame that has a field and an OK camera, the residency bakes
that camera's visible set (0409's catch-up, the caches handed to the renderer), so the frame drawn after the install
finds it baked. Details and the live result: 14.3.

**What would reach 0 frames (R2 + R3, ticket 0529; R2 built 2026-11-24, section 14.4; R3 not built, a question to the owner):** the first map frame can carry the server's terrain only if
the fog can draw it before its Present, that is without that frame's overlay copy and with a plot table at the click:
- **R2, the plot table under the load screen.** The feed takes its first snapshot at the end of the DLL's first
  `ICvGame1::Update` (`server/src/remote/srv_remote_feed.cpp` `AtLeaveUpdateBody`, called from `srv_rt.cpp` LeaveUpdate),
  which comes only after the click. A snapshot at the load's end, under the load screen (for example at the last
  `ICvPlayer1::SetupGraphical` of the load, `SrvRemoteFeed::OnSetupGraphical`, or another EXE->DLL call of the load's
  last stages, on the DLL's thread with the game-core lock held), would give the fog its table and the bake its tile
  styles about 40 s before the click. *(Answered 2026-11-24, ticket 0529, 14.4: the fog bits are the save's own there
  [C]; replaced "Open [I]: whether the fog bits are final at that call".)*
- **R3, the overlay projection at the camera's publish.** The EXE's OverlayProj (`0xDA1690 + 0x40 v`) is final at the
  lockstep publish point of frame N (`overlay-system.md` 4: built at the end of UpdateViews), but the camera block
  (`shell/common/shell_shared.h` `VPShellCamera`) does not carry it; the overlay tap delivers it only with the frame's
  texels after the Present. With OverlayProj per view in the camera block (a layout change: the camera writer, the
  server's reader, `shell/tests/camblock.py`), the fog could build frame N's mask with frame N's own window. The terrain's
  overlay texture (borders, grid) of that first frame would still be missing (its texels exist only after the Present):
  one frame without the overlay is a D66 step, a decision for the owner.

### 14.3 The held frame's visible set baked before the draw (ticket 0424 R1, 2026-11-18)

**The rule [C, `server/src/render/srv_residency_source.cpp` `noteHeldView` / `prepareHeld`, `srv_render_scene.cpp`
`Scene::decide` and `Scene::idle`].** When the fog holds a frame (`FowScene::admit` false) with an OK camera and a field,
`Scene::decide` hands that camera to the residency (`noteHeldView`: the ViewInfo the draw would make, target =
`VPShellCamera::bbW x bbH`), and the next `Scene::idle` runs the residency's `beforeDraw` for it (`prepareHeld`), without the
per-frame CPU budget (no frame waits on a held one; repeated while a pass baked and a visible land tile is still without
a bake, at most 8 passes: with no more such tiles than the burst the catch-up does not run and the burst loop stops at
the budget, which the stand-in's 28 ms bake under ASan reached after one tile, 2026-11-18): the visible set with 0409's catch-up (every visible land tile without a bake,
then the view's edge), the caches handed to the renderer. Once per camera stamp. The preparation waits while the field has
no tile styles from a plot table of its own load (`m_artFromTable && !m_artStale`, ticket 4555), because a bake before the
table is Generic and is re-baked when the table restyles it; one line names the wait per field (`residency: held frame
N: the visible set's preparation waits for the plot table's tile styles`). The draw after the install then bakes nothing.
Log lines: `residency: held frames: on|off` at the start; `residency: held frame N (the fog holds it): the visible set
prepared before the draw: B bakes in M ms` (every preparation that baked, and the first three of a field); `residency:
first drawn frame of load L (frame N): B bakes in M ms before the draw; ... K held frame(s) prepared before it` (the
first frame drawn after a field: the acceptance's "first-draw bake"). The library's own `first draw of load` line now comes
at the first preparation. Switch: environment variable `C5_RENDER_HELD_BAKE=0` (the behaviour before 0424; a test and tool
switch until a server.cfg key is wired, `srv_render_host.cpp` not being 0424's file).

**Stand-in tests [C] (`server/tests/render/fow/test_fow_scene.cpp`, cases `held_frame_prepares`, `held_frame_off`,
`held_frame_waits_for_styles`; 2026-11-18).** The fog without a plot table holds 3 frames (each followed by an idle pass of
budget 0, so no bake-ahead): with the rule, 3 preparations, the first with bakes; after the table the frame of the same
camera is drawn and its line says 0 bakes; later draws prepare nothing. Off (`C5_RENDER_HELD_BAKE=0`): no preparation, the
first drawn frame bakes. Without the art types' table at the field: no preparation and one wait line; after the table one
preparation at the next held frame, then the drawn frame bakes 0.

**Live (2026-11-18 02:03-03:08 EST) [L].** Workstation (loaded: 4-9 other heavy jobs registered, game instances of 0300,
0526 and 0517 among them; a functional run, no quiet lock: the times are indications, COMMON 23), instance c5h-i (Goldberg,
F:), save `C5H-VP8P-HUGE_0350` (c499d3c6...) copied as `C5H-0424P-HUGE_0350` and removed after, 0408's driver
(`server/evidence/0424/run_0424.py`, `live_0424.sh`: the click at the first moment the Continue button shows, back-buffer
dumps every 2 s), the D33 product path as 0404/0408. Builds (`server/evidence/0424/build_0424.sh`, D: unused): the pin
layout `x64-4f4daac7-pf8e9f42`'s bin (x64 DLL 2d737bd5...) with this branch's server.exe, proxy and composite: **base** =
main f8b04a99 (server.exe e99ad919..., proxy 8a09e909..., composite d82aea83...), **r1** = 9eda82a1 with 0424 (server.exe
84e880d2..., proxy b8f412bb..., the same composite). 3 base loads, then 5 r1 loads, one prep each; restored by hash after
each series (backup equal, provision verify ok). Raw data `F:/c5-64bit-work/0424/<build>-run<N>/`; table
`server/evidence/0424/summary_0424.txt` (`summary_0424.py`). Before the series c5h-i's import wrapper was older than
0473 (the launch refused: the child guard missing) and was reinstalled (`tools/harness/native/wrapper.py install`).

Seconds relative to the dismissal (the dismiss call's return). "First insertion frame" = the first CSV frame with the
scene-clear insertion (frame 43 in all 8 loads: the map is drawn behind the load screen from the moment the button shows;
its dump shows the load screen in all 8). "0408 count" = 0408's `map_without_terrain_frames` (frames presented after the
probe that saw the button and before the first server picture); "strict" = first server frame - first insertion frame.

| load | build | fog's table | first pair | fog installed (stamp) | held frames prepared (bakes, ms) | first drawn frame's bakes, its residency pass | first server frame (CSV, s) | strict | 0408 count | click to terrain | unbaked frames |
|---|---|---|---|---|---|---|---|---|---|---|---|
| base-run1 | base | -1.574 | -0.188 | -0.185 (46) | - | 77, 298.1 ms | 47, +0.767 | 4 | 4 | 0.767 s | 0 |
| base-run2 | base | -1.499 | -0.164 | -0.164 (46) | - | 77, 218.6 ms | 47, +0.547 | 4 | 4 | 0.547 s | 0 |
| base-run3 | base | -2.173 | -0.567 | -0.567 (45) | - | 77, 267.0 ms | 46, +0.063 | 3 | 2 | 0.063 s | 0 |
| r1-run1 | r1 | -2.706 | -0.910 | -0.905 (46) | 2 (77, 301.1) | 0, 10.1 ms | 46, -0.538 | 3 | 2 | 0 | 0 |
| r1-run2 | r1 | -2.437 | -0.960 | -0.958 (45) | 2 (77, 314.0) | 0, 7.9 ms | 46, -0.718 | 3 | 0 | 0 | 0 |
| r1-run3 | r1 | -1.663 | -0.178 | -0.178 (45) | 2 (77, 285.1) | 0, 7.9 ms | 45, -0.010 | 2 | 2 | 0 | 0 |
| r1-run4 | r1 | -1.500 | -0.163 | -0.163 (45) | 2 (77, 258.2) | 0, 7.9 ms | 45, -0.010 | 2 | 2 | 0 | 0 |
| r1-run5 | r1 | -1.999 | -0.724 | -0.724 (46) | 2 (77, 256.9) | 0, 7.8 ms | 46, -0.514 | 3 | 0 | 0 | 0 |

- **The first drawn frame bakes nothing** in all 5 r1 loads (its residency pass 7.8-10.1 ms: the near pairs' up-res and
  the draw copies), against 77 bakes in 218.6-298.1 ms in the 3 base loads. The 77 bakes moved to the first held frame of
  the in-game camera (256.9-314.0 ms, each load's second preparation baked 0); the first held frame of the load (the load
  screen's camera, before the table) waited for the table's styles in every load.
- **Map frames without terrain:** strict 2-3 (base 3-4), 0408's count 0-2 (base 2-4); the first server frame came before
  the dismissal returned in all 5 r1 loads (click to terrain 0; base 0.063-0.767 s). 0 unbaked frames in all 8 loads.
- **What is left is the pair** (14.2 item 3): the fog installs when the first overlay pair (the copy of the first
  insertion frame's Present) is bound, 2-3 EXE frames after that frame; that and the next frame are the 2-3 frames that
  remain. Only R2 + R3 (14.2, ticket 0529) remove them. In these loads the first insertion frame (43) was dumped under the
  load screen every time, and so was the next frame in 0408's dumps: how many of the 2-3 frames the player sees depends on
  when the click comes after the button [I].

**Two more loads with the budget fix (r1b, 2026-11-18 03:45-03:55 EST, server.exe 25b810ba..., proxy b3bc0fc8..., head
90be1da0) were disturbed [L]:** the workstation then ran 8 game clients and an ASan scene-bundle run. In both, one bake of
the bake-ahead under the load screen held the render thread past the click (`pipeline: slow bake`: tile (60, 38) 4,345.8 ms,
GPU timer 4,336.9 ms; tile (60, 40) 8,421.1 ms, thread CPU 8,390.6 ms; `fog: the render thread came back after 4444 / 8582
ms`), so the plot table, the pair and the in-game camera all reached the scene in the same pass and the fog installed at
once: no held frame of the in-game camera was left to prepare, the first drawn frame baked its 77 tiles itself (2,226.3 ms
and 433.6 ms, page faults 15,451 and 13,764), and 26 and 55 map frames had no terrain (first server frame +2.50 s and
+4.18 s). None of the other 8 loads had a slow bake or a render-thread gap. This is the 0407 / 0417 family (one bake
waiting seconds in the driver while the 16 GB board is shared by many clients; `terrain-bake-slow-first-use.md`)
[I: board use was not logged in this series]; R1 cannot help while the render thread is inside one bake. The same loads
showed a defect of the first build, fixed after them: the load screen's held camera, noted before the stall and still
waiting for the table's styles, was prepared after the in-game frame had been drawn (87 bakes, 274.0 and 910.7 ms of tiles
of a view the player no longer saw). Since then a drawn frame drops a pending held camera (`ResidencySource::beforeDraw`,
counted as `heldDropped`, named in the first-drawn line; stand-in case `held_frame_stale_dropped`). In all three slow
bakes the time is the bake's GPU timer step (`gpu timer` 4,336.9 / 8,417.2 / 2,555.1 ms of totals 4,345.8 / 8,421.1 /
2,558.7 ms; every other step under 4 ms), the wait `terrain-bake-slow-first-use.md` 5.3 found in the driver's Flush; the
tile (60, 40) was the 46th bake of its load both times it was slow.

**The final build (2026-11-18 04:04-04:14 EST, head 403c26a0: server.exe 732cee10..., proxy 114d462b..., same composite)
[L]:** 2 loads, both on the rule: the first drawn frame baked 0 tiles (residency pass 6.4 and 7.3 ms; the preparations 77
bakes in 242.2 and 336.3 ms), first server frame CSV frame 45 in both (strict 2, 0408's count 2, click to terrain 0.053
and 0 s), 0 unbaked frames, 0 held cameras dropped. Load 2 had one slow bake of 2,558.7 ms (tile (60, 40) again) under the
load screen, 2.6 s before the click, so it did not reach the click. Over the 7 undisturbed loads with 0424 (r1 and final):
the first drawn frame bakes 0 (6.4-10.1 ms), strict 2-3, 0408's count 0-2; base 3 loads: 77 bakes (218.6-298.1 ms), strict
3-4, 0408's count 2-4.

**Not verified:** a quiet machine; other map sizes; a reload in the same process; a load whose table comes after the
in-game camera's first frame without a stall (0408's run1 order: the preparation then starts only when the table arrives,
and gains less); the stale-drop path live (covered by the stand-in case only: no load of the final build stalled at the click).

### 14.4 The feed's plot snapshot under the load screen (ticket 0529 R2, 2026-11-24)

**The rule [C, `server/src/remote/srv_remote_feed.cpp` `AfterSetupGraphical` / `AtLeaveUpdateBody`,
`srv_remote_main.cpp` `ServerHook_ICvPlayer3_SetupGraphical`].** The EXE calls `ICvPlayer1::SetupGraphical` for players
0..63 once per load (`unit-formations.md` 12.7.3 S2), on its UI thread, outside any `ICvGame1::Update`. The server's hook
now calls `SrvRemoteFeed::AfterSetupGraphical(id)` after the forwarded call, for every id. A sequence starts at a call
whose id is not above the one before it (the units' and the models' load-mark rule) and ends at id 63 (`MAX_PLAYERS - 1`
in the 22- and 43-civ builds, `03-dll-engine-boundary/player-limits.md`). Any `LeaveUpdate` in between ends the sequence.
At id 63 of a sequence the feed takes the game-core lock (`SrvRemote::Context()->GetGame()`, the pattern `OwnerRow` already
uses on the same thread at a load) and takes the plot snapshot there (`AtLeaveUpdateBody(..., bAtLoad)`). The snapshot is
a new table generation even when the grid and the turn are the old ones: a reload of the same turn used to keep the earlier
load's table, because the Update rule takes a snapshot only for a new grid, a turn that goes back or a jump of more than
one turn. The first `LeaveUpdate` after it, the first Update after the click, re-reads every plot as a turn change does:
no new generation, one plain line with what changed in between. Lines:
`feed: load snapshot N taken under the load screen at ICvPlayer1::SetupGraphical(63), turn T (M ms in the call); the first Update re-reads every plot (ticket 0529)`
beside the usual `feed: snapshot W x H at turn T (...)`; then
`feed: the load's snapshot re-read at the first Update (turn T): C of N records changed (x,y: ...); fields: terrain ..., fog ..., art type ... (ticket 0529)`.
Not taken: `feed: no load snapshot at ICvPlayer1::SetupGraphical(63): <why>; the plot snapshot comes at the first Update`
(first 8). The reasons are: inside an Update (not a load), `server.cfg feed_load_snapshot=0` (the switch for the before
arm: the behaviour before 0529), no game context, no game, no active team (every fog bit would read unexplored), no plot
grid readable. In the last two cases the lock was held, so the first `LeaveUpdate` takes the snapshot whatever the turn
says. Report line: `feed load snapshots (0529): taken N, not taken N; first-Update re-reads N, records changed in them N`.

**The fog bits are final at that call [C, DLL fork at the x64 pin, `CvGameCoreDLL_Expansion2/`].** The feed's fog bits
come from `CvPlot::GetActiveFogOfWarMode` (`CvPlot.cpp` 479-497): `isRevealed(activeTeam)` and `isVisible(activeTeam)`, which
read `m_bfRevealed` and `m_aiVisibilityCount`. Both are restored with the plot by its serializer (`CvPlot.cpp` 13786 and
13797). `CvPlayer::setupGraphical` (`CvPlayer.cpp` 2306-2320) only calls `CvCity::setupGraphical` (1832-1857: city, wonder,
building and spaceship graphics, the layout flag) and `CvUnit::setupGraphical` (1760-1804: `GameplayUnit*` calls). Both only
notify the engine; neither changes a count. The nested `Map::UpdateDeferredFog` only sends `GameplayFOWChanged` for
deferred plots (`engine-contract.md` 4999-call row), which the feed applies as before. After the 63rd call the DLL can
still change a plot before the first Update: `CvGame::DoGameStarted` (`CvGame.cpp` 1049-1096, called after the
`SetupGraphical` calls in the headless trace, `research/sources/civ5-headless/engine-contract.md` 5.1, and once per load in
`server/evidence/2300/huge360/proxy-calls.tsv`) runs `CvPlot::Validate` on every plot (14499-14528: plot type or terrain
forced for inconsistent mountain or water plots, with no notification). The first Update's re-read catches such a change,
and its line names it. Live check of "final": the fog count of that line (below).

**Stand-in tests [C] (`server/tests/feed/test_remote_feed.cpp`, cases `load-snapshot-0529`, `load-not-reached-0529`,
`load-refused-0529`; 2026-11-24).**
- `load-snapshot-0529`: no read before id 63; at 63 one snapshot with the save's fog bits, a new generation at the same
  turn, a `kSnapshot` event. A fog change between the load and the first Update (no notification) is found by the first
  `LeaveUpdate`'s full read (35 of 35 plots, no new generation), and its line reads `1 of 35 records changed (x,y: 3,1);
  ... fog 1`. The next `LeaveUpdate` is the rolling refresh again (8 reads). The next load takes its own snapshot, and its
  re-read line says 0. The report line is present.
- `load-not-reached-0529`: a sequence that stops at 40 makes no DLL call and no line, and the first Update takes the
  snapshot as before. A lone 63 after an Update is no load.
- `load-refused-0529`: inside an Update, switch off, no active team, no grid, no context, feed off. Each case gives its
  line or nothing. With no team or no grid the first Update at the same turn takes the snapshot (35 reads, no re-read
  line). Every wrapper is released, with no DLL call outside the getters.

Results (2026-11-24 16:12-16:15 EST, workstation, `run_remote_feed_test.py --out C:/c5-64bit-work/0529/feed-plain` and
`--asan`, each through `quietlock.py run`, the fork's headers at deps/dll.lock's 1b87dd185): plain 275 of 275 checks, the
injected-failure build exits 1. Under MSVC `/fsanitize=address`: 275 of 275, the canary caught, no ASan report. The first
run of the new cases failed 3 checks of the test itself: the grid probe's 5 `GetPlot(0, y)` reads come before the 35 plot
reads, so the counts are 40.

**Live (2026-11-24 16:53-, workstation) [L].** Instance c5h-new-a (Goldberg, C:, EXE build 26577625, mods cp+vp+eui),
granted by MC-B (#13131). It was laid by `server/evidence/0600/lay.py` prep with `--composite-dll` and the helper on (the
product layout, runbook 3.3). Builds of this branch: server.exe f6f67cbf... and proxy d2388895... (runbook 1.2 against
keep/pins-0.1b: x64 DLL source 1b87dd185), composite 15887bbc... (shell/build.cmd). Both arms ran on the same build; the
arm is `server.cfg feed_load_snapshot` 0 (before) or 1 (after), set by `lay.py cfgextra` before each launch. Saves:
`C5H-4773-TINY_0018` (e337244c..., 56 x 36, turn 18) and `C5H-VP8P-HUGE_0350` (c499d3c6..., 128 x 80, turn 350), copied
as `C5H-0529P-4773-TINY_0018` and `C5H-0529P-VP8P-HUGE_0350` (saves_snap put, restored by hash after). Driver:
`server/evidence/0529/live_0529.sh series 5` over `run_0529.py` (0424's driver for a lay.py layout). It closes the load
screen through the channel the moment its Continue button shows (the earliest click) and takes back-buffer dumps every 2 s.
Table: `summary_0529.py`. **Not a quiet pair:** `quietlock run --take` waited for other owners' heavy jobs (9 registered:
0713's and 0711's render suites among them, the beta-4 critical path). Holding the lock would have stopped those jobs, so
the loads ran through the gate without the lock (functional runs, COMMON 23). Their durations are indications (grade C),
not grade-A timings. Times are seconds relative to the dismissal (the dismiss call's return; the click itself comes at
most one probe, about 0.2 s, earlier).

A smoke load before the series (tiny-single, `feed_load_snapshot=1`, server.log): `feed: snapshot 56 x 36 at turn 18
(2016 plots read, 0.45 ms)`, then `feed: load snapshot 1 taken under the load screen at ICvPlayer1::SetupGraphical(63),
turn 18 (0.49 ms in the call)` at 42.115 s server time, and `render: fog: plot table 56x36 ... read at a feed batch before
any table` 13 ms later. The first Update after the click came at 44.912 s: `the load's snapshot re-read at the first Update
(turn 18): 0 of 2016 records changed ... fog 0`.

**The series (rounds 1-5, 2026-11-24 16:57-18:08 EST; 20 loads, all exit 0, no box, no 0xC0000142) [L].** The table
below is `summary_0529.py`'s output (`server/evidence/0529/summary_0529.txt`). Times are seconds relative to the dismissal.
"First insertion frame" is the first CSV frame with the scene-clear insertion; "first server frame" is the first with the
server's picture (exact); "strict" is the second minus the first. "Fog held: why" is the reason the fog first gave
(`fog: frames held from frame N: ...`); "fog installed" is its install frame and the fog's own wait. Raw data:
`C:/c5-64bit-work/0529/live/runs/<tag>/` (run.json, analysis.json, out/ logs and CSV; deleted at hand-back, C: space).

| load | fog's first table | fog held: why | fog installed (frame, own wait ms) | first insertion frame | first server frame | strict | click to terrain | first-Update re-read: changed / records (fog) |
|---|---|---|---|---|---|---|---|---|
| tiny-before-r1 | -0.374 | no plot table from the feed | 30, 1428 | 24 (-0.609) | 29 (-0.107) | 5 | 0 | - |
| tiny-after-r1 | -3.051 | no overlay pair yet | 26, 1903 | 24 (-0.450) | 26 (-0.103) | 2 | 0 | 0 / 2016 (0) |
| tiny-before-r2 | -1.221 | no plot table from the feed | 30, 1721 | 24 (-1.534) | 29 (-1.017) | 5 | 0 | - |
| tiny-after-r2 | -2.551 | no overlay pair yet | 27, 1434 | 24 (-0.394) | 26 (-0.264) | 2 | 0 | 0 / 2016 (0) |
| tiny-before-r3 | -0.574 | no plot table from the feed | 28, 1306 | 24 (-0.726) | 27 (-0.349) | 3 | 0 | - |
| tiny-after-r3 | -2.669 | no overlay pair yet | 26, 1043 | 24 (-0.943) | 26 (-0.665) | 2 | 0 | 0 / 2016 (0) |
| tiny-before-r4 | -0.541 | no plot table from the feed | 29, 2159 | 24 (-0.826) | 28 (-0.372) | 4 | 0 | - |
| tiny-after-r4 | -2.241 | no overlay pair yet | 27, 1387 | 24 (-0.166) | 26 (+0.030) | 2 | 0.030 | 0 / 2016 (0) |
| tiny-before-r5 | -1.195 | no plot table from the feed | 30, 1928 | 24 (-1.434) | 29 (-0.990) | 5 | 0 | - |
| tiny-after-r5 | -2.662 | no overlay pair yet | 27, 1489 | 24 (-0.542) | 26 (-0.288) | 2 | 0 | 0 / 2016 (0) |
| huge-before-r1 | -1.038 | no plot table from the feed | 46, 3641 | 43 (-0.195) | 45 (-0.005) | 2 | 0 | - |
| huge-after-r1 | -34.875 | no overlay pair yet | 46, 4096 | 43 (-0.628) | 45 (-0.370) | 2 | 0 | 0 / 10240 (0) |
| huge-before-r2 | -1.292 | no plot table from the feed | 45, 7203 | 43 (-0.216) | 45 (-0.011) | 2 | 0 | - |
| huge-after-r2 | -36.716 | no overlay pair yet | 45, 6183 | 43 (-0.601) | 45 (+0.055) | 2 | 0.055 | 0 / 10240 (0) |
| huge-before-r3 | -1.070 | no plot table from the feed | 46, 4275 | 43 (-0.192) | 45 (-0.009) | 2 | 0 | - |
| huge-after-r3 | -33.385 | no overlay pair yet | 45, 3690 | 43 (-0.307) | 45 (-0.064) | 2 | 0 | 0 / 10240 (0) |
| huge-before-r4 | -0.994 | no plot table from the feed | 45, 3725 | 43 (-0.151) | 45 (-0.009) | 2 | 0 | - |
| huge-after-r4 | -36.676 | no overlay pair yet | 46, 3820 | 43 (-0.169) | 45 (-0.009) | 2 | 0 | 0 / 10240 (0) |
| huge-before-r5 | -1.069 | no plot table from the feed | 46, 3777 | 43 (-0.177) | 45 (+0.037) | 2 | 0.037 | - |
| huge-after-r5 | -32.188 | no overlay pair yet | 45, 2493 | 43 (-0.174) | 45 (+0.054) | 2 | 0.054 | 0 / 10240 (0) |

Statistics (n = 5 per arm, interleaved, arm order rotated per round; mean +- sd; after - before as a Welch difference with
its 95 % interval; grade C: no quiet lock):
- **The fog's first plot table**: Tiny -0.78 +- 0.40 s (before) -> -2.64 +- 0.29 s (after), difference -1.85 s [-2.37,
  -1.33]; Huge -1.09 +- 0.12 -> -34.77 +- 2.00 s, difference -33.7 s [-36.2, -31.2]. In every after-load the table comes
  under the load screen, at the last `SetupGraphical`: 2.2-3.1 s (Tiny) and 32.2-36.7 s (Huge) before the dismissal.
- **Strict frames** (first server frame - first insertion frame): Tiny 4.4 +- 0.9 (3-5) -> 2 +- 0 (all 2), difference
  -2.4 [-3.5, -1.3]; Huge 2 -> 2 in all 10 loads.
- **Click to terrain** (max(0, first server frame - dismissal)): 0-0.055 s in both arms on both sizes. The first server
  frame comes within 0.06 s of the dismissal's return or before it, as after 0424 (14.3). The dismiss call's return
  trails the click, so this metric cannot show the gain.
- **The fog bits at the load are final [L]**: the first Update's re-read found 0 changed records (fog 0, every field 0)
  in all 10 after-loads, 2016 and 10240 records.
- **Map frames without terrain after the dismissal**: 0 in all 20 loads.

**What R2 changes, and what it leaves.** Before, the fog's first wait is always `no plot table from the feed` (the table
of the first Update after the click). After, it is always `no overlay pair yet`: R2 removed the table as a wait in every
load. On Tiny the table used to come after the first pair (the fog installed 3-5 frames after the first insertion frame,
at 28-30), and now the fog installs with the pair (26-27; strict 2). On Huge the table came before the pair in both arms
(the EXE's first map frame after the click is slow, 14.2 item 2), so the install frame (45-46) and strict 2 do not move:
there the pair is the only wait, as 14.2 item 3 found. The 2 frames left on both sizes are the first insertion frame and
the next: the overlay tap copies that frame's own Present (`overlay: first pair bound: ... copy of Present 25` on Tiny,
`44` on Huge). Only R3 can remove them (14.2): the fog building frame N's mask without frame N's overlay copy.
(Correction 2026-11-24, 14.5: frame-by-frame dumps show these frames under the load screen even at the fastest click,
so they are not visible; R3 is not needed for the click.)
The load snapshot cost 0.49 ms (Tiny) and 1.06 ms (Huge) in the `SetupGraphical(63)` call (`feed: load snapshot` line).
The bake's tile styles (ticket 4555) come from the same earlier table, so the held-frame preparation of 0424 R1 no longer
waits for them: 14.3's line `residency: held frame N: the visible set's preparation waits for the plot table's tile
styles` appears once in each of the 10 before-loads and in none of the 10 after-loads (render.log, counted).

**R3's picture pair cannot be made by configuration [L] (2026-11-24 18:38-18:41 EST).** The diagnostic load (Tiny,
`feed_load_snapshot=1 render_overlay=0`) ended when the world came up, as designed. Server.log has `render: fog: the 64-bit
path cannot go on: fog of war: render_fow=1 needs the overlay pair (the mask shares its window), but render_overlay=0
(decision 5 (ii))`, then `server-fatal ... (exit 5, code render-fog)`; the composite has `pacing: the process ends
(why=fatal)`. This is D118's error path, not a crash. With `render_fow=0` as well the picture would have no fog, which is
not R3's look either. So the look of R3's one frame (fog without the overlay texels) needs code: a test key that lets the
fog draw without the pair. The owner's D66 question went without the pair (queue #13288, 2026-11-24). Round 6 was not run:
the quiet lock was held by 0507's timing series from 18:08, and the grant ended at 19:15.

**Not verified:** a quiet machine (grade C; no quiet pair: the take would have held the lock against the beta-4 path's
jobs); a reload in the same process (the stand-in covers the same-turn reload's new generation); a 43-civ build (id 63 is
`MAX_PLAYERS - 1` there too [C]); a load where the DLL changes a plot between the last `SetupGraphical` and the first
Update (the re-read is covered by the stand-in only: 0 records changed in all 10 live after-loads).

### 14.5 R3's look at the click: the frames it changes are under the load screen (ticket 0529 R3 DIAGNOSTIC, 2026-11-24)

**The question.** 14.4 left two map frames after a load without the server's terrain on both sizes: the fog holds them
for the overlay pair (`no overlay pair yet`). R3 (14.2) would let the fog draw them without the pair, so those frames
would show terrain and fog but no overlay texels (borders, grid). The owner's D66 question (queue #13288) asked for a
picture pair of that look at the click. Result: **at the click, on Tiny and on Huge, every frame that R3 changes is
presented under the load screen; the first frame the player sees is already the full picture, with R3 or without it**
[L, frame-by-frame back-buffer dumps]. So the owner's question has no visible frame to decide on with R2 on the product
path. What R3 would change is an earlier full picture under the load screen, not a frame the player sees.

**The test key [C, `server/src/render/srv_fow_scene.cpp` `FowScene::admit` / `beforeWorld` / `chooseWindow`,
`srv_render_options.cpp`].** `server.cfg render_fow_test_nopair=1` is a TEST-ONLY key: default 0, not in the options file,
`docs/OPTIONS.md` or `vp64_options.h`, and not written into render_config. With it the fog does not wait for the pair:
while it has none, the mask is built in a window made from camera N (0674's `c5r::fow::windowFromCamera`, whatever
`render_fow_mask_window` says), and the scene binds no overlay (`ovlCamWinNoOverlay`). This is R3's look exactly: R3 only
changes where the window's matrix comes from; the overlay texels still come only from the copies of Presents, so the
frames before the first copy have none under R3 too. Lines: `fog: TEST KEY render_fow_test_nopair=1: ...` at the init,
`fog: TEST render_fow_test_nopair=1: frame N drawn without an overlay pair ...` per frame (first 16), and
`fog: the mask's window made from the camera at frame N (no overlay pair yet, ...)`. Stand-in case `test_nopair_0529`
in `server/tests/render/fow/test_fow_scene.cpp` (default off holds; on, under both `render_fow_mask_window` values, the
first frame is drawn with the mask in the camera window [-128, 64]^2 and no overlay, the next frame with the pair binds
it; a value other than 0 or 1 is refused): 35 of 35 cases pass (2026-11-24 19:23 EST, WARP, `C5R_EXE` = the 4390913
golden copy).

**Frame-by-frame dumps.** The composite's new debug trigger `shell_composite.shotburst` (`[trace] frames=N`, at most
120) dumps the back buffer of each of the next N Presents (after the EXE's UI: what the player sees; each dump stalls its
Present, so these are pictures, not timings). The driver (`server/evidence/0529/run_0529.py`) requests the burst at the
button (`C5_0529_BURST=N`) or at the server's `HIDDEN -> NO_FIELD` line (`C5_0529_BURST_AT=nofield`). With
`C5_0529_FAST_CLICK=1` it closes the load screen by sending the button's own handler (`frontend.DISMISS_CHUNK`, which
answers `loading` while the button is hidden) in a loop from NO_FIELD on, instead of probe then dismiss: the click comes
at the first frame whose Lua tick serves the channel with the button shown, as a player who clicks the moment it shows.
The probe-then-dismiss click of 14.4 came 0.5-0.8 s later (two channel round trips under the load).

**Runs (2026-11-24 20:28-21:00 EST, workstation, c5h-new-b, EXE build 26577625, lay.py product layout, helper on).**
Builds of branch `task-0529-r3-diag`: server.exe f00fac68..., proxy 522ec608..., composite ffc8859b... (runbook 1.2 on
keep/pins-0.1b, shell/build.cmd). Saves `C5H-0529P-4773-TINY_0018` (copy of `C5H-4773-TINY_0018`, e337244c...) and
`C5H-0529P-VP8P-HUGE_0350` (copy of `C5H-VP8P-HUGE_0350`, c499d3c6...). R2 on in every load (`feed_load_snapshot=1`).
Arm a: the product's fog; arm b: `render_fow_test_nopair=1`. Functional runs through the gates, no quiet lock (MC-B
#13324). Frame numbers below are the lockstep CSV's; the render.log's camera stamps can differ by one.

| load | click | first map frame (CSV) | frames drawn by the fog before the pair | frames under the load screen (dumps) | first frame without the load screen |
|---|---|---|---|---|---|
| tiny a, probe click (tiny-r3a-2028) | after frame 32 | 24 (held, kind 5), 25 held, 26 exact | none (held 24-25) | 32 (burst began at 32) | 33, full picture |
| tiny b, probe click (tiny-r3b-2031) | after frame 30 | 24 exact | 24, 25, 26 (TEST lines) | 28, 29, 30 | 31, full picture |
| tiny a, fast click (tiny-r3a-2043) | between frames 25 and 26 | 24 held, 25 held | none | 23 (button "Loading"), 24, 25 | 26, the first exact frame, full picture |
| tiny b, fast click (tiny-r3b-2047) | between frames 26 and 27 | 24 exact | 24, 25, 26 | 24, 25, 26 | 27, full picture |
| huge a, probe click (huge-r3a-2034) | after frame 48 | 43 held, 44 held, 45 exact | none (held 43-44) | 49 (burst began at 49) | 50, full picture |
| huge b, probe click (huge-r3b-2038) | after frame 49 | 43 exact | 43, 44, 45 (TEST lines) | 47, 48, 49 | 50, full picture |
| huge a, fast click (huge-r3a-2051) | between frames 46 and 47 | 43 held, 44 held, 45 exact | none (held 43-44) | 42 (button "Loading"), 43-46 | 47, full picture |
| huge b, fast click (huge-r3b-2055) | between frames 45 and 46 | 43 exact | 43, 44, 45 (camera stamps 42-45, TEST lines) | 42-45 | 46, full picture |

Read from the dumps by eye (each grid names the frame, its CSV kind and its time to the dismissal). In the fast-click
loads the burst began under the load screen (Tiny at frame 23, Huge at 22 and 37) and ran 80 frames, so every frame from
before the first map frame to well after the load screen's end is a dump. **What the dumps show [L]:**
1. The load screen's button turns from "Loading..." to "Continue Your Journey" in the first map frame itself (Tiny frame
   24, Huge frame 43: the frame before it shows "Loading..."). The EXE's first map frame is the one that takes 1.2 s
   (14.2 item 2), and the channel (and the player's click) is served only after it. So no click can come before the first
   map frame, and the first frames after it are presented with the load screen still drawn over the map.
2. With the fastest click the load screen is gone two to four frames after the first map frame (Tiny: frame 26 or 27,
   Huge: frame 46 or 47). In arm a the first uncovered frame is the first exact one or later; in arm b the frames drawn
   without the pair (Tiny 24-26, Huge 43-45) all end under the load screen, and the first uncovered frame has the pair
   and the overlay texels. The probe-then-dismiss click of 14.4 comes 3-6 frames later still.
3. So R2 (14.4) already removed the visible cost: the fog's two held frames (Tiny 24-25, Huge 43-44) are under the load
   screen at the earliest click. R3 would draw those frames (the TEST key installs the fog at the first map frame: strict
   0) but nobody sees them. **R3 is not needed for the click after a load with R2 on;** the owner's D66 look question
   has no visible frame on this path.

Pictures (DIAGNOSTIC, never in git, COMMON 27): `F:/c5-64bit-work/0529-r3/0529-r3-pair-tiny-DIAGNOSTIC.png` and
`...-huge-DIAGNOSTIC.png` (the fast-click pair, a over b, frames -1..+5 from the first map frame; Visual Checks
vc-20261124-210336-workstation-7f5e and vc-20261124-210400-workstation-5433), every frame at full size under
`F:/c5-64bit-work/0529-r3/{tiny,huge}/`, the probe-click pair under `F:/c5-64bit-work/0529-r3/probe-click/`
(vc-20261124-210434-workstation-8b01, vc-20261124-210451-workstation-4ee0), and the per-load contact sheets the driver's
`analyse` records. Tool: `server/evidence/0529/r3_pair_0529.py`. The run folders (`C:/c5-64bit-work/0529r3/live/runs/`)
were deleted at the hand-back (C: space).

**Not verified:** the look of the frames R3 draws (they are under the load screen; the back buffer holds the EXE's UI over
them, so no presented picture shows them, and no pre-UI dump was taken); a human click (the fast loop clicks at the first
served frame; a person is slower, so sees even later frames); a reload from inside a game (the load screen is the same
`LoadScreen` context [I]); a quiet machine (pictures only, no timing claim: each dump stalls its Present); the 43-civ
build. Earlier statement corrected: 14.4's "the 2 frames left on both sizes ... only R3 can remove them" stands for the
CSV, but those frames are not visible at the click (this section, 2026-11-24).
