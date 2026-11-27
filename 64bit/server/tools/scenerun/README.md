# scenerun: the scene library run, its report and the copy to a test machine (epic 0301, task 3906)

`scenerun.py` runs every bundle of the scene library (`F:/c5-64bit-work/scene-library/`, contract section 11) at every
camera with 3905's player `sceneshot.exe`, with no game instance, and writes a report that keeps two comparisons apart:

- **Regression:** each shot against the player's own earlier picture of the same bundle and shot on the same adapter
  (the baseline folder `<library>/baselines/<machine>__<adapter>/<bundle>/`).
- **Fidelity:** each shot against the stock screenshot of the same camera (median, p95, maximum and shares of the
  per-pixel largest channel difference per region mask, and a difference image; COMMON 25).

Beside them, when the run is on the bundle's adapter, each recorded frame is compared with the server's own dump of it
(P1, `sceneshot --compare --tolerance`). The contract's section 17 has the design, the measured runs and the times.

## Commands

```
# the run (workstation): every bundle, every camera; pictures recorded as DIAGNOSTIC (D138)
python -I perf/quietlock/quietlock.py run --who <id> --what "scenerun" -- \
    python -I server/tools/scenerun/scenerun.py run --player C:/c5-64bit-work/<id>/ss/bin/sceneshot.exe \
        --out F:/c5-64bit-work/<id>/run --issue <id> [--bundle ID ...] [--tolerance 1:1] [--asset-root DIR]

# times (indications): five passes without pictures; quietlock.py check --quiet is read before each
    ... scenerun.py run ... --no-pictures --no-record --repeat 5

# a test machine: put the bundles there by SHA-256 (tools/testbed/tb.py), then run there
python -I server/tools/scenerun/scenerun.py copy --host oldpc --work C:/c5-64bit-work/<id>/copy-oldpc
python -I -X utf8 tools/testbed/tb.py --host oldpc submit --name "<id> scenerun" --timeout 3600 \
    --file C:/c5-64bit-work/<id>/ss/bin/sceneshot.exe=scenerun/sceneshot.exe -- \
    "%TESTBED_PYTHON%" server/tools/scenerun/scenerun.py --library "%TESTBED_ROOT%/scene-library" run \
        --player "%TESTBED_BIN%/scenerun/sceneshot.exe" --out "%TESTBED_OUT%/run" --adapter hw \
        --install "%TESTBED_BASE%/pure_vp/Sid Meier's Civilization V" --issue <id>
python -I -X utf8 tools/testbed/tb.py --host oldpc fetch JOB --out F:/c5-64bit-work/<id>/oldpc-JOB --include "run/report.*"
python -I tools/visualcheck/visualcheck.py pull --host oldpc

# other subcommands
python -I server/tools/scenerun/scenerun.py index            # write <library>/index.json (what the run then requires)
python -I server/tools/scenerun/scenerun.py verify [--bundle ID]   # every member's SHA-256 against scene.json
python -I server/tools/scenerun/scenerun.py report RUN_DIR   # regression and fidelity again from a run folder
```

`--library ROOT` comes before the subcommand (default `C5_SCENE_LIBRARY_ROOT`, else `F:/c5-64bit-work/scene-library`).
On a test machine the library is `<testbed root>/scene-library/` (`E:/c5-testbed/scene-library` on oldpc), written only
by `receive` (the copy's host side), which checks every member against the workstation's hashes before a bundle
enters it and writes its `index.json`. The player there uses `--adapter hw` (the bundle's adapter is the workstation's:
not P1, so no `--compare`) and the testbed's `pure_vp` base install, whose EXE is the bundles' pin (04cfdc71...) and
whose Vibrant Textures equal the workstation's (D94). Fidelity on the laptop would name its asset set; the testbed
instances and base carry VT there too (quiet-testbed.md 21.4), so a run from them is like with like.

**The game's and the mods' files (ticket 0614).** Without `--install` or `--asset-root` the player reads each bundle's
recording instance (its install, MODS folder and mods database), refuses one the pool shows claimed (player exit 9,
PlayerFailed here) and fails a run during which a claim was made. On the workstation, while live runs use the pool,
give the run `--asset-root DIR` (made with `server/tools/sceneshot/assetroot.py make --from <instance> --out DIR` while
the instance is free; the environment's `C5_SCENE_ASSET_ROOT` does the same): the player then reads a frozen copy of the
activated mods' folders and database with a never-launched install, checked against its manifest, and never falls back
to the instance (sceneshot README, "Where the files come from").

## What the run requires, and when it fails

| Exit | Name | When |
|---|---|---|
| 0 | OK | every bundle ran, P1 within the tolerance where it applies, no regression, every picture recorded and described |
| 2 | Usage | bad command line, `--tolerance` not PIXELS:DELTA, `--player` missing |
| 3 | MissingBundle | a bundle the index lists or `--bundle` names is not there; a library without a bundle (never a skip) |
| 4 | ShaMismatch | a member's size or SHA-256 differs from scene.json, an unlisted file, scene.json differs from the index (also `receive`: the shipped copy against the workstation's hashes) |
| 5 | PlayerFailed | the player exited other than 0 or 10 |
| 6 | P1Differs | the player's exit 10: a frame differs from the server's dump beyond the tolerance |
| 7 | Regression | a shot's depth differs from the baseline's, or its colour beyond the tolerance |
| 8 | Undescribed | `visualcheck.py list --issue <id> --result undescribed` is not empty after the run |
| 9 | RecordFailed | `visualcheck.py record` failed |
| 10 | HostFailed | `copy`: the test machine's `receive` job did not pass |

3 and 4 are decided before anything runs, and nothing is written. The last line is `scenerun: OK ...` or
`scenerun: FAILED <code> <Name>: <why>`; the report (`report.json`, `report.md`) is written for every run that started.

## Outputs

`--out` holds `rep01/<bundle>/frames/` (the player's PNGs, depth and sceneshot.json for the dumped frames, and its log),
`rep01/<bundle>/camera-<name>/` per named camera, `fidelity/<bundle>/<shot>.diff.png` (the per-pixel largest channel
difference x4) and the report. Pictures are game pictures: never committed (D83, D138). Times in the report are
indications of the workflow (PERFORMANCE.md), with the quiet check's exit beside each pass.

## Tests

`server/tests/render/scenebundle/test_scenerun.py` (stand-in player `scenerun_fake_player.py`, synthetic bundles, a
temporary Visual Checks store): `python -I -m pytest server/tests/render/scenebundle/test_scenerun.py -q -p no:cacheprovider`.
