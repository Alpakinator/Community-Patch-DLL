# terrainreplay: a capture's terrain draws replayed offline, with the overlay target t1

Issue 0182 (2026-11-14). Replays the EXE's TerrainShader draws of a stock RenderDoc capture on D3D11, hull and domain
shaders included (the 16-control-point patch list, topology 48), with every pixel-shader input as captured, the
overlay target t1 among them, and compares the result with the frame of the same launch. A measurement tool: no
renderer code, and nothing from the EXE or the game's files is committed (D83): the export, the replay and the
pictures stay under `C:/`, `D:/` or `F:/c5-64bit-work/`, and every front end refuses another output folder.

Findings, numbers and limits: `research/05-graphics-composition/renderdoc-capture.md` section 9.

## Files

| File | What |
|---|---|
| `terrainreplay.cpp` | the replayer (`terrainreplay.exe MANIFEST`); manifest format in its header; exit 0 done, 1 bad manifest or input file (no output written), 2 usage, 3 a D3D11 call failed |
| `export.py` | the capture's terrain draws (materials 73, 76, 78) through 3601's exporter (`shell/evidence/3600/3601/xml_export.py`); t1 is exported with the other SRVs as the target's contents at the frame's start |
| `manifest.py` | writes one manifest per variant from an export and runs the replay (`--no-run`: the manifests only, used by `tools/harness/cli.py rdc-export --manifest`, issue 0302); variants `exe` (as captured), `nooverlay` (t1 transparent), `nofow`, `nofog`, `noshadow`, `pass1`, `server*`, `lod0` (docstring); reads a `--select all` export (issue 0347): a buffer SRV becomes a `buf` line, or is skipped with a warning naming draw, stage and slot (also in replay.json's `skipped_srvs`) when `buf` cannot bind it |
| `compare.py` | the replay against the stock frame: overlay pixels, plots, classes, statistics, difference images (docstring) |
| `test_terrainreplay.py` | the test: a tessellated patch with a t1 on WARP (composition, depth, id, tessellation), and every input check of the four parts with its exit code |

`export.py` and `manifest.py` are 3019's `render/evidence/3019/terrain_export.py` and `replay_manifest.py` (commit
1234c563) with output-folder checks and exit codes added; `terrainreplay.cpp` is 3019's `terrain_replay.cpp` with the
`device` and `shader_hlsl` directives, the subresource size check and exit codes.

## Directives added by issue 4152 (epic 0327, unit draws; 2026-11-15)

Granted by the MC (queue 1446 and 1463) as an additive change: nothing existing changes. Manifests written before
(manifest.py, epics 0166 and 0301) replay to bit-equal outputs: 2026-11-15, the base build (this folder at commit
4182eb7e) and the task build gave byte-equal colour, depth and id files for the three test manifests and for
manifest.py's `exe`, `nooverlay` and `nofow` variants of 3901's terrain export (3701 stock A, frame 1762), compared by
`models/tools/unitreplay/base_vs_task.py`; the existing 38 test cases pass on both.

| Directive | What |
|---|---|
| `call INDEXED COUNT START BASE [INSTANCES [START_INSTANCE]]` | with INSTANCES (1..65536): `DrawIndexedInstanced` / `DrawInstanced`; without: as before |
| `buf STAGE SLOT PATH FMT FIRST NUM` | a buffer SRV (the unit draws' bone palette, VS and PS t27: FMT 2, R32G32B32A32_FLOAT); formats of 4, 8 or 16 bytes per element |
| `tex ... DIM 4 ...` | a cube texture (square faces, ARR a multiple of 6, `TEXTURECUBE` or `TEXTURECUBEARRAY` view). `UnitShader_Skinned_CODE`'s t2-t4 are cubes; given as DIM 2 with ARR 6 they stay a 2D array, which a TextureCube register reads as 0 (the units come out black) |
| `background PATH` | raw RGBA8, W x H x 4 bytes: the colour target's contents before the first draw (SAMPLES 1) |

The unit-draw manifests are written by `models/tools/unitreplay/unit_manifest.py` (any draws of an `rdc-export`
export, a draw's vertex or index buffer replaceable); findings in `research/05-graphics-composition/unit-playback.md`
sections 4 and 7. The test's `unit_draw_cases` covers them on WARP (62 cases in all on 2026-11-15).

## Use

Build (clang-cl, the render tree; through the build gate):

    python -I perf/quietlock/quietlock.py run --who <ticket> --what "render build terrainreplay" -- \
        python -I render/build.py --target terrainreplay --build-dir C:/c5-64bit-work/<ticket>/render-build

Test (WARP, no GPU, no game data; 38 cases on 2026-11-14):

    python -I render/tools/terrainreplay/test_terrainreplay.py --exe C:/c5-64bit-work/<ticket>/render-build/tools/terrainreplay/terrainreplay.exe

A capture (the run of 0182, 3701's stock A on Tiny; the reference is the harness shot of the same launch and camera):

    python -I render/tools/terrainreplay/export.py D:/c5-64bit-work/0167/captures/c5h-a_20261114-131137_frame1762.rdc \
        C:/c5-64bit-work/0182/exports/stockA --xml-dir C:/c5-64bit-work/0182/xml
    python -I render/tools/terrainreplay/manifest.py C:/c5-64bit-work/0182/exports/stockA C:/c5-64bit-work/0182/replay/stockA \
        --exe <build>/tools/terrainreplay/terrainreplay.exe --variant exe --variant nooverlay --variant nofow
    python -I render/tools/terrainreplay/compare.py C:/c5-64bit-work/0182/replay/stockA C:/c5-64bit-work/0182/exports/stockA \
        D:/c5-64bit-work/0167/captures/3701/live_stock_stimuli.png C:/c5-64bit-work/0182/compare/stockA --map 56x40 --mask-plot 50,13

The export reads the player's EXE (`%USERPROFILE%/c5-instances/c5h-c/install/CivilizationV_DX11.exe`, the default of
3601's exporter) for the shader hashes, and converts the capture with RenderDoc 1.47's `renderdoccmd` (about 1 min for a
100 MB Tiny capture). The replay runs on the GPU (`device hardware`, the default) in a few seconds per variant.

## What the comparison can and cannot show

- t1 in a capture is the target **at the frame's start**, i.e. the previous frame's finished overlay
  (`overlay-system.md` 3). At rest it equals the frame's own except for the animated selection ring: leave the selected
  unit's plot out with `--mask-plot`.
- The stock frame has everything drawn after the terrain: water and shore, rivers (on hex edges, where the grid and
  borders lie), trees, units, cities, resource models, the fog-of-war clouds over the edge of the seen area, banners
  and icons, the interface. The replay has none of these, so a difference there is not the replay's. `compare.py`
  separates what it can from data (land by world z, seen plots by the `nofow` variant, edge plots next to an unseen
  one) and gives each plot's overlay pixels next to its non-overlay pixels as a control.
- The replay is this project's own D3D11 replay of the captured state, not RenderDoc's (qrenderdoc's replay is held by
  its debugger setting, `water-system.md` 6.2): its fidelity is shown against the stock frame, not assumed.
