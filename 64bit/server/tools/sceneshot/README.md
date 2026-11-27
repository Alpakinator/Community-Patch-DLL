# sceneshot: the offline scene harness's player (epic 0301, task 3905)

`sceneshot.exe` draws a recorded frame of a scene bundle (`research/06-tooling-and-workflows/offline-scene-harness.md`)
with the same render units `server.exe` links, without the game, and writes the server's picture of it: a colour PNG and
the raw depth. With `--compare` it first compares that colour and depth byte for byte with the server's own readback of
the same frame (the bundle's `dumps/`): the standing check "offline equals live" (P1). The contract's "sceneshot" section
has the replay order, the exit codes and the measured results.

## Build

```
python -I perf/quietlock/quietlock.py run --who <id> --what "sceneshot build" -- \
    python -I server/tools/sceneshot/build_sceneshot.py --out C:/c5-64bit-work/<ticket>/ss [--asan] [--smoke] \
        [--c5render DIR] [--common-objs DIR]
```

The source list, include folders, libraries and flags come from
`server/tests/render/scenebundle/build_scenebundle_tests.py` (imported). `--common-objs` reuses the common objects of a
scenebundle test build of the same variant (`<out>/obj[-asan]/common`). Output: `<out>/bin[-asan]/sceneshot.exe`.

## Run

```
sceneshot.exe --bundle DIR (--frame ID | --span FIRST:LAST | --camera NAME | --dumped) --out DIR
              [--compare] [--diff] [--no-pictures] [--adapter bundle|hw|warp|<LUID hex>]
              [--install DIR] [--exe PATH] [--settle-ms MS] [--bake-rate R] [--bake-burst N] [--bake-budget-ms MS]
              [--tolerance PIXELS:DELTA] [--allow-other-exe] [--allow-other-assets] [--mods DIR] [--set render_KEY=VALUE]
              [--asset-root DIR]
```

- Decals (format version 3, issue 0364): with `render_config` `render_decals` "1" the residency runs its decal pass and the
  set comes from `decals/load<NNNN>.dtap` (through the scene's decal reader, before the field's load). Their names and
  textures are read from the install and the mods' folder the recording used (`render_decal_mods`, a
  `...\My Games\Sid Meier's Civilization 5\MODS` folder; `--mods DIR` replaces it), never this machine's own Documents.
  A missing folder exits 9 (Install).

- `--dumped --compare` is the P1 check: every frame the recorder dumped, compared before any PNG is written.
- `--tolerance PIXELS:DELTA` (default `0:0`, equal bytes): a frame passes when its depth is byte-equal and at most PIXELS
  colour pixels differ, each by at most DELTA in any channel. The tolerance in force is the one the MC accepted (contract
  section 16); `--set render_KEY=VALUE` replaces a `render_config` value for a diagnosis (not P1, said in the output).
- `--option KEY=VALUE` (ticket 4938, repeatable): one `[Extras]` line of `VP64Options.ini` for the player's `SrvOptions()`,
  as if the file held it (every other option at its default, as before). A key that is not an `[Extras]` option, or a
  value the options loader would warn about, exits 2 (Usage). Each line is printed. It is how an extra's on and off
  pictures are drawn (rule E9 of the contribution guide for extras): `--option EnableVisualAdditions=1 --option
  VisibleAreaOutline=1`. With an extra on, the picture differs from the recording by design (not P1); with every extra
  off (the default, or `--option VisibleAreaOutline=0`), `--dumped --compare` is P1 as before. The player links
  `server/src/extras/` (`build_sceneshot.py`); the scene tests do not, and their scenes get no extras module.
- `--adapter bundle` (the default) picks the bundle's adapter by its LUID, else by its description (a LUID changes at
  every start of Windows). `hw` is the largest hardware adapter, `warp` the software rasterizer; a comparison on another
  adapter than the bundle's is said to be "not P1" in the output.
- `--install` defaults to the bundle's `asset_set.install`, `--exe` to `<install>/CivilizationV_DX11.exe`. The EXE is
  checked against `pins.exe_sha256` and the install's asset set against the bundle's (D94); either difference refuses
  the run unless `--allow-other-exe` / `--allow-other-assets` is given.
- **Where the files come from (ticket 0614, offline-scene-harness.md section 34).** Without `--asset-root` the player
  reads the recording instance's own install and MODS folder (the bundle's `asset_set.install` and `render_decal_mods`),
  and its mods database at each use. A live run on that instance rewrites the database while its game starts ("0
  activated" for about two minutes): the reason for 0455's ".fxsxml not found" exit 8. So:
  - a source (the install, the EXE, the mods' Documents folder, an asset root or its install) inside a pool instance (a
    folder holding `instance.json`) whose claim file exists (`<C5_POOL_DIR, else <C5_DEFAULT_INSTANCES_ROOT, else
    %USERPROFILE%\c5-instances>\_pool>\claims\<name>.json`, the rule of `tools/harness/pool.py holder PATH`) exits 9
    with a line naming the instance, the claim file, its owner and ticket; checked before anything is read and again
    before the OK line (a claim made during the replay fails it). An unclaimed instance is read, with a NOTE line.
  - `--asset-root DIR` (or the environment's `C5_SCENE_ASSET_ROOT`; not with `--install` or `--mods`): a frozen copy of
    the activated mods' folders and the mods database, with a never-launched install (default the setting
    `render_game_dir`, pure_vp), made by `assetroot.py make --from <instance> --out DIR` while the instance is free.
    The player checks `asset-root.txt` first (the database's SHA-256, the MODS listing hash, no `-journal`/`-wal`, the
    database read with as many activated mods as recorded): any difference exits 9. Nothing falls back to the recording
    instance. 2026-11-20: C4PORT with an asset root made from c5h-c and pure_vp as the install: 8 of 8 frames
    byte-equal (the same as with c5h-c's own files).
  ```
  python -I server/tools/sceneshot/assetroot.py make --from c5h-c --out C:/c5-64bit-work/<ticket>/asset-roots/c5h-c-<date>
  python -I server/tools/sceneshot/assetroot.py verify C:/c5-64bit-work/<ticket>/asset-roots/c5h-c-<date> [--full]
  sceneshot.exe --bundle B --out O --dumped --compare --tolerance 1:1 --asset-root C:/c5-64bit-work/<ticket>/asset-roots/c5h-c-<date>
  ```
  One root serves every bundle recorded with the same mods (the 4980 and 0301 bundles: CP, VP and EUI compatibility of
  VP 5.4.8); the asset set (VT) and the EXE pin are still checked against the bundle.
- `--settle-ms` (default 120000) bounds the residency's settle before each shot; `--bake-rate` (100000/s),
  `--bake-burst` (4096) and `--bake-budget-ms` (50) replace the live token bucket (they change when a tile is baked, not
  what is baked).
- Outputs in `--out`: `<frame id>.png` (or `<camera>.png`), `<stem>.depth.f32` (float32 rows), with `--diff`
  `<stem>.diff.png` for a differing frame, and `sceneshot.json` (every shot's hashes, comparison numbers and times).
  `--no-pictures` writes only `sceneshot.json`. Every picture kept or looked at is recorded with
  `tools/visualcheck/visualcheck.py record ... --what "DIAGNOSTIC: ..."` (D138); none is committed.
- The time per shot in the output is an indication of the workflow, never a product figure (`docs/briefs/PERFORMANCE.md`;
  several players on one adapter slow each other).

## Exit codes

| Code | Name | When |
|---|---|---|
| 0 | OK | every shot written (with `--compare`: each byte-equal to its dump) |
| 2 | Usage | bad command line |
| 3 | NoCamera | `--camera NAME`: no `cameras/NAME.vpcam` |
| 4 | FrameOutside | `--frame` / `--span` outside the recorded span, or a frame id inside it that was not drawn |
| 5 | SettleTimeout | the residency did not settle within `--settle-ms`: no picture |
| 6 | ResidencyStopped | the residency stopped (out of memory at radius 0): no picture |
| 7 | NoDevice | the adapter is not here, or the device failed |
| 8 | ReplayFailed | a member the scene refused, the feed out of order, a draw error, a recorded frame flag the replay does not reproduce |
| 9 | Install | the EXE or install missing, or not the bundle's (EXE pin, asset set); a source in a claimed pool instance, or an asset root missing, damaged or changed (ticket 0614) |
| 10 | Differs | `--compare`: a frame's depth is not byte-equal to the server's dump, or its colour differs beyond `--tolerance` |
| 11 | WriteFailed | an output file could not be written |
| 12 | NoDump | `--compare` on a frame without a dump |
| 13 | Unsupported | `render_tiles=tap`, a `render_config` value the server's parser refuses, a load inside the span |
| 21-42 | (refusal) | the bundle checker refused the bundle (`scenebundle::RefusalExitCode`), after its `scenebundle: REFUSED ...` line; a missing bundle is 21 NoManifest |

## Tests

`server/tests/render/scenebundle/sceneshot_test.cpp` (built and run by `build_scenebundle_tests.py --run`, plain and
`--asan`): synthetic bundles made by the test's own run of the scene in the host's order, the player run in process on
WARP and on the hardware adapter, every exit code and output line asserted. Needs the installed game (`C5_GAME_DIR`, or the
Steam path) for the EXE's shaders and the water art, read-only. Ticket 0614: `CaseClaimedInstance` (a recorded mods'
folder in a fake pool instance, a fake pool dir through `C5_POOL_DIR`: unclaimed played with the NOTE, claimed or an
unreadable claim refused with exit 9 and nothing written); `server/tests/render/scenebundle/test_assetroot.py` (pytest,
`C5_SCENESHOT_EXE` required): `assetroot.py` make, verify and its refusals, and the built player against Python-made
roots (accepted, then NoManifest for the missing bundle; a changed database or MODS, a journal, no manifest, another
format, a claimed install: exit 9; `--install`/`--mods` with an asset root: exit 2). A test's `C5R_GAME_DIR` inside a
claimed pool instance is now refused too: point the tests at pure_vp.

## The unit scene (issue 0440, bundle format version 5)

sceneshot links the server's unit scene (`server/src/units/*.cpp` and `c5r_models.lib`, through the common units of
`build_scenebundle_tests.py`). A bundle with `render_config` `render_units=1` is replayed with it: the database is the
`units_db` member, written to `--out/units-gen<NNNNNNNNNN>.db`; the controller's state at the span's first frame is
rebuilt from `units_history.bin` (every frame the server drew from the controller's first frame, replayed through the
scene's own calls before the span; its `ready()` state checked per frame). A bundle whose `version_notes` hold a
`gap: units:` line is Unsupported (13); `gap: fog particles` (the FOWLevel-3 particles drawn live without their state in
the bundle) is printed as a NOTE. Diagnostics, not P1: `--set render_units=0` (no unit scene) and `--units-no-history` (the
controller starts at the span's first frame). Contract: `research/06-tooling-and-workflows/offline-scene-harness.md`
section 22.

## The FOWLevel-3 fog particles (ticket 0403, bundle format version 6)

With `render_config` `render_fow` and `render_fow_particles` "1" the player turns the fog's particles on again after the
fog's clock (`fog_clock.bin`) from `particles.bin`, the simulation's byte-exact state at the span's first frame
(`FowScene::setParticleState`): `sceneshot: particles.bin in use: spawned, seed S, ...`. A state the fog scene refuses
(another install's flow field, a damaged state, `C5_RENDER_FOW_PARTICLES=0` in the player's environment) fails the replay
(8, nothing written). `render_fow_particles=1` without the member is the checker's MissingMember unless the recorder's
`gap: fog particles` note says why (then the replay draws no particles and prints a NOTE). Diagnostic, not P1:
`--set render_fow_particles=0` (no particles). Contract: `research/06-tooling-and-workflows/offline-scene-harness.md`
section 23.
