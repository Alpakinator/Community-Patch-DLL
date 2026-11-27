# render/ - asset pipeline of the server's terrain renderer

Epic 2900 (package I2-D of increment 2). A standalone x64 static library, `c5render`, that reads the installed
game's files (FPK packs, loose files, DDS textures) and the DXBC shaders embedded in `CivilizationV_DX11.exe`, plus
the tools and tests around it. It does not depend on `server/`, `proxy/` or `ipc/`; the root `CMakeLists.txt` does
not include it yet.

## Build

    python render/build.py --test

Toolchain: clang-cl -m64, lld-link, Ninja, MSVC 14.4x STL and the Windows 10 SDK through `vcvarsall x64` (the
server's toolchain, D15), C++14, `/MD`. The build tree goes to `C:/c5-64bit-work/render-build/<key>/<config>` when
`C:/c5-64bit-work` exists (`<key>` = 10 hex digits of the SHA-1 of the checkout's path, so each checkout and worktree
has its own tree; bug 0091), else `build/render-x64/<config>` (*corrected 2026-11-21, one-off 9080: on C: since ticket
0181, D133, as `render/build.py` says; the text said D:*); `--build-dir` overrides, `--target` builds one target
(`shell/build.cmd` uses both). Tests that read game files take `C5R_GAME_DIR` (install root), `C5R_EXE` and
`C5R_WORK` (scratch, default `<build tree>/test-out`) from the environment, set by `--test` from `--game-dir`,
`--exe`, `--work` or the defaults in `build.py`; without them they report SKIP, the executable exits 77, and
`build.py --test` fails the run: skipped counts as failed. Nothing is ever written into a game folder.

### AddressSanitizer variant (issue 3209)

    python -I perf/quietlock/quietlock.py run --who <team> --what "render ASan" -- python render/build.py --asan --test

MSVC `cl` + `link` + Ninja through `cmake/msvc-asan-x64.cmake` (LLVM 19's clang-cl ASan runtime crashes at start-up
on this machine), `/fsanitize=address /Zi /Oy-` on every target, warnings `/W4 /WX /wd4100` (cl's spelling of the
clang build's set), its own build tree `<default tree>-asan` (`Release-asan`) unless `--build-dir` is given. With
`--test`: first the canary `c5r_asan_canary` (one byte past a heap block, built only in this variant) must produce a
heap-buffer-overflow report, else the run fails ("ASan not active"); then ctest runs under a job-object memory cap
(`--mem-cap-mb`, default 6144, via `shell/tools/memcap.py`) with `ASAN_OPTIONS` `log_path="<tree>/asan-logs/asan"`
(quoted for the drive's colon), `log_exe_name=1`, `halt_on_error=1`; every non-empty `asan.<exe>.<pid>` file fails the
run and its SUMMARY line is printed. ctest's lines stream as progress. The runtime DLL comes from the toolset's
`bin\Hostx64\x64` on the vcvarsall PATH. A sanitizer run is heavy: start it through `quietlock.py run` (COMMON 16).

The render suite of the server (`server/tests/render`, pytest) has its own ASan variant (issue 0149):
`server/tests/render/build_render_tests.py --out DIR --asan` builds `DIR/bin-asan` and copies
`clang_rt.asan_dynamic-x86_64.dll` (and its .pdb) from cl.exe's folder beside the binaries, so pytest needs no PATH
change; with `C5_RENDER_TEST_BUILD=DIR/bin-asan` the suite's `conftest.py` sets `ASAN_OPTIONS` `log_path` itself, quoted,
into `C5_ASAN_LOG_DIR` (default `DIR/asan-logs`). The two traps it removes (9055): no
runtime on PATH fails every exe-starting test with 0xC0000135; an unquoted `log_path=D:/...` ends at the drive colon
("expected '=' in ASAN_OPTIONS") and fails every process at start.

## Layout

One folder per module; each has its own `CMakeLists.txt` and is picked up automatically:

| Path | Contents |
|---|---|
| `include/c5r/status.h`, `binreader.h`, `src/common/` | error codes (`c5r::Err`) and the bounds-checked reader every parser uses |
| `include/c5r/gamefs/`, `src/gamefs/` | FPK reader, loose-file lookup in the game's order, DDS parser and D3D11 texture creation (task 2901) |
| `include/c5r/shaders/`, `src/shaders/` | PE `.shader` section, DXBC container, shader index and reflection (task 2902) |
| `include/c5r/terrainsets/`, `src/terrainsets/` | the terrain-side shader sets and their constant-buffer layouts (task 2903) |
| `include/c5r/heightfield/`, `src/heightfield/` | the height-field tap format, the EXE grid layout, comparison (task 2904: tap, not re-bake) |
| `include/c5r/terrain/`, `src/terrain/` | the terrain surface drawn with the EXE's tessellated TerrainShader programs: per-tile height textures from a `HeightGrid`, TerrainFrame / TerrainTile / DistanceFog from the camera, tile colour caches or own colour sources, water placeholder, GPU timestamps (epic 3200, issue 3202) |
| `include/c5r/bake/bake_types.h` | the bake stage 1 contract (epic 3400, owned by the team lead): LayerTables, LayerList, TextureSet, BlendVertex |
| `include/c5r/bake/layers.h`, `src/bakelayers/` | TerrainDef.csv (the loose DLC file) -> LayerTables, the EXE's normal grid and list builder -> LayerList per tile (issue 3401; `terrain-bake-records.md` 2.2-2.4) |
| `include/c5r/bake/blend.h`, `src/bakeblend/` | the blend pass: per-vertex weights and the 67 x 67 grid, the EXE's TerrainShaderScreen pass 2, three 512 x 512 float targets (issue 3402; `terrain-bake-blend.md`) |
| `include/c5r/bake/post.h`, `src/bakepost/` | the passes after the blend: normals, macro shadow, CacheBake, GPU BC1 compression into t0/t8; style cubes and ColorKey LUT (issue 3403; `terrain-bake-passes.md`) |
| `include/c5r/bake/pipeline.h`, `src/bake/` | the bake pipeline: `PipelineBaker`, the `TileBaker` of `baker.h` over bakelayers + bakeblend + bakepost, with interfaces (and stand-ins) for grid normals, slope classes, height page, bake constants, style (issue 3404; `terrain-bake-comparison.md`) |
| `include/c5r/bake/residency.h`, `include/c5r/bake/standin.h`, `src/bakeresidency/` | residency, bake-ahead and invalidation of baked tiles: `Residency` (the calls of epic 3300's `srvrender::TileCacheSource` seam: onField, onPlotsChanged, beforeDraw, idle, release, statsLine; the radius option, BC1 arrays with Texture2D draw copies, OutOfMemory, the gray-frame counter) and `StandinBaker` (one flat BC1 colour per tile, test seams); used by `shell_server.exe --bake pipeline|standin` and meant for server.exe (issue 3407; `terrain-bake-residency.md`) |
| `tools/bakecompare/` | the server's t0/t8 against the EXE's own caches of 3401's reference set, texel by texel, with the EXE-vs-EXE ceiling, classes of difference and difference images (issue 3404; exit 0 report, 2 usage, 3 missing, 4 unparsable, 5 inconsistent, 6 set-up) |
| `tests/<module>/` | tests, one executable per module; every check has a negative test |
| `tools/<tool>/` | command-line tools: `fpkscan`, `shaderdump`, `hfbake`, `quadtest`, `terrainshot` (+ `offline_all.py`, `compare_shots.py`, `highlight_accuracy.py`), `bakelayers_check` (server lists vs the EXE's, exit 0 equal / 1 differ / 2-5 input errors), `initbench` (issue 0198: the install-reading part of the server's render start, one arm per process; driver `evidence/0198/initbench_series.py`); binaries land in `<build>/tools/<tool>/` |
| `evidence/3200/` | the offline comparison's results and pictures (at most 512 px wide) |
| `include/c5r/units/`, `src/units/`, `tests/units/`, `tools/unitscene/` | the unit module `c5r::units` (epic 0041, U1-U3): input `UnitScene` and the `.c5units` file, art from install and mods, the EXE's unit draw inputs (28- and 20-byte streams, palette blocks, ids, cb1) and the three passes, pose layer with clip cache, formation placement, failure statuses and the pre-check; builds `models/src` as `c5r_models` (`research/05-graphics-composition/unit-renderer.md`) |
| `include/c5r/pathmodels/`, `src/pathmodels/`, `tests/pathmodels/` | the move-order path's models `c5r::pathmodels` (ticket 0300, part 1): the placement rule (instances from the served nodes and their ground positions), the 18 `assets/UI/Overlays` assets through `c5models` (32-byte vertices, material textures, SelectedAni clips), the draw list and `PathRenderer` with the EXE's UIShader; tests against 3801's capture (`tests/pathmodels/f6496_reference.h`), the DIAGNOSTIC picture tool `c5r_pathshot` (`evidence/0300/`) (`research/05-graphics-composition/path-models.md`) |
| `include/c5r/unitanim/`, `src/unitanim/`, `tests/unitanim/`, `tools/unitanim/` | the unit animation controller `c5r::unitanim` (epic 0374, U5): the `.c5anim` timeline file (4404), the runtime (4403: the K6 clock, the per-unit random source with forced choices, the state machine interpreter, the logic file cache, the controller entry with its statuses, X1 cross-fade rules and the pre-check), the graph interface for the evaluator (4409), the event bridge (4404: path-4 codes, EnableActions per member, damage deaths) and the offline driver `unitanim play` (4404: programs, triggers and status CSV, `.c5units` frames; `driver.h`), the oracle run `tools/unitanim/oracle_anim.py` (4406: C on the catalogue and the recorded sequences, D transitions and random parts; K6 continuous clock by default, MC #2089) (`research/05-graphics-composition/unit-controller.md` 13-15) |
| `include/c5r/arc/`, `src/arc/`, `tests/arc/`, `evidence/0199/` | the ranged-attack arc `c5r::arc` (ticket 0199 part 1): ArrowSettings.xml from the install, the generated mesh (rings and head), the arrow record with its growth clock, the end points from the plots through the height field and the wrap choice, the two ArrowShader draws (the EXE's program) with stock's states and constants; tests against 0179's recorded arc (`C5R_ARC_REF`, made by `tests/arc/mkref_arc.py`) and DIAGNOSTIC pictures (`evidence/0199/picture_0199.py`) (`research/05-graphics-composition/arrows-and-path-line.md` 12) |

Linking `c5render` from outside this CMake project (e.g. `shell/build.cmd`): it needs `d3d11 dxgi dxguid d3dcompiler
windowscodecs ole32` (c5r::terrain compiles its own shaders with D3DCompile at `init`).

The `c5r_terrain_test` draw tests need `C5R_EXE` and the ground truth of epic 3000 (`C5R_GT`, default
`D:/c5-64bit-work/i2-ground-truth`): without the EXE they skip (and the run fails); without the ground truth
`game_offline_draw_tiny_ground_truth` fails with the path it looked for.

Research: `research/05-graphics-composition/asset-pipeline.md`, `terrain-rendering-inventory.md` and
`terrain-surface.md`.
