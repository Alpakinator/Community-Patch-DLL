# Independent verification of the C++ model reader (epic 3100), one-off 9021

Written 2026-11-11 by one-off 9021, an agent that did not build epic 3100. Subject: `models/` at `main` 27301b2 (same
`models/` and `model-reader.md` as the merge 8daab47). Every number below was measured in this session unless it
says "their tool". Offline only: no game instance, nothing installed, game files read from the project's copies
(`D:/c5-64bit-work/3100/extract`, read only; c5h-b's install copy, read only). Mutated files are copies under
`D:/c5-64bit-work/9021/`. Scripts and outputs: `evidence/9021/`.

Marks: [C] measured here (dump, run or own reader); [I] inferred. "Their tool" = the team's own program rerun.

## 1. Verdict per claim

| # | Claim (epic 3100) | Verdict | Evidence (section) |
|---|---|---|---|
| 1 | reads 12,760 of 12,761; the big-endian file refused with `DecompressCorrupt`, written up | **confirmed by own measurement** (40-file sample with own code; full count by their tool, rerun byte-identical) | 3 |
| 2 | canonical summary = Python reader on all 12,760, section CRCs included; the totals | **confirmed**: totals by own recount (every number reproduced); equality by own comparator against their `py.jsonl`; section bytes of 40 files by own Python decompression. "13 vertex formats" includes the empty format | 3 |
| 3 | header 0x28 = zlib CRC-32 of 0x68..end, all 12,761 pass | **confirmed by own measurement** (12,785 files incl. `.gsd`; a flipped byte gives `BadCrc`). The CRC does not cover bytes 0-0x67 | 3 |
| 4 | Oodle1 port from nwn2mdk, Boost licence and origin kept, every read/write bounds-checked; no Granny runtime anywhere | **confirmed**: notice, origin and licence checked against upstream; no binary, no Granny module loaded (module lists). No decoder memory error in 86 hostile files or under ASan fuzzing (section 4) | 4, 5 |
| 5 | a negative test with its own code for every check; 300 random streams and 40 corruptions refused without a crash | **confirmed as stated** (their tool); **but the reader as a whole is not safe on hostile input**: stack overflow, null-pointer read, uncaught `bad_alloc` and a 4-billion-step loop from crafted files of 31-114 KB, and an out-of-bounds read inside the team's own negative test under ASan | 4 |
| 6 | every game file passes the geometry checks; 18 VP files fail on real data faults (Embassy, EthiopiaCompBow, missing bones) | **confirmed by own measurement** for the three named files; "every game file" by their tool (rerun identical) | 8 |
| 7 | art rows resolve for landmarks, wonders, unit members | **confirmed by own measurement**: 15 rows of my choice (8 touched by CP/VP) resolved by hand agree with `artresolve`; mount order: the evidence supports the team's doubt (mods should win) | 6 |
| 8 | rest-pose pictures, WARP, hidden desktop; 11 + 6 recognisable; C++ = Python pixel-identical; placement rules | **confirmed**: 17 of 17 rerendered byte-identical; the comparison fails on broken placement or bones; hidden desktop and no focus change measured; my 3 pictures match the game by eye (one only in parts). The C++ = Python identity itself was not rerun (their tool) | 7 |
| 9 | the bone-name bug was found through the pictures; `test_gr2` now checks bone transforms | **confirmed, weakly**: `test_gr2` fails on the original bug; it passes when translations are read from the wrong offset, which only the pictures catch | 7 |

**Defects found** (for the MC to file; severity as I judge it). Filed as bug 0093. **All eight fixed or explained
2026-11-11 by one-off 9025 (issue 9025): the "Fixed" line under each row, details, limits and tests in section 11.**

| Id | Severity | Defect | Where | Proposed fix |
|---|---|---|---|---|
| V1 | high (crash of the reading process on a crafted or corrupt file, e.g. a broken mod); **fixed 2026-11-11 (9025): depth limit `kMaxTypeNesting` = 64 (game maximum 8), `TypeTreeCorrupt`; test `test_hostile` v1_chain_5000, v1_chain_at_limit, v1_chain_over_limit** | Stack overflow (0xC00000FD) on a type tree whose definitions reference each other in a chain of 1,500 or more (114 KB file) | `RawFile::typedef_at`, `src/gr2_file.cpp` 323-376: recursion per referenced definition, only the count is capped | carry a depth counter (member or argument) and throw `TypeTreeCorrupt` above a fixed depth (choose it from the deepest game file; not measured here) |
| V2 | high; **fixed 2026-11-11 (9025): `Object::array` refuses elements of 0 bytes with a count (`ObjectOutOfRange`), `check_geometry` refuses weights/indices outside the vertex; tests v2_zero_stride_skinned, v2_zero_stride_count_max, v2_check_geometry_zero_stride** | Access violation (0xC0000005) in `check_geometry` on a vertex type of size 0 that still has members named `BoneWeights`/`BoneIndices` (vertices vector empty, pointer arithmetic from `data()`); and with a large count a 4.3e9-step loop in the summary's bounding box | `src/checks.cpp` 45-49; `Object::array` (`src/gr2_file.cpp` 551-586) accepts any count for a 0-byte element | refuse arrays whose element size is 0 and count > 0 (`ObjectOutOfRange`); in `check_geometry`, `continue` after the "vertex bytes != count x stride" violation instead of reading the vertices |
| V3 | medium; **fixed 2026-11-11 (9025): per-vertex checks skipped after the size violation, `Mesh::read` bounds-checked; test v3_short_vertex_bytes, and `test_gr2` passes under MSVC ASan** | Out-of-bounds read in `check_geometry` when vertex bytes < count x stride: it reports the violation, then reads every vertex (`Mesh::read`, weights pointer). ASan flags the team's own test `tests/test_gr2.cpp` 227-230; release passes silently | `src/checks.cpp` 26-49 | same `continue` as V2 |
| V4 | medium (a 31 KB file asks for 1 GiB per section, up to 64 GiB; abort when allocation fails); **fixed 2026-11-11 (9025): expanded <= 256 x stored per Oodle1 section and <= 256 MiB in all (`BadSectionTable`); failed allocations are `OutOfMemory` (new code); tests v4_amplification, v4_total_budget, v4_out_of_memory** | `std::bad_alloc` thrown by `out.assign(dst_size)` (`src/oodle1.cpp` 262) and the section vectors in `RawFile::load` escapes: `read_gr2` calls `f.load` outside its `try` (`src/gr2_model.cpp` 160). Under a 3 GiB job cap: exit 0xC0000409 | `src/gr2_model.cpp` 157-161, `src/oodle1.cpp` 262 | move `f.load` inside the `try`, catch `bad_alloc` in `load`; refuse expanded sizes above e.g. 64 x stored + 64 KiB (largest ratio in the game: 39.5; no game file has a section stored empty but expanded non-empty) and a total above a set budget |
| V5 | low; **fixed 2026-11-11 (9025): exit 77 when any part skipped; `build.py --test` fails on a skip unless `--allow-skip`** | `ctest` reports "3 of 3 passed" when `C5M_SAMPLES` is missing; README and `build.py` say the tests count as skipped | `models/tests/testing.h` 40, `models/README.md` 20-21 | return 77 whenever any part was skipped, or fix the text |
| V6 | low; **fixed 2026-11-11 (9025): exact bit patterns of two warrior bones and three academy placements; mutB fails 11 checks, mutP 6** | `test_gr2`'s bone check passes with wrong translations (mutB); placement is not checked by any ctest test | `tests/test_gr2.cpp` 288-298 | compare a few known bone transforms and one `InitialPlacement` of `anc_academy.gr2` exactly |
| V7 | low (process); **fixed 2026-11-11 (9025): `--out`, else `$C5M_BUILD_DIR/<config>`, else `build/models/<config>` in the checkout** | `models/build.py`'s default build tree is `D:/c5-64bit-work/3100/build/<config>`, the epic's own folder; later users write into it | `models/build.py` 69-74 | default to `build/models/<config>` in the repository or require `--out` |
| V8 | info; **explained and fixed 2026-11-11 (9025): only the PE/COFF TimeDateStamp (file offset 0x80) differed; `/Brepro` makes the builds byte-identical (one checkout path)** | Two clean builds give different executables (not reproducible) | build | none needed now |

Research corrections are in section 9 (none of them changes a number of epic 3100).

## 2. Build from a fresh clone (claim: one command, /W4 /WX, tests pass)

Fresh `git clone <this repository's GitHub URL> D:/c5-64bit-work/9021/clone` (27301b2; `models/`
unchanged since 8daab47), `C5M_SAMPLES=D:/c5-64bit-work/3100/extract`, then the documented command with an own build
tree (`python models/build.py --test --out D:/c5-64bit-work/9021/build1`, again with `build2`), each through
`quietlock.py run` [C]:

| Run | Wall | Compiler | Warnings | ctest | Checks (direct runs) |
|---|---|---|---|---|---|
| 1 (2026-11-11 01:49, empty tree) | 23 s | clang-cl 19.1.7, /W4 /WX | 0 | 3 of 3 passed | `test_art` 177, `test_gr2` 71, `test_modelshot` 39; 0 failures, 0 skipped parts |
| 2 (01:50, empty tree) | 21 s | same | 0 | 3 of 3 passed | same |

The two builds' executables differ byte for byte (not a reproducible build; timestamps/PDB ids, not examined
further). [Explained 2026-11-11 by 9025, section 11: the only differing field is the COFF TimeDateStamp; Release
builds carry no PDB reference; fixed with `/Brepro`.] Without `C5M_SAMPLES` the tests still exit 0: `test_art` 155 checks, `test_gr2` 42 checks, each printing
"(some parts skipped)", and ctest reports **3 of 3 passed**, not "skipped" as `models/README.md` says (section 9).
`test_modelshot` renders on a thread attached to a desktop the test creates (`CreateDesktopW` + `SetThreadDesktop`,
`tests/test_modelshot.cpp` 187-205), so the run did not touch the interactive desktop.

## 3. Sweep and agreement with the Python reader (claims 1-3)

**Rerun of their tool [C].** `gr2sweep --manifest D:/c5-64bit-work/3100/extract/gr2_manifest.tsv --jobs 12` from the
fresh build (2026-11-11 ~01:52, through `quietlock.py run`): 12,761 files, 1 error, 18 files with geometry
violations, 77.5 s wall (cold cache, shared machine; not a speed figure). `cpp.jsonl` and `cpp_checks.tsv` are
**byte-identical** to the team's (`D:/c5-64bit-work/3100/sweep/`).

**Totals recounted with own code** (`count_claims.py`, not their `counts.py`) from my `cpp.jsonl` and from the
team's Python-reference `py.jsonl`; both give the same line [C]: 12,761 files, 1 error
(`MiscData.fpk/cityruins_improved.gr2`, `DecompressCorrupt` in C++, "big-endian file not supported" in Python),
**14,656 meshes, 5,812,896 vertices, 5,348,659 triangles, 13 vertex formats, 16 curve formats, 0 unresolved strings,
largest skeleton 1,097 bones (`Expansion1LeaderModels.fpk/huns_atilla.gr2`)**, 16-bit indices in 14,353 meshes,
32-bit in 258. Every number of claim 2 is reproduced. Note: one of the 13 "vertex formats" is the empty format
(meshes with no vertex data, e.g. `LeaderModels.fpk/gandhi_lights.gr2`), so there are 12 real vertex layouts.
Own equality check (`eq_summaries.py`, floats compared as float32, not their `compare.py`): 12,760 of 12,760 summaries
equal, 0 different, same file set [C]. This compares against the team's `py.jsonl`; I did not rerun the 324 s
Python sweep, so the Python side of this line is their output. The sample below is my independent check of it.

**Own check of 40 files** (`verify40.py`, seed 9021; own header, section-table and CRC code, decompression with
one-off 9012's Python Oodle1 port, own minimal type-tree walker that finds members by name or by name hash) [C]:
forced picks = the largest skeleton (`huns_atilla.gr2`), the mod files `Embassy.gr2`, `Lalibela.gr2`,
`EthiopiaCompBow.gr2`, one file per vertex format (11 more files: Real32 and Real16 layouts, skinned and rigid, the
empty format, two VP mod layouts), then random files to 40 (mostly animation files, as the install is). Result:
**40 of 40** pass every check: magic, version 7, total size = file size, CRC, section table inside the header,
every section and fix-up/marshalling table inside the file, expanded sizes, and **the size and CRC-32 of every
Python-decompressed section equal the C++ reader's** (so the two Oodle1 decoders agree byte for byte on these 40);
for 27 files (17 with meshes, 112 meshes in all) my walker's per-mesh vertex and index counts equal the C++ summary.
Output `evidence/9021/verify40.out.txt`.

**Claim 3, the header CRC, on every file with own code** (`crc_all.py`) [C]: for all 12,785 files (12,761 `.gr2` +
24 `.gsd`) field 0x28 = zlib CRC-32 of bytes 0x68..total size, total size = file size, the big-endian file
included; 0 mismatches. The alternatives do not fit: from 0x58 (nwn2mdk's `sizeof(Header)`) 0 files, from 0x20 0
files. One flipped byte in the body of a copy of `warrior.gr2` changes the CRC (0x60696304 -> 0x45a4aaa2) and the
library refuses the copy with `BadCrc` [C]. **The CRC does not cover the first 0x68 bytes**: a copy with byte 0x5C
flipped (a reserved header word) reads without any error, and flipping byte 0x30 (section count) is caught only by
the section-table check (`BadSectionTable`), not by the CRC. Header fields are guarded by their own range checks,
not by the CRC (see section 4).

**Census (own, `compression_census.py`)** [C]: every non-empty section of every `.gr2` is compression 2 (28,661
sections; none stored empty with a non-zero expanded size); 73,427 sections are empty (stored and expanded size 0),
8 of them marked compression 0. Largest expanded/stored ratio in the game data:
39.5 (`Assets/UI/Overlays/CenterOneTurn.gr2` section 6).

## 4. Hostile input (claims 4-5)

**Their negatives rerun [C, their tool]:** `test_gr2` 71 checks pass, each named error code appears (Truncated,
BadMagic, BadHeader, BadCrc, BadSectionTable, UnsupportedCompression, FixupOutOfRange, ObjectOutOfRange,
TypeTreeCorrupt, BadStringDatabase, GeometryCheck, DecompressCorrupt), the 300 random streams and 40 one-byte
corruptions included.

**Own hostile files** (`attacks.py` writes them, `run_attacks.py` runs `gr2sweep --file` on each in its own process
inside a job object with a 3 GiB memory cap and a 60 s timeout; release build of the fresh clone; every file except
the CRC case has a recomputed valid CRC). 86 cases plus 8 chain depths, 2026-11-11 01:52-01:58 [C]
(`evidence/9021/attacks_release.tsv`):

| Group | Cases | Outcome |
|---|---|---|
| truncation of `warrior.gr2` at every header word (0-0x68), at the header end, at each section's first, second and last byte, at size-1; zero length | 40 | all clean: 38 `Truncated`, 2 `BadHeader` (cut at 0x68 and 0x1C7: header size larger than the file) |
| header fields: version, header size 0x20 / 0xFFFFFFFF, total size +1 / below the header, section count 0 / 65 / 0xFFFFFFFF, table offset 0xFFFFFFF0 (wraps), root type section/offset out of range; byte 0x30 flipped without fixing the CRC | 12 | all clean (`BadHeader`, `Truncated`, `BadSectionTable`; the flipped byte is caught by the table check, not the CRC) |
| section sizes that overflow 32 bits: data offset + size wraps, fix-up count x 12 wraps, marshalling count x 16 = 2^32, expanded size 0xFFFFFFFF; compression 1 and 0xFFFFFFFF | 6 | all clean (`BadSectionTable`, `UnsupportedCompression`) |
| compressed sizes that lie: stored size halved / 35 / +4,096, expanded size halved / doubled, stop0 > stop1, stop1 > expanded, both stops 0 | 8 | 7 clean `DecompressCorrupt`; stored size +4,096 reads and gives the **same summary as the original** (the decoder ignores trailing stored bytes; harmless) |
| fix-ups (on an uncompressed rebuild of the warrior, `rb_warrior.gr2`, whose summary equals the original's except the type tag I wrote): target = section size, size + 1, source 3 B before the end, target section = count | 4 | all clean (`ObjectOutOfRange`, `FixupOutOfRange` x3) |
| vertex count of mesh 0 set to 0xFFFFFFFF, 0x10000001 (count x stride wraps 32 bits), count + 1 | 3 | all clean `ObjectOutOfRange` |
| type tree: member 0 of the vertex type Inline of itself; Reference to itself | 2 | `TypeTreeCorrupt`; the reference cycle reads cleanly |
| string slot not in the database (0xDEADBEEF); a `.gsd` whose `Strings` count is 0x7FFFFFFF | 2 | clean (`unresolved` = 1; database refused, `ObjectOutOfRange`) |
| **memory amplification**: `warrior.gr2` with section 0 stored empty and declared 1 GiB expanded (31,048 B file) | 1 | reads "OK" after allocating **1,026 MB** in 3.3 s (an empty stored section expands to zeros by design) |
| **memory amplification x8**: all 8 sections stored empty, each declared 1 GiB (31,048 B file) | 1 | **abort, exit 0xC0000409** at the 3 GiB cap: `std::bad_alloc` from `out.assign(dst_size)` in `oodle1_decompress` / `RawFile::load`, which run outside every `catch`. Uncapped, the reader would try to commit 8 GiB from a 31 KB file (not run: it would disturb the shared machine) |
| **zero-stride vertex type**: every member of mesh 0's vertex type made `Inline` of an empty type (stride 0), names kept, so `BoneWeights` and `BoneIndices` are found | 1 | **access violation, exit 0xC0000005**: `check_geometry` reads the weights through `m.vertices.data() + v * stride + offset` with an empty vector (`src/checks.cpp` 47-49), i.e. a null pointer |
| the same with vertex count 100,000,000 / 0xFFFFFFFF | 2 | 1e8: **access violation** after 1.8 s; 0xFFFFFFFF: **no answer within 60 s** (the summary's bounding-box loop runs 4.3e9 times over a type of size 0; `Array` accepts any count when the element size is 0) |
| **deep type tree**: a chain of type definitions, each a `Reference` to the next | 3 (+8 depths, `chain_depth.py`) | **stack overflow, exit 0xC00000FD**, from depth **1,500 (a 114 KB file)**; 1,000 still reads. `RawFile::typedef_at` recurses once per referenced definition and caps only the count (100,000), not the depth (`src/gr2_file.cpp` 323-376) |

**Fuzzing (own driver `fuzz9021.cpp`, in-process, CRC recomputed after each mutation of 1-8 bytes or words)**:
8 configurations x 20,000 iterations = **160,000 mutated files, built with MSVC `/fsanitize=address`**, 2026-11-11
02:45-03:08 (`run_fuzz.py`, 4 processes, started through `quietlock.py run` while the lock was free; the slowest
configuration ran on after 2800 took the lock at 02:55) [C]: **no crash, no ASan report, no hang** in any
configuration. Inputs and outcomes (`evidence/9021/fuzz_asan.out.txt`): the compressed warrior mutated over the whole
body (94 % `DecompressCorrupt`), in section data, in the fix-up tables, in the header and section table; the
uncompressed rebuilds of the warrior, the academy and the VP file `EthiopiaCompBow.gr2` mutated in their section
data (where about 80 % of the mutants still read and run through `check_geometry` and the summary). So random
corruption is handled; the crashes above need structured input (a long reference chain, a zero-size vertex type,
huge declared sizes) that byte flips do not produce. [2026-11-11, 9025: each of these crafted inputs now ends in a
named error; section 11.]

**AddressSanitizer.** clang-cl 19.1.7's ASan runtime does not start on this machine (Windows 11 26100): every
instrumented test exits 139 before `main` with "interception_win: unhandled instruction" [C]. MSVC 14.43's
`cl /fsanitize=address` works (`build_fuzz.py`, same sources, /O1): under it `test_art` passes (177 checks, no
report) and **`test_gr2` stops with a container-overflow READ of 4 bytes** in `c5m::Mesh::read` called from
`check_geometry` (`src/checks.cpp` 45), from the team's own negative test "vertex bytes that do not match count x
stride" (`tests/test_gr2.cpp` 227-230, which shrinks `vertices` to 5 bytes and expects a violation): `check_geometry`
records the size mismatch and then reads every vertex anyway. In the release build the read stays inside the
vector's old capacity, so the test passes and nothing shows.

## 5. Licence, sources and run-time modules (claim 4)

- `models/src/oodle1.cpp` lines 1-31 [C]: names the origin (`nwn2mdk-lib/gr2_decompress.cpp`,
  `https://github.com/Arbos/nwn2mdk`, commit b6e67050be87b72403916aab891b4d571c2d7032), quotes that file's header
  (derived from xoreos-tools' `granny-decoder` branch, Boost Software License 1.0), lists the changes made, and
  carries the full Boost 1.0 text. Checked against the upstream file in the team's clone
  `D:/c5-64bit-work/3100/clone/nwn2mdk` (remote `https://github.com/Arbos/nwn2mdk`, HEAD b6e6705): the same header
  [C]. The nwn2mdk repository as a whole is Apache-2.0; this one file says Boost, and the port keeps Boost.
- Compatibility: the repository is GPL-3.0-or-later (decision D22, `LICENSE` is the GPL 3 text); Boost 1.0 is a
  permissive licence that the FSF lists as GPL-compatible [I: from the licence texts, not a legal opinion]. D34's
  conditions (source only, notice and origin kept, compatible licence) are met.
- No prebuilt binary is tracked anywhere in the repository (`git ls-files` has no `.dll`, `.exe`, `.lib`, `.obj`,
  `.pdb`); no file name or source line in the repository mentions `granny2` or a Granny DLL; the only
  `LoadLibrary` in `models/` is `winsqlite3.dll` from System32 (`src/art_db.cpp` 54) [C]. Link libraries of the tools:
  `modelshot` d3d11, d3dcompiler, dxguid, windowscodecs, ole32, user32 (`tools/modelshot/libs.txt`).
- Modules loaded at run time (`probe_modules.py`: each executable of the fresh build started suspended on a hidden
  desktop, its module list polled every millisecond until exit; 2026-11-11 02:28) [C]: `gr2sweep` 8 modules,
  `test_gr2` 8, `test_art` 11, `artresolve` 9, `test_modelshot` 31, `modelshot` 34. In every case the only module
  outside `C:/Windows/System32` is the executable itself; no module name contains "granny" or "oodle". Graphics and
  database modules seen: `winsqlite3.dll` (test_art, artresolve); `d3d11.dll`, `dxgi.dll`, `d3d10warp.dll`,
  `d3dcompiler_47.dll` (+ `windowscodecs.dll` for modelshot). Polling can miss a module loaded and unloaded within a
  millisecond; static imports and the one `LoadLibrary` (above) agree with the list.

## 6. The art resolution on rows of my own (claim 7)

Rows chosen here, resolved **by hand** (`art_by_hand.py`: plain SQL on the team's copy of c5h-b's
`Civ5DebugDatabase.db`, opened `mode=ro&immutable=1`; regular expressions over every XML file for `WonderArtInfo`;
a plain file listing of the three roots, every copy of each name shown) and then with **their tool**
(`art_tool_rows.py` runs `artresolve` once per row) [C], 2026-11-11 02:20-02:27:

| Kind | Row | By hand (database / XML -> `.fxsxml` -> `<Mesh file>`) | `artresolve` |
|---|---|---|---|
| landmark, **VP** (CP/VP delete the stock dig row and insert per-artifact rows) | ARCHAEOLOGICAL_DIG / RESOURCE_ARTIFACTS | `Archaeological_Dig.fxsxml` scale 0.8 -> Expansion2Models `archaeological_dig.gr2` | same |
| landmark, **VP** | ARCHAEOLOGICAL_DIG / RESOURCE_HIDDEN_ARTIFACTS | `hidden_antiquity_site.fxsxml` (VP mod folder) -> VP `hidden_antiquity_site.gr2` | same |
| landmark | MINE / IRON, Industrial, Constructed | `IND_Mine_Iron.fxsxml` -> ImprovementModels `ind_mine_iron.gr2` | same |
| landmark | PLANTATION / BANANA, Ancient, Constructed | `Plantation_MID_Banana.fxsxml` scale 0.96 -> `plantation_mid_banana.gr2` | same |
| landmark, **VP** (new improvement) | EMBASSY / RESOURCE_ALL (3 states) | `Embassy.fxsxml` scale 1.2 -> VP `Embassy.gr2` | same |
| landmark, **VP** (scale override 0.80) | KASBAH / RESOURCE_ALL | `Kasbah_HB.fxsxml` / `Kasbah.fxsxml` scale 0.8 -> Expansion2Models | same |
| wonder | BUILDING_PYRAMID | tag `THE PYRAMIDS` -> `civ5artdefines_wonders.xml` -> `Pyramid.fxsxml` -> WonderModels `pyramid.gr2` | same, fScale 0.05 |
| wonder | BUILDING_GREAT_LIBRARY | `THE GREAT LIBRARY` -> `great_library.gr2` | same |
| wonder | BUILDING_BOROBUDUR | `ART_DEF_BUILDING_BOROBUDUR` -> loose `Civ5ArtDefines_Expansion2_Wonders.xml` -> Expansion2Models `borobudur.gr2` | same, fScale 0.19 |
| wonder, **VP** (VP's Great Firewall uses the Terracotta Army art) | BUILDING_GREAT_FIREWALL | `TERRACOTTA ARMY` -> in two XML files (Expansion and Expansion2 "Inherited") -> `Terracotta_Army.fxsxml` in Expansion1Models and Expansion2Models, byte-identical | same, names both copies |
| wonder | BUILDING_PRORA_RESORT | `ART_DEF_BUILDING_PRORA` -> `prora.gr2` | same |
| unit member, **VP** (`UnitArtChanges.sql` 159 sets Model) | ART_DEF_UNIT_MEMBER_DESTROYER | `Wickes.fxsxml` scale 0.091 -> VP `Wickes.gr2` | same |
| unit member, **VP** (scale 0.06) | ART_DEF_UNIT_MEMBER_U_DANISH_LONGBOAT | `U_Denmark_Longboat.fxsxml` -> DenmarkModels | same |
| unit member | SPEARMAN | `Spearman.fxsxml` -> UnitModels `spearman.gr2` | same |
| unit member | MUSKETMAN | `Musketman.fxsxml` -> UnitModels `musketman.gr2` | same |
| unit member, **VP** (new unit) | DHANURDHARA | `EthiopiaCompBow.fxsxml` -> VP `EthiopiaCompBow.gr2` | same |

**15 of 15 rows (16 queries) agree** [C]. None of these rows has two different copies of its `.fxsxml` or `.gr2`, so
none of them depends on the mount order.

**The mount order.** The team chose archives first, then loose `Assets/`, then MODS, and flagged it as possibly
backwards. Evidence found here [I for the conclusion, C for the files]: 32 file names shipped in the CP/VP mod
folders also exist in the archives or the loose install, and **none is byte-identical** to the copy it shadows (e.g.
leader-background `.dds` files the Community Patch ships at 720,128 B against 1.4-7.5 MB in the archives, such as
`alexander.dds` 720,128 B against 7,560,128 B, listed with `import="1"` in `(1) Community Patch (v 151).modinfo`, which puts them into the game's virtual file system). A mod
ships a same-named, different file only to replace the original, so the mods' authors expect **mods to win over the
archives**: the team's order makes these copies lose. For loose DLC folders against archives the files give no such
direct signal. So the evidence supports the team's doubt for MODS; the EXE's actual order is still not measured
(it needs a live run or the EXE's file-system code).

## 7. Pictures (claims 8-9)

**Rerender [C, their tool].** `make_pics.py --exe <fresh build1>/modelshot.exe --pics D:/c5-64bit-work/9021/pics
--compare D:/c5-64bit-work/3100/pics` (2026-11-11 02:46, through `quietlock.py run`, 6.4 s): 17 pictures, 0 failed,
**all 17 "same"** as the team's full-size PNGs, and the 17 256-pixel copies are byte-identical (SHA-256) to the
committed `evidence/3100/pics/*.png` [C]. So the pictures are reproducible from a fresh clone.

**The comparison can fail [C].** Two deliberately broken copies of `models/` (`D:/c5-64bit-work/9021/mutP`, `mutA`),
built and rendered the same way:

| Broken input | Pictures that change (`--compare` says DIFFERENT) |
|---|---|
| `mutP`: the model's `InitialPlacement` never read (`src/gr2_model.cpp` 342 removed) | 5 of 17: academy, iron mine, city block, all city blocks, academy with stencil. The other 12 have identity placements (units, deer, elephant, Stonehenge, Uluru, lumber mill), so dropping it changes nothing there |
| `mutA`: the original bug of claim 9 (bone members looked up as `LocalTransform` / `InverseWorld4x4` only) | 13 of 17, every unit, resource, Uluru, the mine, the lumber mill and the city; the academy (identity bones) does not change |
| `mutB`: every transform's translation read from the rotation's offset (`src/gr2_file.cpp` 517) | 14 of 17 |

So a placement or bone-transform regression is caught by the picture comparison, as long as one of the pictures
has a non-identity placement or bone.

**Hidden desktop and focus [C].** Every `modelshot` run of `make_pics.py` and of `run_hidden.py` started on a
desktop named `C5MS_<pid>_<n>` (17 of 17 lines `+ [desktop C5MS_...]` in the batch log). `probe_modules.py` started
each executable on its own hidden desktop: a probe process there reports its thread's desktop as that hidden desktop,
and the foreground window of the interactive desktop was the same before and after every one of the 7 runs. No
window appeared on the owner's screen during this session as far as the foreground check can tell.

**Three pictures of my own against the stock screenshots of epic 3000 (interim data, read only), by eye only:**

| Picture (`evidence/9021/own_*.png`) | Stock screenshot | By eye |
|---|---|---|
| `WonderModels/pyramid.gr2`, HB* skipped | `i2-ground-truth/tiny/shot_v2_city_zoomin.png`, right of Madrid | **Matches in shape**: square pyramid on a walled square platform, small entrance building and ramp on one side. The capstone is brown-orange in my picture and pale green-gold in the game: lighting, specular or a different material, not judged |
| `WonderModels/statue_of_liberty.gr2`, HB* skipped | `i2-ground-truth/huge/shot_v2_city_zoomin.png`, below Memphis | **Matches**: green statue with raised torch, stepped dark pedestal with windows, star-shaped base, a ring of grey rocks (the `Rocks` mesh of model `HB_Statue_of_Liberty` is drawn and appears in the game too) |
| `MiscData/horse.gr2` (pasture; `ArtDefine_Landmarks` PASTURE/HORSE -> `Horse.fxsxml`), PL*/HB* skipped | huge shot, right edge: two horses inside a ring of stone walls | **Parts match, layout does not**: brown horse, stone wall pieces and trough are the game's, but the rest pose stacks the 8 wall pieces at the centre and the second horse inside the first, as the team found for the sheep; the places come from the idle animation |

The pictures confirm by eye what the team says: the reader gets geometry, textures and the rigid placement right;
fidelity (lighting, materials) and animated layouts are outside the static reader.

**Claim 9 [C].** `test_gr2` of `mutA` (the original bug) **fails** (71 checks, 2 failures: `t > 1.0f`,
`iw > 1.0f` at `tests/test_gr2.cpp` 296-297). But the check is coarse: `mutB`, which reads every bone's
translation from the rotation's offset (`src/gr2_file.cpp` 517, `a.off + 16` instead of `+ 4`), passes `test_gr2`
with 0 failures, and `mutP` (placement dropped) also passes it. So `test_gr2` catches "transforms left at identity"
and nothing finer; finer errors (mutB: 14 pictures differ) are caught only by the picture comparison, which is
not part of `ctest`.

## 8. Mod-file faults (claim 6)

Own reading (`modfaults.py`: own walker, Python decompression, nothing from `models/`) of the three files the claim
names, from c5h-b's MODS copy [C]:

| File | Mesh | Own reading | C++ `cpp_checks.tsv` |
|---|---|---|---|
| `Units/Dhanurdhara/EthiopiaCompBow.gr2` | `Material.001`, 1,097 vertices | **15 vertices with a NaN or infinite position**; 18 bone bindings, all present in the 33-bone skeleton | 15 non-finite positions |
| `Improvements/Embassy/Embassy.gr2` | `Bldg`, 1,079 vertices | **359 vertices whose four weights sum to 128** (the other 720 sum to 255), all bone indices 0; its one binding names `Point01`, which is not a bone of the file's skeletons (4 bones) | 359 weight sums, `Point01` not in skeleton `Box01` |
| `Improvements/MonolithicChurch/Lalibela.gr2` | `Box01` | binding `Point01` not a bone of the file (4 bones) | the same |

So the data faults are real. Whether they show in the game is another matter [I]: `Bldg` has a single bone
binding, so the engine probably draws it rigid and never uses the weights; a binding to a missing bone may fall back
to the model root. Only a live run can say. The claim's wording "every game file passes" is confirmed by the
byte-identical `cpp_checks.tsv` (section 3), which lists only `mods:` labels.

## 9. The research read as a stranger

`model-reader.md`, `landmarks-and-models.md` (the 3100 corrections), `evidence/3100/`, `models/README.md`, the
section README line, read 2026-11-11 by someone who did not watch the work:

- **Good:** every section is dated with absolute dates and clock times; workers and commands are named; sources
  of the open readers carry repository, commit and licence; trust grade C is stated for the sweep timings; the
  open questions (big-endian file, mount order, fidelity) are in a "Not verified" list; the section README lists
  `model-reader.md`; corrections in `landmarks-and-models.md` are made in place with a trace ("12,760 in the first
  version ... corrected 2026-11-11 by epic 3100", "was 2 files, one of them the big-endian one", the curve-format
  list).
- **Stated as confirmed but not, or weaker than written:**
  1. [Fixed 2026-11-11 by 9025: the tests now exit 77 when any part skipped, and README and `build.py` describe that.]
     `models/README.md` (and the docstring of `models/build.py`): without `C5M_SAMPLES` "those tests print SKIP and
     ctest counts them as skipped". Measured: all three tests exit 0 and ctest reports **3 of 3 passed**
     (`testing.h` returns 77 only when *no* check ran; 155 of 177 / 42 of 71 checks run). A run without the samples
     cannot be told from a full run by ctest's summary.
  2. Section 2.1, "a reader therefore detects any corruption of compressed data before decoding" is right for the
     data, but a reader may take it as "the file is CRC-protected": bytes 0x00-0x67 are not covered (section 3).
  3. Section 1 says the decoder is bounds-checked and the negatives show "none crashes"; true of the decoder, but the
     layers above it crash on structured input (section 4): the claim "every check has a negative test" holds, the
     implied "hostile input ends in a clean error" does not.
  4. Section 5 marks "11 required pictures plus 6 checks, all recognisable ... [C]": the pixel identity of the two
     readers is [C]; "recognisable" is a by-eye judgement, as `pics.txt` says.
  5. `pics.txt` rule 1 says the academy's bones have an identity "LocalTransform"; section 2.2 says the game's
     member is `Transform` and that `LocalTransform` was the wrong name. Same data, inconsistent name.
  6. "13 vertex formats" counts the empty format (section 3); "every section compressed" (landmarks 0.1) holds
     for every non-empty section; 8 empty sections are marked compression 0.
  7. `landmarks-and-models.md` header table kept "0x28 | CRC | per file" without pointing at the finding of
     `model-reader.md` 2.1.
- Corrections made in place by this verification (2026-11-11, each marked "one-off 9021"): `model-reader.md` 1
  (pointer to the defects), 2.1 (the CRC does not cover the header block), 2.4 (the empty vertex format);
  `landmarks-and-models.md` 4.2 (the 0x28 row). `models/` and `pics.txt` were not edited (not this one-off's files).
- Nothing else found that is marked [C] but rests on inference; the inferences (mount order, the big-endian
  decoder variant, texture-binding rule, fog sampler, the playback semantics of 6.4) are marked [I].
- [Fixed 2026-11-11 by 9025 (V7): the default is now `build/models/<config>` in the checkout.] The default build tree
  of `models/build.py` is `D:/c5-64bit-work/3100/build/<config>`, the epic's own scratch folder; a later team that runs the documented command writes into 3100's folder (COMMON rule 15 says each epic
  uses its own). This verification used `--out D:/c5-64bit-work/9021/build1` and `build2`.

## 10. Not verified

- The Python side of the full 12,760-file equality: I compared the C++ output with the team's `py.jsonl` by own code
  but did not regenerate `py.jsonl` (324 s Python sweep); my independent decompression covers 40 files.
- The memory-amplification case without a cap (8 x 1 GiB): not run, to keep the shared machine quiet; the capped
  run shows the abort path.
- The EXE's mount order (section 6), rendering fidelity against the game (the pictures here are by eye), and whether
  the VP mod-file faults show in the game (section 8): all need a live run or the EXE's code.
- The modelshot pictures were not checked for winding, handedness or scale against the EXE's camera.
- clang-cl's own AddressSanitizer: its runtime does not start on this Windows build; ASan results come from MSVC's
  `/fsanitize=address` on the same sources, not from the shipped clang-cl build.
- Big-endian decoding (section 2.3 of `model-reader.md`): not attempted.

**Machine load during this session (COMMON rule 16).** Every build, sweep and fuzz run started through
`quietlock.py run` with the lock free. Two runs started free and ran on after another team took the lock: the
mutation/ASan builds (02:21-02:24, 2800 took the lock at 02:21) and the slowest fuzz configuration (to 03:08, 2800
took it again at 02:55). Short runs under a minute (attack files 01:56-01:58 overlapping 7000's hold from 01:58, the
art queries and module probe 02:20-02:28, one 60 s fuzz trace at 03:00) ran while the lock was held, as the brief
allows for runs under a minute. 2800 and 7000 should know their series overlapped these.

## 11. The fixes of bug 0093 (one-off 9025, 2026-11-11)

Written 2026-11-11 16:30-16:56 EST by one-off 9025 (issue 9025), branch `worktree-agent-a0adf7eb09744090e`, from
`main` 389b84b. Every result below was measured in that session [C]; scratch under `D:/c5-64bit-work/9025/`, scripts
and outputs in `evidence/9025/`. Builds: clang-cl 19.1.7 Release (the documented build) and MSVC 14.43 `cl
/fsanitize=address` (new `python models/build.py --asan`). Game files: the extraction `D:/c5-64bit-work/3100/extract`,
read only.

### 11.1 Per defect

All crafted inputs are generated by the new test `models/tests/test_hostile.cpp` (with the writer
`tests/gr2_writer.h`, moved out of `test_gr2.cpp`); nothing binary is committed. Each case runs as a child process
of the test in a job object (512 MiB commit cap, 128 MiB for the out-of-memory case; 20 s) with error dialogs
off, so a crash shows as the case's exit code. "Unfixed" = the library sources of 389b84b with the new tests and the
new header declarations (`evidence/9025/make_unfixed.py`; `/WX` dropped for the old `err_name` switch); its output is
`evidence/9025/test_hostile_unfixed.out.txt`.

| Defect | Fix | Limit / rule and what it rests on | Test (case) | Unfixed reader | Fixed reader |
|---|---|---|---|---|---|
| V1 | depth counter in `RawFile::typedef_at` (RAII, so a throw unwinds it) | `kMaxTypeNesting` = 64. Deepest nesting the game's 12,761 files reach: **8** (`evidence/9025/depth_probe.py`, an instrumented sweep with the limit raised, `census_runs.out.txt`); 8x margin. The stack held 1,000 in 9021's run | v1_chain_5000; v1_chain_at_limit (64 reads); v1_chain_over_limit (65) | 5000: **exit 0xC00000FD** (stack overflow); 65: reads OK | `TypeTreeCorrupt: type definitions nested more than 64 deep`; 64 reads |
| V2 | `Object::array` refuses a non-zero count of 0-byte elements (both array kinds); `check_geometry` refuses `BoneWeights`/`BoneIndices` that do not fit in the vertex (`offset + min(count, 4) > stride`) and then skips the weight check | the game has **0** meshes with stride 0 and vertices (`census_facts.py`); after the fix every count-driven loop over an array is bounded by the section's bytes (`need()` covers count x size, size > 0) | v2_zero_stride_skinned (3 vertices); v2_zero_stride_count_max (0xFFFFFFFF); v2_check_geometry_zero_stride (a hand-made `FileInfo`) | **exit 0xC0000005**; **timeout after 20 s**; **exit 0xC0000005** | `ObjectOutOfRange: array of 0-byte elements: Vertices` (twice); `GeometryCheck: ... BoneWeights outside the vertex (offset 0, stride 0)` |
| V3 | `check_geometry` runs no per-vertex check when bytes != count x stride (index and group checks still run); `Mesh::read` returns 0 for a component outside the buffer | - | v3_short_vertex_bytes (1,000 vertices, no bytes); `test_gr2`'s own case (5 bytes) now also asserts exactly one violation | **exit 0xC0000005**; under ASan `test_gr2` stops with a **container-overflow READ of size 4 in `Mesh::read` from `check_geometry`** (`asan_unfixed_test_gr2.out.txt`; 9021's finding reproduced) | `GeometryCheck: vertex bytes 0 != count x stride`, 1 violation, `Mesh::read` gives 0; ASan clean |
| V4 | `parse_header` refuses, before any allocation, an Oodle1 section with expanded > 256 x stored (so an empty stored section expands to nothing) and a file whose sections expand to more than 256 MiB in all (`BadSectionTable`); `RawFile::load` (hence `read_gr2` and `add_gsd`), `oodle1_decompress` and `read_gr2` turn `std::bad_alloc` into the new code **`Err::OutOfMemory`** (the two old catch sites said `DecompressCorrupt` / `ObjectOutOfRange`) | ratio `kMaxExpandRatio` = 256: game maximum **39.5** (9021, section 3), 6.5x margin; no game section is stored empty with a non-zero expanded size. Total `kMaxExpandedTotal` = 256 MiB: game maximum **7,919,104 B** (`fpk:LeaderModels.fpk/04_light_test.gr2`; its largest section 5,363,636 B is also the game's largest), 33x margin. A mod file over a limit gets the named error with both sizes in the text | v4_amplification (2 sections stored 0 B, each declared 1 GiB; also through `add_gsd`); v4_total_budget (2 x 150 MiB inside the ratio); v4_out_of_memory (200 MiB from 1 MiB under a 128 MiB cap) | **exit 3** (the CRT's `abort()` after the uncaught `bad_alloc`; the test turns the fault report off, so not 0xC0000409 as in 9021's run); budget: `DecompressCorrupt` after allocating 300 MiB; out of memory: **exit 3** | `BadSectionTable: section 0 declares 1073741824 B expanded from 0 B stored (more than 256 times)`; `... sections expand to more than 268435456 B in all`; `OutOfMemory: section 0: Oodle1: out of memory for 209715200 B` |
| V5 | `tests/testing.h`: exit 77 whenever any part skipped (was: only when no check ran); `build.py --test` fails (exit 2) when ctest lists a skipped test, unless `--allow-skip`; without `C5M_SAMPLES` it uses `D:/c5-64bit-work/3100/extract` when that exists (read only) | COMMON rule 9: a run with skips counts as failed | `C5M_SAMPLES` pointing at a missing folder | ctest "3 of 3 passed" (9021) | `test_art`, `test_gr2` ***Skipped; `tests that skipped parts: test_art, test_gr2 ...; failing the run`, exit 2; with `--allow-skip` exit 0 |
| V6 | `test_gr2` compares bit patterns: warrior bones 1 (`bone_CLUB`, the largest translation) and 3 (translation, rotation, scale-shear, inverse-world translation), academy models 0, 1 and 13 (`InitialPlacement`) | values from 9012's Python reader (`evidence/9025/ref_transforms.py`, `.out.txt`), independent of `models/` | `test_gr2`, real part | mutB (translation read at +16): **11 failures**; mutP (placement not read): **6 failures** (`test_gr2_mutB.out.txt`, `test_gr2_mutP.out.txt`; the mutations applied to the fixed sources by `make_unfixed.py WORKTREE`) | 135 checks, 0 failures |
| V7 | `build.py`: `--out`, else `$C5M_BUILD_DIR/<config>`, else `build/models/<config>` inside the checkout (git-ignored, about 8 MB); `--asan` appends `-asan` | never another team's folder; a tree per checkout also keeps two clones from sharing one CMake cache | fresh worktree run (11.3) | wrote into `D:/c5-64bit-work/3100/build` | prints `build tree: <checkout>/build/models/Release` |
| V8 | explained: two clean Release builds of the same sources into two folders differ in **one byte per executable, the PE/COFF TimeDateStamp at file offset 0x80**; Release links write no debug directory, so there is no PDB GUID or path, and the build folder's path is not in the binaries. Fixed with lld-link `/Brepro` (CMakeLists, clang-cl build only) | - | `evidence/9025/repeat_check.py`, `repeat_check.out.txt` | 7 of 7 executables differ, at 0x80 only | 7 of 7 byte-identical (two clean builds into two folders of one checkout; not across checkout paths, 11.4); `/Brepro` adds a debug directory entry of the repro kind |

### 11.2 Same class, next to each defect (searched 2026-11-11)

- Recursion: `RawFile::walk` (`walk_all`) already stops at depth 200 and skips visited objects; `typedef_at` called
  from it is now limited too. `art_xml.cpp` elements stop at depth 256. `art_files.cpp`'s directory walk recurses per
  folder but skips reparse points, so it follows the real tree only. No other recursion in `models/src`.
- Loops driven by a count from the file: arrays (`Object::array`) were the only place where a count escaped its byte
  check (0-byte elements); `walk()`'s three array cases are bounded by `need()` per element or up front and skip types
  without pointers (every 0-byte type); index, bone, curve and string-database loops run over `Array`s; fixed-size
  member arrays are capped at 65,536 elements and types at 4,096 members. `canonical_summary`'s bounding box loops over
  `vertex_count` of a `FileInfo`: for a read file that is now bounded by the bytes; for a hand-made `FileInfo` each
  step is bounds-checked by `Mesh::read` (no crash, but the loop is not shortened) [I: not timed with a huge count].
- Allocations sized from the file: section buffers (now limited, V4); fix-up vectors (the table lies inside the
  file); vertex, index and curve copies (bounded by section bytes); the Oodle1 decoder's windows (at most 8,193
  mid-bit windows of a few entries each, from 23-bit parameters; under 1 MB [I: from the code, not measured]).
- Nothing of this class found outside `models/` (not searched beyond it).

### 11.3 Results

- **Census** (`evidence/9025/census_runs.sh`, `census_runs.out.txt`, through `quietlock.py run`): gr2sweep of all
  12,761 files with the fixed reader: 1 error (the big-endian file, as before), 18 files with geometry violations;
  `cpp.jsonl` and `checks.tsv` **byte-identical** to the sweep of the unfixed reader (SHA-256 `ecf24e5c...` and
  `287869a9...`), which is itself byte-identical to epic 3100's output in `D:/c5-64bit-work/3100/sweep/`.
- **Suite, plain** (clang-cl Release, samples present): ctest 4 of 4 passed, 0 skipped; `test_art` 177 checks,
  `test_gr2` 135 (was 71), `test_hostile` 10 cases, `test_modelshot` 39; 0 failures (`suite_counts.out.txt`).
- **Suite, MSVC AddressSanitizer** (`build.py --asan --test`): 4 of 4 passed, 0 skipped, no ASan report; the same
  counts except `test_hostile` with 9 cases: v4_out_of_memory is not built there, because ASan's `operator new` ends
  the process on a failed allocation instead of throwing `std::bad_alloc`. The positive control (unfixed sources under
  ASan) reports the V3 container overflow, so the runtime was active.
- **Fresh worktree**: a new worktree of commit cb012bd at `D:/c5-64bit-work/9025/fresh` (removed afterwards),
  `C5M_SAMPLES` and `C5M_BUILD_DIR` unset, `python models/build.py --test` with no arguments: tree
  `<checkout>/build/models/Release`, samples from the convention folder, ctest 4 of 4 passed, 0 skipped, exit 0
  (`fresh_worktree_run.out.txt`).

### 11.4 Not verified

- Repeatability of the Debug configuration (PDB GUIDs). Between two checkout paths the builds are **not**
  identical (`repeat_check.out.txt`, last block): the test executables embed source paths (`__FILE__` in `CHECK`) and
  differ in thousands of bytes; `gr2sweep` and `modelshot` differ in 12 bytes besides the timestamp, not examined.
  The repeatability shown is for one source path, as the build is used.
- The Python tools in `models/tools/pyref` and 9012's reader on crafted files (they read only game files; a deep
  type chain there would end in Python's `RecursionError`, not a crash) [I].
- `kMaxTypeNesting` against mod files: the game maximum is 8; a mod exported by another Granny version could nest
  deeper, and 64 leaves room for that [I].

## 12. Independent rerun against the fixed reader (tracker 4000, 2026-11-12)

Written 2026-11-12 20:54 - 2026-11-13 00:20 EST (all runs 23:20-00:15) by a worker of the bugfix team (tracker 4000),
which had no part in 9021 or 9025.
Subject: `models/` of `main` e7c0eec (the same `models/` as the merge of 9025, commit cb012bd), built from worktree
branch `worktree-agent-a106031d1be190b19`. Scratch and builds under `D:/c5-64bit-work/4000/0093/` (removed at the end; the crafted
files are regenerated by `gen_inputs.py`); scripts and text
outputs in `evidence/4000-0093/` (`run_stage.sh` names every command; each stage ran through `quietlock.py run --who
4000` with `C5_JOB_OWNER=4000`, crafted-input and fuzz stages inside a job memory cap, `capped.py`). Game files read
only (`D:/c5-64bit-work/3100/extract`, c5h-b's `EthiopiaCompBow.gr2`). No game, server or instance. Marks: [C]
measured in this session; [I] inferred. Nothing here is [L]: the reader is not part of a live game yet.

### 12.1 Results on V1-V8 (the fixes hold)

| Check | Build | Outcome |
|---|---|---|
| `python models/build.py --test --out D:/c5-64bit-work/4000/0093/build` (no other argument, `C5M_SAMPLES` and `C5M_BUILD_DIR` unset) | clang-cl 19.1.7 Release | ctest 4 of 4 passed, 0 skipped, exit 0; samples taken from the convention folder as documented (`build_test.out.txt`) [C] |
| V5: the same with `C5M_SAMPLES` pointing at a missing folder | same | `test_art`, `test_gr2` ***Skipped, "failing the run", **exit 2** (`v5_skip.out.txt`) [C] |
| 9021's `attacks.py` (regenerated, 86 cases) through 9021's `run_attacks.py`, unchanged, 1 GiB cap per case, 60 s | Release | **86 of 86 clean** (82 named errors, 4 OK); the 7 cases that crashed or hung in 9021's run (`amp_1g_x8`, `vtype_empty`, `vtype_empty_cnt_max`, `vtype_empty_cnt_1e8`, `tt_chain_5000/20000/99000`) now end in `BadSectionTable`, `ObjectOutOfRange` and `TypeTreeCorrupt`; `amp_1g_x1` (read "OK" after 1 GiB) is now `BadSectionTable`; every other case gives 9021's outcome (`attacks_release.tsv`, `compare_attacks_release.out.txt`) [C] |
| the same 86 | MSVC 14.43 `/fsanitize=address` (9021's `build_fuzz.py`, unchanged) | **86 of 86 clean, no ASan report**, same outcomes (`attacks_asan.tsv`, `compare_attacks_asan.out.txt`) [C] |
| 9021's `chain_depth.py`, unchanged (depths 250-4,000) | Release | all 8 exit 1 (named error), none 0xC00000FD (`chain_depth.out.txt`) [C] |
| own crafted cases, V1-V4 boundaries (`own_cases.py`, 53 of its 69 cases; table below; the other 16 are 12.2) | Release and ASan | **53 of 53 as expected** in Release and under ASan (`own_release.tsv`, `own_asan.tsv`) [C] |
| 9021's fuzz configurations, 9021's seed 9021, 20,000 per configuration (**160,000 inputs**, the inputs of 9021's run) | plain `/O1` | 0 crashes, 0 hangs, 86 s on 4 processes (`fuzz_plain.out.txt`) [C] |
| the same 8 configurations, new seed 40930, 32,000 per configuration (**256,000 inputs**), chunks of 2,000 (`run_fuzz_4000.py`), 1.5 GiB cap per process | MSVC `/fsanitize=address` (9021's `build_fuzz.py`) | **0 crashes, 0 ASan reports, 0 hangs**, 1,423 s on 4 processes, peak 583 MB per process; 78-82 % of the section-data mutants of the three uncompressed rebuilds still read and reach `check_geometry` (`fuzz_asan.out.txt`) [C] |
| the same 8 configurations, new seed 40931, 100,000 per configuration (**800,000 inputs**) | plain `/O1` | 0 crashes, 0 hangs, 309 s on 4 processes (`fuzz_plain2.out.txt`) [C] |
| `python models/build.py --asan --test --out D:/c5-64bit-work/4000/0093/build-asan` | MSVC `/fsanitize=address` (`build.py --asan`) | ctest 4 of 4 passed, 0 skipped, no ASan report, exit 0 (`asan_suite.out.txt`) [C] |

The V1-V4 boundaries, each a separate crafted file (all Release and ASan unless marked) [C]:

| Limit | Just inside | Just outside / far outside |
|---|---|---|
| type nesting 64 (`typedef_at`) | Reference chains of 1, 2, 32, 63, **64**: read OK; Inline chains of 63, **64**: OK | Reference chains of **65**, 66, 100, 250, 1,000, 1,500, 5,000, 20,000, 99,000 and Inline chains of 65, 5,000: `TypeTreeCorrupt: type definitions nested more than 64 deep`, at most 0.2 s and 16 MB |
| neighbours in `typedef_at` | 100,001 type definitions; 4,096 members; `Real32[65536]`; a type of exactly 2^24 B: OK | 100,002 definitions; 4,097 members; `Real32[65537]`; 2^24 + 1 B: the named `TypeTreeCorrupt` texts |
| 0-byte elements (`Object::array`) | stride-0 vertex type (with and without `BoneWeights`/`BoneIndices`) and count 0: OK, 1 mesh; count 0xFFFFFFFF without a data pointer: OK (the count is ignored) | counts 1 and 0xFFFFFFFF: `ObjectOutOfRange: array of 0-byte elements: Vertices`; root `ReferenceToArray` of an empty type, counts 1 and max: the same for `Meshes`; `ArrayOfReferences` count max: `array too large` |
| ratio 256 (`parse_header`) | 4,096 B stored with 1 MiB - 1 and exactly 1 MiB; 1 B with 256 B; 0 B with 0 B: past the limit (then `DecompressCorrupt` / `ObjectOutOfRange` on the zero data) | 1 MiB + 1; 257 B from 1 B; 1 B from 0 B: `BadSectionTable: ... (more than 256 times)` |
| total 256 MiB | 2 x 128 MiB on one shared 1 MiB, and 256 MiB - 1: past the limit (134 MB peak, then `DecompressCorrupt`); 64 uncompressed sections over one shared 4 MiB = exactly 256 MiB: **read OK, 263 MB peak** (the limit is really allocated) | 256 MiB + 1 in two compressed sections, in 64 compressed sections (16,388 B stored each, so only the total refuses), and 64 uncompressed sections + 4 B: `BadSectionTable: sections expand to more than 268435456 B in all` |
| `check_geometry` weights inside the vertex | `BoneWeights` u8x4 ending exactly at the stride: OK | (stride 0 above) |

Also measured: 64 sections sharing one fix-up table of 262,144 entries (3.1 MB file) read OK at a 198 MB peak, a
64-fold amplification of the table that the file size bounds; a linked list of 10,000 objects is walked to the depth
limit 200 and reads OK.

### 12.2 New defects of the same classes (not covered by the fixes)

Each input is written by `python -I -X utf8 evidence/4000-0093/own_cases.py --out DIR` (file names below) and run
with `run_cases.py` (gr2sweep `--file`, one process per case in a job object of 1 GiB, 60 s). The fixed reader
refuses nothing in these files, because each one stays inside every limit of section 11; what grows is the work and
memory per reference.

| Id (proposed) | Class | Input (own_cases.py) | Release | ASan | Where |
|---|---|---|---|---|---|
| V9 | V3 (out-of-bounds read) | `v3_bone_indices_shorter.gr2` (852 B): one mesh, 3 vertices of stride 18: `Position` Real32x3, `BoneWeights` UInt8x4 (64, 64, 64, 63), `BoneIndices` UInt8x2 last in the vertex | reads OK, silently | **heap-buffer-overflow READ of size 1, 0 bytes after the 54-byte vertex buffer**, `check_geometry` `src/checks.cpp`:64 (`asan_v3_bone_indices.out.txt`) | the weight loop reads `ix[k]` for `k < bw->count` (up to 4): `BoneIndices` shorter than `BoneWeights` reads up to 2 B past each vertex, past the buffer for the last one. 9025's guard checks each component against the stride with its own count, so both pass. Bound `k` by `min(bw->count, bi->count, 4)` |
| V10 | V4 (memory from a small file) | `m_shared_mesh_4096_refs.gr2` (1.1 MB): `Meshes` = 4,096 references to one mesh of 87,381 vertices (1 MiB) | **1 GiB cap reached in 0.5 s**, `OutOfMemory`; asks for 4 GiB [I: 4,096 x 1 MiB]; 64 references: 68 MB | not run (cap) | `read_gr2` copies the vertices once per reference (`copy_le`, `gr2_model.cpp` 278); indices, names and curves the same way |
| V10 | V4 | `t_suffix_types_1x4096.gr2` (328 KB): one run of 4,096 `String` members; its 4,096 suffixes are read as 4,096 overlapping type definitions = 8.4 M `Member` records | reads OK at a **643 MB peak**; 4 runs (`t_suffix_types_4x4096`, 1.3 MB): cap in 1.2 s, `OutOfMemory`; 1 run of 1,024: 40 MB | not run | `typedef_at` stores each overlapping definition in full; the limits (4,096 members, 100,000 types) allow 4 x 10^8 records |
| V10 | V4 | `w_walk_types_256_x_65536.gr2` (295 KB) and `w_walk_types_4096_x_65536.gr2` (787 KB): k one-`String` types, each walked over one shared array of 65,536 elements, string database CRC set so `walk_all` runs | 256: cap in 20.6 s, `OutOfMemory`; **4,096: exit 0xC00000FD after 16.5 s at the cap** (the stack could not commit a page [I]); 16 types: 106 MB | not run | `RawFile::walk` keeps one `visited_` entry per (address, type), k x n = 2.7e8 entries [I: about 100 B each from the 16-type case, so about 27 GB] |
| V11 | V2 (time) | `w_walk_suffix_1024_x_4096K.gr2` (4.3 MB): 1,024 suffix types walked over one 4 MiB array: about 1.1e9 string-slot reads | **timeout at 60 s** (256 KiB array: 11.8 s) | timeout (256 KiB: also timeout) | `walk` reads every `String` slot of every element for each type; without the 60-s stop the 4 MiB case reads OK after **358 s at a 647 MB peak** (`long_timing.out.txt`) |
| V11 | V2 | `g_gsd_long_string_65536x1M.gsd` (2.1 MB) as the `--gsd` of `warrior.gr2`: 65,536 `Strings` slots all pointing at one 1 MiB string | **timeout at 60 s** (4,096 slots: 13.9 s) | timeout | `StringDatabase::add_gsd` copies and hashes the string once per slot (64 GiB of copying); without the stop it loads after **219 s** (7 MB peak) |

Under a cap these end in `OutOfMemory`, a timeout or (once) 0xC00000FD; without a cap, as the server would read a mod
file, they take the machine's commit (V10) or minutes of CPU (V11). 9025's own search (11.2) looked at loops and
allocations sized by a count in the file; these are sized by the number of references to shared bytes, which no
count states. A fix in the spirit of 11: a read budget per file (bytes copied into `FileInfo`, `Member` records,
`visited_` entries, string bytes copied) measured against the game's maxima, refused with a named error.

### 12.3 Not verified

- Debug builds; the fuzz under clang-cl (its ASan runtime does not start here, section 4).
- Uncapped peaks of V10 (extrapolated, [I]); I did not let a case take the machine's commit.
- Whether any V10/V11 input could be made smaller; the sizes above are the first that showed the effect.
- The census of the game's 12,761 files with the fixed reader (9025 did it; not repeated).

## 13. The fixes of V9-V11 (tracker 4000, 2026-11-13)

Written 2026-11-13 00:18-01:00 EST by a worker of the bugfix team (tracker 4000), branch
`worktree-agent-a5dc6edfc6b311a53` from `main` 0388c13. The defects are those of section 12.2, which this section
closes [C]. Builds: clang-cl 19.1.7 Release (`python models/build.py --test`) and MSVC 14.43 `/fsanitize=address`
(`--asan --test`), trees under `D:/c5-64bit-work/4000/0093fix/` (removed at the end). Every build and run went
through `quietlock.py run --who 4000` (`evidence/4000-0093fix/run_stage.sh` names each command); the crafted inputs
run only inside `test_hostile`, which caps every case (job memory limit, 20 s) and prints one line per case with its
time and peak commit. Game files read only (`D:/c5-64bit-work/3100/extract`). Nothing here is [L].

### 13.1 Per defect

All inputs are built in memory by `models/tests/test_hostile.cpp` (the shapes and sizes of `evidence/4000-0093/own_cases.py`);
nothing binary is committed. "Unfixed" = the library sources of 0388c13 with the new tests and a compile shim
(`evidence/4000-0093fix/make_trees.py unfixed`), output `test_hostile_unfixed.out.txt`. Times and peaks: the per-case
lines of `suite_counts.out.txt` (fixed) and `test_hostile_unfixed.out.txt`, Release / ASan.

| Defect | Fix | Test case | Unfixed reader | Fixed reader |
|---|---|---|---|---|
| V9 | `check_geometry` reads bone index k only when k < `BoneIndices`' own count (`checks.cpp` weight loop) | `v9_bone_indices_shorter`: 3 vertices of 18 B, `BoneWeights` u8x4 (64,64,64,63), `BoneIndices` u8x2 (0,1) last, 4 bone bindings; bytes 0-1 of vertices 1 and 2 are 200, so a reader taking 4 indices reads 200 | Release: `check_geometry violation: 4 weighted bone indices past the bone bindings` (case fails); ASan: **heap-buffer-overflow READ of size 1 ... `checks.cpp:64` in `check_geometry`** | read OK, `check_geometry` clean, 0.0 s / 0.0 s |
| V10 | read budget, built bytes (13.2): `copy_le` charges every vertex/curve copy before it is made | `v10_shared_mesh_4096_refs` (1.1 MB: 4,096 references to one mesh of 87,381 vertices) | Release `OutOfMemory` at the 1 GiB cap (peak 1,023 MB, 0.5 s); ASan: `AddressSanitizer failed to allocate` at the cap | `ReadBudget: ... build more than 268435456 B (kMaxBuiltBytes)`; 0.1 s, peak 260 MB / 0.3 s, 353 MB |
| V10 | the same budget at a size that must still read | `v10_shared_mesh_64_refs` (64 MiB of copies) | read OK | read OK, 67,132,368 B built, 1,956 steps; peak 68 MB |
| V10 | `typedef_at` counts a definition's members first and charges its records (`sizeof(Member)` each) before building them | `v10_suffix_types_1x4096` (328 KB: 4,096 overlapping suffix definitions, 8.4 M records) | Release reads OK at a **643 MB** peak (case fails); ASan: allocation failure at the 1 GiB cap | `ReadBudget ... (kMaxBuiltBytes)`; 0.3 s, 260 MB / 3.8 s, 354 MB |
| V10 | walk visits and elements charged as steps, each visited key as 32 B; `visited_` is now an open-addressing set of 64-bit keys (was an `unordered_map` node per visit) | `v10_walk_types_4096_x_65536` (787 KB, 2.7e8 visits) | Release **exit 0xC00000FD** at the 1 GiB cap after 10.5 s; ASan timeout (20 s) at 850 MB | `ReadBudget: ... more than 16777216 read steps (kMaxReadSteps)`; 1.1 s, 202 MB / 3.5 s, 362 MB |
| V11 | steps per string slot walked; the walk checks a slot without building the string it used to return and drop (`RawFile::walk_string_slot`: same errors, same unresolved set) | `v11_walk_suffix_1024_x_4096K` (4.3 MB, 1.1e9 slot reads) | timeout at 20 s, Release and ASan (358 s uncapped, 12.2) | `ReadBudget ... (kMaxReadSteps)`; 1.3 s, 143 MB / 8.8 s, 316 MB |
| V11 | every string returned (`cstr`, a database hit) charged before the copy | `v11_gsd_long_string_65536x1M` (2.1 MB `.gsd`, through `StringDatabase::add_gsd`) | timeout at 20 s, Release and ASan (219 s uncapped, 12.2) | `ReadBudget: string database: ... (kMaxBuiltBytes)`; 0.9 s, 10 MB / 1.2 s, 347 MB |

### 13.2 The read budget

`include/c5models/gr2.h`: `kMaxBuiltBytes` = 256 MiB and `kMaxReadSteps` = 16 Mi (16,777,216) per read of one file
(`read_gr2`, `StringDatabase::add_gsd`); past either, `ParseError(Err::ReadBudget)` (new code, `err_name`
"ReadBudget"). `RawFile::charge_bytes` / `charge_steps` keep the totals in `LoadStats::built_bytes` / `read_steps`
(copied into `FileInfo::stats` at the end of `read_gr2`).

- Built bytes: type records (`sizeof(TypeDef)` + one map node + `sizeof(Member)` per member), every string returned
  or scanned (length + 1; an unresolved `#hash` 10 B), each new unresolved slot (64 B), each visited key (32 B), every
  vertex, index (4 B) and curve copy, and the model structures (`sizeof` of each `Material`, `TextureMap`, `Bone`,
  `Mesh`, `VertexComponent` + name, ... pushed). Cumulative, not live: a string built and dropped still counts, which
  is what bounds V11's 64 GiB of copying.
- Steps: each `walk()` call, each element of a pointer-bearing member and of an array of references walked, each
  `Array::at`, each index read, each type member read (+1 per definition), and each member look-up (`Object::has`,
  `Object::need`) at 1 + the number of members it scans, which bounds the quadratic look-ups over 4,096-member types.
- Game maxima, measured with both limits raised to 2^62 (`make_trees.py probe`, instrumented gr2sweep, all 12,761
  files and the 24 string databases, `probe.out.txt`) [C]: **8,296,788 built bytes** and **729,912 steps**, both
  `fpk:LeaderModels.fpk/04_light_test.gr2` (the file with the largest sections, 11.1); string databases 241,932 B and
  37,978 steps. Margins: **32x** (bytes) and **23x** (steps). 256 MiB is also `kMaxExpandedTotal`, so a hostile file
  holds at most about twice that (sections plus what is built); measured peaks of the crafted cases: 260 MB Release,
  362 MB ASan.
- A first cut with 64 Mi steps let the walk cases run 2.6 s / 2.3 s Release and over 20 s under ASan (both failed on
  the case timeout); the step limit came down to 16 Mi and the walk's per-slot string and per-visit map node went.
  With them the slowest crafted case is 1.3 s Release, 8.8 s ASan.

Unchanged by the fix (census below): which files read, every summary line, every violation. The pre-count in
`typedef_at` only measures (it stops at the first unknown type or the section's end), so a bad definition reports
the same error as before. Not a performance claim: the census took 5.6-5.7 s wall (9025: 5.4-5.5 s), one run each.

### 13.3 Results

- **Census** (`run_stage.sh census`, `census.out.txt`): gr2sweep of the fixed reader over all 12,761 files: 1 error
  (the big-endian file, as before), 18 files with geometry violations; `cpp.jsonl` and `checks.tsv`
  **byte-identical** to epic 3100's sweep `D:/c5-64bit-work/3100/sweep/` (SHA-256 `ecf24e5c...`, `287869a9...`, the
  hashes of 11.3) [C]. The probe tree's `cpp.jsonl` is identical too.
- **Suite, plain** (`build_test.out.txt`): ctest 4 of 4 passed, 0 skipped; `test_art` 177 checks, `test_gr2` 135,
  `test_hostile` 17 cases (10 of 9025, 7 new), `test_modelshot` 39; 0 failures (`suite_counts.out.txt`) [C].
- **Suite, MSVC ASan** (`asan_suite.out.txt`): ctest 4 of 4 passed, 0 skipped, no ASan report; the same counts with
  `test_hostile` at 16 (v4_out_of_memory not built there, 11.3) [C]. Positive control: the unfixed sources under
  ASan report V9's overflow (13.1), so the runtime was active.
- **Unfixed** (`test_hostile_unfixed.out.txt`): plain 17 checks, 6 failures; ASan 16 checks, 6 failures; the 6 are
  the six new hostile cases; the cases of 9025 and the 64-reference control pass [C].

### 13.4 Not verified

- 9021's attack scripts, the fuzz and `own_cases.py`'s full table (section 12) were not rerun against this fix; the
  V9-V11 inputs are covered by `test_hostile` only. An independent rerun is the next step of bug 0093.
- The 64 B and 32 B charged per map node and visited key are estimates for the MSVC x64 STL [I]; the measured peaks in
  13.1 are of the same order as the charged totals.
- Debug builds; the fresh-clone run of `build.py --test` with no arguments (the documented build ran with `--out`).
- Other inputs of the same classes: anything that repeats work per reference is now counted, but only the shapes
  above were built [I].

## 14. Independent rerun against the fixes of V9-V11 (tracker 4000, 2026-11-13)

Written 2026-11-13 02:44-03:35 EST by the verifier of section 12 (no part in the fix of section 13). Subject: `models/`
of team branch `worktree-agent-a32ee29770208a4f5` head 5779c01, merged into this verifier's branch
`worktree-agent-a106031d1be190b19`. Builds and scratch under `D:/c5-64bit-work/4000/0093r2/` (removed at the end);
the same scripts as section 12 (`evidence/4000-0093/run_stage.sh` with `R=D:/c5-64bit-work/4000/0093r2`), every run
through `quietlock.py run --who 4000`, crafted and fuzz runs inside a job memory cap. Text outputs in
`evidence/4000-0093/r2/`. Marks: [C] measured here, [I] inferred.

| Check | Build | Outcome |
|---|---|---|
| `python models/build.py --test --out D:/c5-64bit-work/4000/0093r2/build` | clang-cl Release | ctest 4 of 4 passed, 0 skipped (`test_hostile` 4.9 s) [C] |
| 9021's 86 attacks (`run_attacks.py`, unchanged) | Release and MSVC ASan | **86 of 86 clean** in both, outcomes identical to section 12's run (`attacks_release.tsv`, `attacks_asan.tsv`) [C] |
| 9021's `chain_depth.py` | Release | all 8 depths exit 1 [C] |
| `own_cases.py`, all 69 (expectations of the V10/V11 cases changed to "OK or `ReadBudget`, within 400 MB and 60 s") | Release | **69 of 69 pass in 12 s**: V9's file reads OK; the eleven V10/V11 inputs that failed in 12.2 end in `ReadBudget` (steps or built bytes) in 0.2-1.6 s at peaks of at most 262 MB; the 4,096- and 65,536-slot string databases are refused by `add_gsd` in 0.8 s (were 14 s and 219 s) (`own_release.tsv`) [C] |
| the same 69 | MSVC ASan | **69 of 69 pass, 0 skipped** (the cases that section 12 kept out of ASan now run), **no ASan report**: the V9 over-read is gone (`own_asan.tsv`) [C] |
| read-budget edges, exact (`budget_edges.py`, bisection over gr2sweep runs) | Release, edge rechecked under ASan | **steps**: a walked `ArrayOfReferences` of c unfixed slots (1 step each, no bytes; 71 MB file): c = 16,777,188 reads, **16,777,189 is refused** with `read budget: ... more than 16777216 read steps`; the 28 steps between are the rest of the read. **bytes**: 255 references to a mesh of 1 MiB of 1-byte vertices plus one mesh of x vertices (3.2 MB file): x = 957,680 reads, **957,681 is refused** with `... more than 268435456 B (kMaxBuiltBytes)` (90,896 B of other built bytes). c-1/c/c+1/c+2 and x-1/x/x+1/x+2 give the same answers in both builds (`budget_edges.out.txt`) [C] |
| 9021's 8 fuzz configurations, new seed 40933, 290,000 per configuration (**2,320,000 inputs**) | plain `/O1` | 0 crashes, 0 hangs, 1,028 s on 4 processes; no input reached `ReadBudget` (byte mutations do not build shared references) (`fuzz_plain.out.txt`) [C] |
| the same configurations, new seed 40932, 13,500 per configuration (**108,000 inputs**) | MSVC ASan | 0 crashes, **0 ASan reports**, 0 hangs, 602 s on 4 processes, peak 557 MB per process (`fuzz_asan.out.txt`) [C] |

**Observation (not a defect of the budget, low):** a file just inside the byte budget still costs time after the
read. The bytes-edge file at x = 957,680 (3.2 MB, 256 meshes of about 1 MiB of vertices) takes **14 s** in Release
gr2sweep and **102-104 s** under ASan, against 0.2 s when it is refused: `check_geometry` and `canonical_summary` loop
over all 268 M vertices (`Mesh::read` looks each component up by name), and these loops are not counted by the
budget. Bounded (by the 256 MiB budget) but about 1,000 times the read itself; a server that checks mod files could
charge them too or cap vertices per file [I].

### 14.1 Verdict

V1-V11 hold on every input of sections 4, 12 and 13 run here; the budget's two edges are exact and refuse cleanly
with the named error. **Ready to close** as far as this verification goes; the one observation above is for the coordinator to file or not.

### 14.2 Not verified

- Debug builds; `build.py --test` with no arguments at all (run with `--out`, as briefed).
- Uncapped behaviour: every crafted run here ran in a 1-1.5 GiB job; peaks stayed at or below 557 MB.
- The census of the game files against the budget (section 13.3 did it; not repeated).
