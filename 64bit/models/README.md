# models/: the `.gr2` model reader without the Granny runtime (epic 3100)

A standalone x64 static library (`c5models`) that reads Civilization V's Granny 2 files (format 7, 32-bit,
Oodle1-compressed sections) into meshes, vertex formats, skeletons, bindings, materials, texture references and
animation track groups (parsed, not played), plus the tools and tests that prove it. Research:
`research/05-graphics-composition/model-reader.md` and `landmarks-and-models.md`.

## Build (one command)

```
python models/build.py --test            # Release; build tree build/models/Release in this checkout (git-ignored)
python models/build.py --config Debug --out <dir>
python models/build.py --asan --test     # MSVC AddressSanitizer (cl.exe); tree build/models/Release-asan
```

Build tree: `--out DIR`, else `$C5M_BUILD_DIR/<config>`, else `build/models/<config>` inside the checkout (about 8 MB).
Until issue 9025 (2026-11-11) the default was epic 3100's own folder `D:/c5-64bit-work/3100/build`, so every later
caller wrote into that epic's scratch; a team that wants its tree on the D drive passes `--out
D:/c5-64bit-work/<its id>/...` or sets `C5M_BUILD_DIR`.

clang-cl -m64 + lld-link + Ninja in the VS 2022 x64 environment, C++14, `/MD` (the server's toolchain, D15). No
third-party code except the Boost-licensed Oodle1 decoder port in `src/oodle1.cpp` (notice and origin kept there).
No dependency on `server/`, `proxy/`, `ipc/`, `render/` or `shell/`; the root `CMakeLists.txt` does not include it.

Tests that need game files read them from `C5M_SAMPLES`, the root of an extraction made with
`models/tools/pyref/extract.py` (convention: `F:/c5-64bit-work/3100/extract` since ticket 0181, D133; `build.py` uses
the first existing of that folder, `C:/c5-64bit-work/3100/extract` and the 2026-11-10 extraction kept on D: as storage,
`D:/c5-64bit-work/3100/extract`, read only, when `C5M_SAMPLES` is not set). A test that cannot read a sample prints `SKIP`, runs everything
else and exits 77, which ctest lists as "Skipped" (since issue 9025: before, a test exited 77 only when no check at
all had run, so a run without the samples showed "3 of 3 passed"). `build.py --test` then fails with exit 2, because a
run with skipped parts is not a full run; `--allow-skip` accepts it on a machine without the game files. The
synthetic tests (built-in byte buffers, all negative checks that do not need a real Oodle1 stream) and
`test_hostile` (crafted files, built in memory) always run.

`test_hostile` holds the crafted files of bug 0093 (a 5,000-deep chain of type definitions, vertex types of 0 bytes,
vertex bytes shorter than count x stride, sections declaring 1 GiB from nothing, an allocation larger than the process
may commit; since issue 4000 also bone indices shorter than the bone weights, thousands of references to one mesh,
overlapping type definitions, one array walked through thousands of types, one long string behind every slot of a
string database). It runs each case in a child process of its own inside a job object (memory cap, 20 s), so that a
crash or a hang shows as that case's exit code or "timeout", and prints one line per case with its time and peak
memory. Limits the reader enforces: `kMaxExpandRatio`, `kMaxExpandedTotal`, `kMaxTypeNesting` and the per-file read
budget `kMaxBuiltBytes` / `kMaxReadSteps` (error `ReadBudget`) in `include/c5models/gr2.h`; the reasons are in
`research/05-graphics-composition/model-reader-verification.md` sections 11 and 13.

## Layout

| Path | What |
|---|---|
| `include/c5models/gr2.h` | public API: header, Oodle1, string databases, typed object access, model structures (4410: each bone's track-mask weights, `Bone::track_masks`, unit-controller.md 13.3), geometry checks, canonical summary |
| `src/` | the library (`oodle1.cpp`, `gr2_file.cpp`, `gr2_model.cpp`, `checks.cpp`, `summary.cpp`, art resolution) |
| `tests/test_*.cpp` | one ctest executable each; `testing.h` is the harness, `gr2_writer.h` writes small Granny files for the tests |
| `include/c5models/art.h` | art resolution: `FileProvider` / `DirectoryProvider`, XML scanner, `.fxsxml`, `WonderArtInfo`, database tables (`winsqlite3.dll` or TSV), the landmark / wonder / unit chains |
| `tools/<name>/` | one executable per folder: `gr2sweep` (sweep and canonical summaries), `modelshot` (rest-pose pictures), `artresolve` (art rows to files, `--all`, `--survey`) |
| `tools/gr2sweep/` | `gr2sweep --manifest <tsv> --out cpp.jsonl --checks checks.tsv --jobs N`, or `--file X.gr2 [--gsd ...]`; start it through `perf/quietlock/quietlock.py run` |
| `tools/modelshot/` | D3D11 WARP offscreen, own DDS reader, WIC PNG; run through `run_hidden.py` (hidden desktop); `make_pics.py` makes the evidence set; `pydump.py` + `--dump` draws from one-off 9012's Python reader for comparison |
| `tools/artresolve/` | `dump_tables.py` is the TSV fallback for the database |
| `tools/pyref/` | Python: `extract.py` (from `.fpk` archives into `F:/c5-64bit-work/3100/extract` and `gr2_manifest.tsv`), `gr2canon.py` (reference summaries with one-off 9012's reader), `compare.py`, `counts.py`, `test_compare.py` |
| `tools/posefit/` | Python (epic 0327, 4151): `posefit.py fit` fits captured t27 palette blocks (3908's `*.bones.json`) to a member's clips at one t (K3), `selftest`; `track.py` follows figures across frames, `blendfit.py` fits a two-clip cross-fade, `summarize.py` writes the evidence CSVs, `compare.py` sets C++ and Python fits side by side, `make_anim_reference.py` writes the C++ test's reference, `test_posefit.py` (pytest; `POSESAMPLE` = the built tool) (`research/05-graphics-composition/unit-playback.md` 1, 5) |
| `tools/curvecensus/` | Python (epic 0327, 4151): curve formats and degrees of every unit member's clips, by database member (`unit-playback.md` 2) |
| `include/c5models/anim.h` | (issue 4151) the pose sampler: curve decoders (formats 1-5, 8-11, 17, 18; the rest refused), de Boor degree 0-3, clip binding, local / world poses, skinning rows, the EXE's palette rows (`src/anim_curves.cpp`, `src/anim_pose.cpp`; `unit-playback.md` 5); test `tests/test_anim.cpp` against `tests/anim_reference.h` (written by `tools/posefit/make_anim_reference.py` from the Python reference) |
| `tools/posesample/` | (issue 4151) `posesample`: a model's skinning or palette rows at given times from a clip, for `tools/posefit/posefit.py fit --sampler cpp` and `compare.py` |
| `include/c5models/unit_mesh.h` | (issue 4152) a unit mesh converted into the EXE's 28-byte (skinned) or 20-byte unit vertex stream (K1, `research/05-graphics-composition/unit-playback.md` 3); since ticket 0447 by the EXE's load rule (`unit_basis`, Granny's half conversion, the float weight rule; `unit-renderer.md` 10.9); test `tests/test_unit_mesh.cpp` |
| `tools/unitmesh/` | (issue 4152) `k1_probe.py`: our stream from a `.gr2` against a capture export's buffers, per element; `k1_scan.py`: every mesh of a `.gr2` against the draws of an export; `k9_pairs.py`: the unit programs of exports named; `test_k1_probe.py` |
| `tools/unitreplay/` | (issue 4152) Python only, the replays themselves run on `render/tools/terrainreplay` (its 4152 directives): `unit_manifest.py` (manifests of unit draws from an export), `frame_start.py`, `k2_compare.py`, `base_vs_task.py` (replayer before/after, bit-equal outputs), `draw_units.py` (a unit drawn with our meshes, compared); `test_unitreplay.py` (WARP) |
| `include/c5models/logic.h`, `tools/logicdump/` | (issue 4402, epic 0374) the unit logic file readers (`.fsmxml`, `.ftsxml`, `.dge`; `src/logic_xml.cpp`, `src/logic_dge.cpp`) and their canonical dumps (`src/logic_dump.cpp`); test `tests/test_logic.cpp`; `logicfiles.py` copies the files a database reaches (or every copy) to a work folder, `logicdump.exe` and `logicdump.py dump` write the C++ and Python dumps, `logicdump.py compare` hashes them (DoD B, `research/05-graphics-composition/unit-controller.md` 12) |

Commands of the 2026-11-11 sweep: `python -I models/tools/pyref/extract.py` (see its header), `python -I
models/tools/pyref/gr2canon.py --manifest D:/c5-64bit-work/3100/extract/gr2_manifest.tsv --out py.jsonl --jobs 12`,
`gr2sweep --manifest ... --out cpp.jsonl --checks cpp_checks.tsv --jobs 12`, `python -I models/tools/pyref/compare.py
py.jsonl cpp.jsonl`, `python -I models/tools/pyref/counts.py cpp.jsonl`.

## Extraction layout (`C5M_SAMPLES`)

`<root>/fpk/<ArchiveName without .fpk>/<entry name>` for every `.gr2`, `.gsd`, `.fxsxml`, `.xml`, `.dds`, `.ddtxml` and `.ftsxml` entry of
the install copy's `Resource/Common/*.fpk` and `Resource/DX9/*.fpk`. Loose files (`Assets/`, `MODS/`) are read in
place from the install copy, read-only. The FPK reader of epic 2900 (`render/`) replaces this extraction in the
server: the library takes bytes, never paths.

## Canonical summary (C++ `canonical_summary()` and the Python reference write the same thing)

One JSON object per line and file. Labels: `fpk:<Archive>.fpk/<entry>`, `install:<path under the install root>`,
`mods:<path under MODS>` (forward slashes). String database: a file inside an archive uses that archive's `.gsd`; a
loose file whose header field 0x58 is non-zero uses the union of all archive databases. A NULL string is `""`.

| Key | Value |
|---|---|
| `file` | label |
| `endian` | `"LE"` or `"BE"` |
| `tag` | header type tag (integer) |
| `sdb` | header field 0x58 (string database CRC; 0 = inline strings) |
| `sections` | `[[expanded_size, crc32 of the decompressed bytes], ...]` for every section in header order |
| `unresolved` | count of string slots that neither a fix-up nor the database resolved |
| `textures` | root `Textures[].FromFileName`, reduced to the lower-case file name |
| `materials` | `[{"name", "shader": ExtendedData.ShaderSet or "", "maps": [[Usage, lower-case file name of Map.Texture.FromFileName or ""]]}]` (root `Materials` order) |
| `skeletons` | `[{"name", "bones": count, "parents": [ParentIndex, ...]}]` |
| `models` | `[{"name", "skel": skeleton name or "", "meshes": [mesh names]}]` |
| `meshes` | `[{"name", "v": vertex count, "stride", "fmt": "Type:Name:count,..." (count = array size or 1), "i": index count, "isz": 2/4/0, "imax": largest index or -1, "groups": [[MaterialIndex, TriFirst, TriCount]], "bones": [BoneBindings[].BoneName], "mats": [MaterialBindings[].Material.Name], "bbox": [minx, miny, minz, maxx, maxy, maxz] of Position or null, "morphs": count}]` |
| `tgs` | root `TrackGroups`: `[{"name", "tt": transform tracks, "vt": vector tracks, "xt": text tracks, "curves": {"P:D3K16uC16u:10:2": n, ...}}]`; P/O/S = Position/Orientation/ScaleShear curve; name = the curve type's first member name without `CurveDataHeader_`, then `Format`, `Degree`; NULL curve data not counted |
| `anims` | `[{"name", "dur", "step", "over", "tgs": [track group names]}]` |

Definitions fixed by `gr2canon.py` (issue 3102) and matched by `src/summary.cpp`: `unresolved` counts distinct
string slots reached by a full walk of the object tree (type names included), only in files with 0x58 != 0; NULL
entries of reference arrays are written in their empty form, never skipped; `stride` is 0 for an untyped vertex
array; `bbox` skips NaN per axis and writes non-finite values as `NaN`, `Infinity`, `-Infinity`; archives without a
`.gsd` (EffectData, MiscData) have `NONE` in the manifest.

The comparison (`tools/pyref/compare.py`) parses both sides and compares every float as float32 (exact), all other
values exactly. A file that fails to parse is a line `{"file": label, "error": "<Err name>: <text>"}`.
