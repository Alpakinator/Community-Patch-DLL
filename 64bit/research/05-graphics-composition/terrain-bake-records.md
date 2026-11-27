# The terrain bake's per-tile layer records, the stock tile-cache pool, and why tiles stay gray (issue 3023)

Written 2026-11-13 00:10-00:40 EST by one-off 3023 for decisions D81/D82 (bake stage 1, epic block 3400). Marks: `[C]`
from a static reading of the DX11 EXE (Ghidra 12.1.4 headless, read-only project `D:/c5-64bit-work/2900/2903/ghidra`,
plus the objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm`), `[L]` read live from a test instance, `[I]`
inferred. Addresses are VAs at ImageBase 0x400000. Following D83, layouts and behaviour are given in words and
tables. No decompiler output is committed. Raw outputs are on the D drive and are not committed, because they hold
game data: `D:/c5-64bit-work/3023/` (`ghidra-out/`, `run1/`, `run2/`, `run3/`, `captures/`).

Setups of the live runs, all on Goldberg instance c5h-c, CP/VP/EUI, window 2554x1411, RTX 4080:

| Run | When (EST) | Config | DLL | Save (copy) |
|---|---|---|---|---|
| 1 | 2026-11-12 23:45-23:51 | D33 (`[DisableSystems]` set + marker `clear=0x1`) | `i2-shell-test` b92373aeb (old pin; the only DLL with the view-mask marker, `deps/i2-shell-test.lock`) | `C5H-3023-HUGE_0350` (copy of `C5H-VP8P-HUGE_0350`, huge, turn 350, SHA-256 c499d3c6...) |
| 2 | 2026-11-12 23:51 - 11-13 00:02 | stock | `i0ship-bbfacca1-ia` (current pin) | same |
| 3 | 2026-11-13 00:02-00:08 | stock + RenderDoc 1.47 preload (D57) | same | `C5H-3023-TINY` (copy of `c5h-9028-tiny-t0008`) |

Tools (read-only, by PID): `shell/tools/layer_probe.py` (all tiles, the tables, the pools, a check of each tile's
list against the material bytes) and `shell/tools/pool_watch.py` (pool and visible tiles sampled every 0.05-0.5 s,
optionally after a `UI.LookAt`). The instance was backed up by hash before the runs and restored by hash afterwards
(`D:/c5-64bit-work/3023/run1/backup-standard`, verify OK at 00:09). The save copies were deleted.

## 1. Answer for 3400: the layer records are present for every tile after a D33 load, and they can be used

- **[L, run 1] After a D33 load all 10,752 tiles of the huge map have a layer list.** That holds for the 135 baked
  tiles and for the 10,617 never-baked ones. The list length (tile+0x1F8) has this distribution: 1 layer 8,125
  tiles, 2: 1,083, 3: 1,114, 4: 380, 5: 41, 6: 3, 7: 2, 8: 4. **300 of 300 lists checked** (100 baked, 200 unbaked) agree
  with the material bytes of the CPU height field under the tile, read from 0xD91270+0x4C, a layer that epic 3200's
  tap already reads. Run 2 (stock) gives the same histogram.
- The lists are made at load by a per-tile job, not by the in-play cull. That is why D33 does not stop them (section
  2.2) [C + L].
- **The global tables are present and static**. They hang off the terrain renderer that the tile points to,
  TR8 = tile+0x13C = TR+8 (section 2.3). They match `Assets/DLC/Shared/Terrain/TerrainDef.csv` row for row [L]. So
  stage 1 has two ways to get its inputs:
  - read the tables and lists from the EXE once after load (a few KB plus 36 bytes per tile); or
  - compute them in the server from the tapped material bytes and a game file read at run time (D83, D85).
- What the GPU stage draws per tile is now known from a capture (section 4): one grid mesh of 67 x 67 vertices. Each
  vertex carries 8 layer weights and 8 layer indices, and up to 8 layers are blended additively into three 512 x 512
  float targets. What is **not** decoded: how the CPU turns material bytes into those per-vertex weights when a tile
  has more than one material. The captured tile had a single layer, weight 255. This is the first item for 3400.

## 2. The records [C, values L]

### 2.1 Per tile (tile object 0x330 bytes, grid array TR+0x1ADC, column-major index x * tilesY + y)

| Offset | Meaning |
|---|---|
| +0xD4 / +0xD8 | 65 (64 in the last row/column): the tile's extent in samples (terrain-rendering-inventory.md 5.1) |
| +0xDC / +0xE0 | the tile's origin in height samples (x, y) |
| +0x13C | TR8, the renderer the tables hang off (= TR+8) |
| +0x178 / +0x17C / +0x180 | cache size: current near-cache size, the size class of the colour cache (64/128/256/512), and the size wanted for the screen size (0x4B2B40) |
| +0x190 / +0x194 | near-camera caches (RGBA8, section 3.1) |
| +0x198 / +0x19C | colour cache t0 / second cache t8 (BC1) |
| +0x1CC | content generation: raised when the tile's content changes (writers not traced). *Added 2026-11-13 (3404): one writer is 0x4387D0, which for every tile in a world rectangle sets +0x1CC = the frame counter, +0x1F8 = 0 and +0x300 = 1 (terrain-bake-comparison.md 7) [C]* |
| +0x1D0 | generation of the last bake (set by the bake wrapper 0x627810 from a global frame counter; 1 = never baked) |
| +0x1D4 | sort key of the eviction order (0 for tiles never seen; frame stamps for seen ones) |
| +0x1E4 | an embedded job object (vtable 0x9E9BDC, body 0x6ABD30). It computes the tile's height range and LOD class (0x7212B0) and its layer list (0x568430) |
| **+0x1F8** | **layer count, 0..8** |
| **+0x1FC** | **layer ids, u32 each** (64 entries reserved, at most 8 used) |
| +0x2FC | 1 when the tile needs the second cache t8 (some layer has a spec texture) |
| +0x310 / +0x314 | bake state now / state requested (section 3.3) |
| +0x320 / +0x324 / +0x328 | largest height step, highest point, height class: 1 and 2 = under water or flat low (never baked, skipped by the cull), 3 = land |

### 2.2 How the list is built (0x568430) [C]

Rewritten 2026-11-13 by 3401 (W1 of team-3400) from a second static reading of 0x568430, 0x747580, the grid
constructor 0x582A60 and the CSV loader 0x61BAE0 (objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm` and 3023's
`ghidra-out/d1.txt`). The earlier text said "a margin of 2 samples on each side", "a threshold (0x9FCEC8)" and left
the normal grid open; the window, the threshold value, the normal grid and an index offset are now given below.
Constants are given as decimal values; the hex word is named only where the exact float matters.

Inputs: the tile's origin (ox, oy) = tile+0xDC/+0xE0 and extent (ex, ey) = tile+0xD4/+0xD8 (65, or 64 in the last
column / row), the height field 0xD91270 (u16 heights h and the material byte layer), TR8's tables, and TR8+0x1AA8 /
+0x1AAC = the map's sample counts W, H (the same as the grid's samplesX/Y [I]; read live by 3401's probe).

1. **Window.** x runs from ox-2 to ox+ex inclusive and y from oy-2 to oy+ey inclusive: 2 samples before the tile and
   ex-64+1 after it (68 x 68 samples for a 65-extent tile, 67 in the last column/row). Each (x, y) is wrapped with
   `(W + x) mod W`, `(H + y) mod H` (unsigned). **y wraps too**, although the map does not wrap in y: the bottom margin of
   row 0 reads the last rows of the field, and the top margin of the last tile row reads row 0.
2. **Material.** b = the byte layer at the wrapped (x, y); the first layer id is j = TR8+0xC10[b]. There is no check
   of b: a byte without a texture set reads the zero-filled table, so j = 0.
3. **Normal.** n = N[(y-oy+2) * 69 + (x-ox+2)] from the tile's 69 x 69 normal grid N (step 2.2a). Note the offset:
   N's entry (i, k) belongs to sample (ox+i, oy+k), so the sample (x, y) is tested with the normal of sample
   (x+2, y+2). An EXE quirk; a port copies it.
4. **Slope move.** Only when TR8+0xB10[b] = 1 (slope-projected set): if n.z <= 0.9 (float 0x3F666666), j moves by
   +2 when |n.y| <= |n.x|, else by +1.
5. **Counting** (an array of 128 counters, zeroed per tile): a slope-projected sample adds 1 to j, j+1 and j+2 (j as
   moved); every sample, slope-projected or not, then adds 1 to j. So a slope-projected sample counts j twice and
   j+1, j+2 once. When j was moved, j+1 and j+2 reach up to the set's first id + 4, i.e. into the **next** texture
   set's layers (the extra-count quirk). Example with the live tables: Gibraltar (byte 8, ids 10-12) moved by +2
   counts 12, 13, 14, and 13/14 belong to sets 9 and 10.
6. **List.** Ids 0 .. TR8+0x1110-1 with a non-zero count are written in ascending order to tile+0x1FC, their count
   to tile+0x1F8 (counters at or past the layer count are ignored). Only when the count is above 8: a bubble sort by
   count, descending, swapping neighbours only when the left count is strictly smaller (so equal counts keep their
   ascending id order), repeated until a pass makes no swap; then the count is set to 8 (entries 8.. stay in memory).
7. tile+0x2FC = 0, then, only when the global 0xD90F28 is non-zero: 1 when tile+0x300 is non-zero or some listed
   layer's set has a spec texture (TR8+0x808[set] != 0).

#### 2.2a The 69 x 69 normal grid (0x747580, called with the tile's origin, 69, 69 and an output array) [C]

The function belongs to the height-field object 0xD91270 and works on a 76 x 76 window of heights, columns
x = ox-2 .. ox+73 and rows y = oy-2 .. oy+73 (window index (c, r) = (x-ox+2, y-oy+2)).

- **Fetch.** For each window sample: x is wrapped as `(samplesX * 100 + x) mod samplesX` only when the map wraps
  (0xD90FBC = 1), then x is clamped to 0 .. samplesX-1 and y to 0 .. samplesY-1 (no wrap in y). It keeps the u16
  value H(c, r) as an integer and F(c, r) = float(H) * (1/255) as a float (the float word 0x3B808081).
- **Output.** For c = 2 .. 70 and r = 2 .. 70 (69 x 69), entry N[(r-2) * 69 + (c-2)] = the normal of window sample
  (c, r) = height sample (ox+c-2, oy+r-2). It is computed as follows (single-precision float, each operation rounded
  in the order written; the EXE does it 4 rows at a time with SSE, which does not change any result):
  1. Integer central differences, arithmetic shift: gx = (H(c+1, r) - H(c-1, r)) >> 1, gy = (H(c, r+1) - H(c, r-1)) >> 1.
  2. Start values of the "plane prediction": px = (float(gx) * (1/255)) * -2 + F(c, r), py the same with gy.
  3. Two weighted sums, one for x and one for y, over 16 taps each. Per tap with neighbour value v = F(c+dc, r+dr),
     prediction p and kernel weight K:
     `d = ((p - v) * (p - v)) * -3.3333333 * (1/16384)` (float words 0xC0555555, 0x38800000, multiplied in this order),
     `w = (((d * d) * 0.5 + d) + 1) * 1.03 * K` (1.03 = 0x3F83D70A),
     numerator += w * v, denominator += |w|; both sums start at 0 and add the taps in the order below.
  4. Dx = numerator_x / denominator_x, Dy = numerator_y / denominator_y.
  5. With the grid's scales sx = +0xA8 (1.0), sy = +0xAC (0.866), sz = +0xB0 (0.2): a = -(Dx * sz), b = -(Dy * sz),
     n = ((b * 0) - sy * a, (a * 0) - b * sx, sy * sx), normalised with D3DXVec3Normalize (the import; how D3DX rounds
     the length is not read: [I] that it is `v / sqrt(x^2+y^2+z^2)`).
- **Taps and the prediction (an EXE quirk).** The prediction is meant to follow the slope, but between columns
  (x sums) or rows (y sums) it is advanced by **1.0**, not by the gradient. Order of the taps, with the prediction
  each one uses:

  | Sum | Taps in order (dc, dr) | Prediction |
  |---|---|---|
  | x | (-2,-1) (-2,0) (-2,+1) | px |
  | x | (-1,-2) (-1,-1) (-1,0) (-1,+1) (-1,+2) | px + 1 |
  | x | (+1,-2) (+1,-1) (+1,0) (+1,+1) (+1,+2) | ((px + 1) + 1) + 1 |
  | x | (+2,-1) (+2,0) (+2,+1) | that + 1 |
  | y | (-1,-2) (0,-2) (+1,-2) | py |
  | y | (-2,-1) (-1,-1) (0,-1) (+1,-1) (+2,-1) | py + 1 |
  | y | (-2,+1) (-1,+1) (0,+1) (+1,+1) (+2,+1) | ((py + 1) + 1) + 1 |
  | y | (-1,+2) (0,+2) (+1,+2) | that + 1 |

  The centre column (x sum) / row (y sum) and the four corners are not taps.
- **Kernels.** Two 5 x 5 tables in the height-field object, written by its constructor 0x582A60: Kx at +0xFF68 and
  Ky at +0xFFCC, both indexed [dc+2][dr+2] (5 floats per dc). They are outer products of two 5-vectors, each entry
  one single-precision multiply: Kx[i][k] = A[i] * B[k], Ky[i][k] = B[i] * A[k], with

  | index | 0 | 1 | 2 | 3 | 4 |
  |---|---|---|---|---|---|
  | A (derivative) | 0.0702 | 0.3526 | 0 | -0.3526 | -0.0702 |
  | B (smoothing) | 0.0228 | 0.2293 | 0.4948 | 0.2293 | 0.0228 |

  (each the nearest float to the decimal shown). A is positive on the low side, so Dx and Dy are roughly the negated
  slope in height units per sample; sz and the sign then give an upward normal (n.z = sy*sx/len > 0).

#### 2.2b The texture-set table read from TerrainDef.csv (0x61BAE0) [C, values L]

- The EXE reads `TerrainDef.csv` by name, skips the header line and parses each row with the pattern
  `%i %s %s %s %f %f %i %x` after the separators: continent, diffuse, heightmap, specmap (`NULL` = none), base
  dominance, texture scale, projection, and **the id column as hexadecimal**.
- **After the last row it appends one more set**: diffuse and heightmap both `Checkerboard.dds` (from
  `assets\terrain\TerrainTextures\`), no spec, scale 0.01, continent and id -1. That is why the live tables hold 42
  sets for 41 data rows (set 41: diffuse handle = height handle, scale 0.01 [L]); no tile of the huge save used it
  (run 2).
- Layers: each set k takes one layer id, or three (projections 0, 1, 2) when its projection column is 1; ids are
  given in row order. With the 2026-11-12 VP install: 41 rows + Checkerboard = 42 sets, 3 of them slope-projected, so
  48 layers (TR8+0x1110) [L].

Callers: the per-tile job (above), and the in-play cull 0x7BA820, for a tile in state 5 whose content generation
+0x1CC is ahead of +0x1D0.

### 2.3 The tables at TR8 (live values, huge save, identical in runs 1 and 2)

| Offset | Table | Live |
|---|---|---|
| +0x1110 | layer count | 48 |
| +0xC10 [byte] | material byte -> first layer id | bytes 0..41: 0,1,2,3,4,7,8,9,10,13,14,... (a slope-projected byte takes 3 ids) |
| +0xB10 [byte] | slope-projected flag | 1 for bytes 4, 8, 17 |
| +0xF10 [layer] | layer -> texture set k | 0..41; sets 4, 8, 17 have three layers each |
| +0x1010 [layer] | projection of the layer: 0 top-down, 1 and 2 the two side projections | |
| +0xE10 [k] | texture scale of set k | 0.014, 0.1, 0.014, 0.17, 0.05, ... |
| +0x708 / +0x908 / +0x808 [k] | EXE texture handles (u16) of the diffuse, height and spec texture of set k (spec 0 = none) | e.g. 8420 / 8389 / 0 |
| +0x53C | the height field 0xD91270 | |
| +0x1AA8 / +0x1AAC | sample counts of the map, used for wrapping | 8192 / 5376 = the grid's samplesX / samplesY (huge, 3401 [L]) |

**Texture set k is row k of `TerrainDef.csv`** (41 data rows: continent id, diffuse, heightmap, specmap, base
dominance, texture scale, projection, id) [L]; set 41 is the `Checkerboard.dds` set the loader appends (2.2b,
added 2026-11-13 by 3401). **Which file:** `Assets/DLC/Shared/Terrain/TerrainDef.csv` (loose). The base pack
`Resource/Common/TerrainModels.fpk` holds an older `terraindef.csv` with other scales and slope flags (sets 4, 8, 9, 17,
26, 34 slope-projected; row 0 has NULL textures); the live tables match the loose DLC file, not the pack [L, 3401,
2026-11-13]. `c5r::gamefs` resolves the bare name to the pack, because it does not mount the Shared DLC's loose
folders (its `Upgrade1.Civ5Pkg` lists no `<Directory>`); how the EXE mounts them is not traced [I]. *Corrected
2026-11-13 (3404): traced, the "Common" kind mounts the package folders (terrain-bake-comparison.md 2); gamefs now does
the same and resolves the name to the loose DLC file.* Three columns line
up with the live tables:

- the scale column equals +0xE10;
- projection = 1 marks exactly sets 4, 8 and 17 (Mesa, Gibraltar, Amer_Crater), the three slope-projected ones;
- NULL specmaps are exactly the sets with +0x808 = 0.

So the material byte that the height blend writes (asset-pipeline.md 4.1 step 5) is the CSV row. The EXE's texture
records (0x4170B80 + 0x398 * handle) hold sizes and formats, not file names. The diffuse/height/spec textures are BC3
(1024 or 2048, 11-12 mips). The server loads them by name from the CSV through `c5r::gamefs`.

### 2.4 The lists in the server and the check against the EXE (issue 3401, 2026-11-13) [C code, L check]

**Implementation.** `render/src/bakelayers/layers.cpp` (header `render/include/c5r/bake/layers.h`, contract types in
`render/include/c5r/bake/bake_types.h`), written from 2.2-2.2b only:

- `parseTerrainDef` / `loadTerrainDef`: the CSV text -> `LayerTables` (sets, the appended Checkerboard set, layer
  count, first layer per material byte, slope flags, layer -> set, layer -> projection). `loadTerrainDef` reads
  `<install>/Assets/DLC/Shared/Terrain/TerrainDef.csv` first and falls back to the gamefs lookup (2.3 says why).
- `normalGrid`: the tile's 69 x 69 normals (2.2a), single-precision, one rounding per operation in the stated order,
  no contraction. Computed only for a tile whose window holds a slope-projected material (the normals are read for
  nothing else): 6 of 10,752 huge tiles, so a whole huge map takes about 1 s.
- `buildLayerList` / `buildAllLayerLists`: 2.2 steps 1-6 with every quirk; a material byte without a texture set is
  an error (`NotFound`), not layer 0 as in the EXE, so that bad input is visible.
- Tests: `render/tests/bakelayers/` (30 tests: each parse and validation path with a negative test asserting the Err;
  the window, both wraps, the slope moves, the +2 normal offset, sort and cut; the checker tool run as a product with
  its exit codes; the install's CSV against the live tables). `python render/build.py --test`: 10/10 suites passed,
  0 skipped (2026-11-13 03:25).
- Checker: `render/tools/bakelayers_check` (`--tap C5HF --game-dir DIR --exe-lists FILE [--report JSON]`); the EXE's
  lists file comes from `shell/tools/layer_probe.py --lists-txt` (live) or `--from-json` (an older probe). Exit 0 all
  equal, 1 differences (each tile with its list pair and cause), 2-5 usage / unreadable / unparsable / mismatched.

**Result: acceptance A holds; the lists equal the EXE's on every tile of both saves [L].**

| Run (2026-11-13, c5h-e, CP/VP/EUI, stock config) | DLL | Heights + material bytes | Tiles equal | Tables |
|---|---|---|---|---|
| A huge `C5H-3401-HUGE_0350` (copy of `C5H-VP8P-HUGE_0350`), pid 54568, probe 03:37:29 | current pin `i0ship-bbfacca1-ia` (E589306E...) | ReadProcessMemory of 0xD91270 (`tap_live.py rawdump` + `hfbake decode`) | **10,752 / 10,752** | equal |
| A Tiny `C5H-3401-TINY_0008` (copy of `c5h-9028-tiny-t0008`), pid 40520, probe 03:42:06 | same | same | **2,240 / 2,240** | equal |
| B huge, same save, pid 57736, probe 03:48:02 | `i2-shell-test` b92373aeb (638B5E21...; old pin, has the shell-helper loader) | the composite DLL's tap (C5HF, `tap=1 tap_byte_layer=1`) | **10,752 / 10,752** | equal |
| B Tiny, same save, pid 28676, probe 04:09:52 | same | same | **2,240 / 2,240** | equal |
| offline: 3023 run 2 (stock) and run 1 (D33) lists | current pin / old pin | 3200's taps of the same save (other sessions) | 10,752 / 10,752 each | equal |

- The slope path is exercised: on the huge save 6 tiles (94,47), (94,48), (111,43), (111,44), (112,43), (112,44) hold
  slope-projected materials (Amer_Crater, Gibraltar), with 4 to 1,203 such samples each and 4 to 591 samples moved
  by +1 or +2; the closest n.z was 0.9000206 (tile (94,48)) and the lists still match, so the normal grid and the
  threshold hold to at least 2e-5. Three huge tiles have more than 8 ids (sorted and cut): equal. Tiny has no
  slope-projected material.
- *(Added 2026-11-13, one-off 9047, terrain-bake-verification.md 3)* Reproduced on two other saves (a Tiny map of another
  game, 2,240 / 2,240; the huge map at turn 428, 10,752 / 10,752), with an own reader beside `layer_probe.py`. A read of
  the live lists in a game in play can catch a list mid-rebuild (tile (91, 21): count 2, ids [0, 0], 4 s after a read
  of [3, 43]) [L]: re-read before calling a difference.
- The current pin has no shell-helper loader, so the composite DLL (and with it the height-field tap and the bake
  tap) cannot run on it; arm A read the field by PID, which 3203 (P3) and this run show equal to the DLL's tap: run B's
  DLL tap and its own ReadProcessMemory copy differ in 0 of 44,040,192 samples, byte layer included [L].
- Heights between sessions: runs A and B of the huge save differ in 32 samples by 1 u16, of the Tiny save in 8 by 1
  u16; byte layer identical (as shared-texture-and-compositor.md 11, P4). The lists do not depend on it. Run B's
  Tiny DLL tap equals its ReadProcessMemory copy too (0 of 9,175,040).

**The EXE reference set for the comparator (3404)** [L], `D:/c5-64bit-work/3400/ref/<save>/` (game-derived, not
committed; `manifest.json` per save lists pins, hashes, times, counts and files):

| Save, arm | Lists + tables | Height field | Caches (t0 / t8, raw BC1 256 x 256, 6 mips, 43,680 B each) |
|---|---|---|---|
| huge armA (current pin) | `layers.json`, `lists.txt` | `heightfield.c5hf` (by PID) | none: the current pin cannot load the composite DLL |
| huge armB (i2-shell-test, stock) | same, plus `layers_at_bake.json` | `tap/tap_l3_c1.c5hf` (DLL tap) = `heightfield.c5hf` | 3,083 t0 + 3,051 t8 = 6,134 files, 267.9 MB, 0 failed, 0 tiles changed during the copy |
| Tiny armA | as huge armA | as huge armA | none |
| Tiny armB | as huge armB | as huge armB | 1,307 t0 + 1,245 t8 = 2,552 files, 111.5 MB, 0 failed |

- After entering, the bake-ahead filled the pool in about 35 s on huge (cached 180 -> 740 -> 2,107 -> 3,085 at 11 s
  steps, then 3,083 for 3 polls; cursor 4,767 = the whole bake-ahead list) and within 10 s on Tiny (1,303 cached in
  run A, 1,307 in run B, against 1,278 tiles of height class 3). **63 cached Tiny tiles have height class 2** (flat
  low, state 1): the bake-ahead bakes class-2 tiles; only the in-play cull skips classes 1 and 2 (correcting 3.2's
  "Water and low tiles (height class 1/2) are never baked" as far as class 2 goes) [L]. Same huge pool figures as 3023
  run 2 (3,083 cached, 10 free).
- `caches/raw_index.json` gives per file: tile index, x, y, slot, slot generation, SHA-256, and the tile record read
  just before the copy (state words +0x310 / +0x314, size class +0x17C, near size +0x178, wanted +0x180, +0x1CC / +0x1D0,
  cache handles, height class, +0x2FC, the layer list). The bake tap held exactly the cached tiles (huge: 3,356 slots,
  3,083 valid; Tiny 1,307 / 1,307).

**EXE against EXE: two sessions of one save do not bake identical caches (the comparator's ceiling)** [L]. Tiny,
arm B twice (pids 28676 and 7060, same DLL, same config, same machine): 2,479 caches present in both (1,304 t0 tiles
in run 2); **1,882 byte-identical, 597 not** (409 t0, 188 t8), every one of them with the same state word, size class
and layer list in both sessions. Decoded mip 0 over all 2,479 (`cache_decoded_diff.py`, scratch): **94.29 % of
texels equal**; per-texel largest channel difference p99 55, p99.9 101, max 247 (8-bit). The worst t0 tiles have
only 23-41 % of texels equal. Seen in tile (11,19) t0: the same ground, but the dark blob decals (shadow-like spots,
[I] feature or tree shadows) lie in other places, so their placement is random per session (bake stage 2's CPU part
or the job order; not traced). Huge, arm B twice (pids 57736 and 48132): the same 3,083 tiles and 6,134 caches in both;
**3,960 byte-identical, 2,174 not** (1,331 t0, 843 t8), all with equal state, size class and list; decoded mip 0:
**92.26 % of texels equal**, p99 74, p99.9 132, max 255; differing t0 tiles have a median of 70 % of texels equal,
the worst under 1 %. In a differing farm tile the field patches have other tints and layout and the road runs at a
slightly different place: **farm and road decals, and the blob decals, are laid out randomly per session** [L for the
pictures, I for the cause: bake stage 2's CPU part, which this epic does not port].
*Corrected 2026-11-15 (issue 4107, from 4101's terrain-decals.md 3.1) [C, L]: the cause is in the decal producers, not
in the bake: `rand()` in the farm layout 0x6016F0 (field style) and `rand() % 3` in 0x5865B0 (a turned field) [C], the
Forest's random tree placement for the blob decals (Tree_Shadow, one shadow quad per tree, 0x770EE0), and the route
builder's curves (random, not traced to one call [I]); river, flood-plain and model-ground decals are deterministic
[L: equal between two loads]. With the same session's decal records drawn by the server, the "not fair" caches become
equal (terrain-decals.md 13.3). Replaced: "I for the cause: bake stage 2's CPU part".* So a texel-exact comparison of the
server's bake against one EXE session is bounded by this: tiles with such decals cannot match better than two EXE
sessions match each other, unless the server reproduces the EXE's random state of that session. Byte-identical
between the sessions, and therefore the fair target for the comparator (3404): the 3,960 huge / 1,882 Tiny caches
listed in `equal_between_sessions.json` beside each manifest (per-texture decoded figures of the others in
`decoded_<save>.json`; the scripts `capture.py`, `cache_determinism.py`, `cache_decoded_diff.py` are in
`D:/c5-64bit-work/3400/ref/`).

## 3. The stock tile-cache pool and why tiles stay gray

### 3.1 One tile's caches [L, run 2, EXE texture records]

| Cache | Format | Size | Mips | Bytes |
|---|---|---|---|---|
| t0 colour (tile+0x198) | BC1_UNORM | 256 x 256 | 6 | 43,680 |
| t8 second (tile+0x19C), on 10,612 of 10,752 tiles | BC1_UNORM | 256 x 256 | 6 | 43,680 |
| **per tile, both** | | | | **87,360** |
| near-camera pair (tile+0x190/+0x194), state 5 only | R8G8B8A8_UNORM | 256 x 256 | 9 | 2 x 349,524 = 699,048 |
| same, closest tiles | R8G8B8A8_UNORM | 512 x 512 | 10 | 2 x 1,398,100 = 2,796,200 |

At one moment of run 2 (default zoom), 79 tiles held a 256 near pair and 32 held a 512 near pair: 144.7 MB on top of
the BC1 caches [L]. Not counted here: the bake's scratch targets (3 x 512^2 RGBA16F, section 4) and the EXE's
per-tile height pages.

### 3.2 The pool [C + L]

- Quality switch 0xD90F74 (3 here) selects the following through 0x453F80 (live block 0xD90F14..0xD90FAC in run
  1's JSON):
  - array mode 0xD90F4C = 1;
  - colour-cache size 0xD90F30 = 256 and largest size 0xD90F2C = 512;
  - **array-pool capacity 0xD90F38 = 6,144 slots**;
  - **bakes per frame 0xD90F60 = 1**;
  - up-res candidates per frame 0xD90F64 = 2.
- Colour caches come from the array pool's free list (TR+0x21E8C, count TR+0x21E90). Run 1 [L]: 5,899 free + 245 in use =
  6,144. **6,144 slots x 43,680 B = 268.4 MB (256 MiB) of video memory.** A tile with t0 and t8 uses 2 slots, so the
  pool holds about 3,072 tiles.
- Near caches come from per-size free lists (64: TR+0x2450, 128: +0xA2E0, 256: +0x12170, 512: +0x1A000). An empty size
  falls back to the next smaller one (0x6AD280) *only in non-array mode; in array mode (stock high) an empty size gives
  no near cache at all (corrected 2026-11-15, issue 0306, terrain-bake-passes.md 3.5.1 [C])*. Live free counts: 16 / 0 /
  240-256 / 0-60.
- **The huge map does not fit**: 4,729 land tiles x 2 slots = 9,458 > 6,144.
- After entering the game, the bake-ahead (list TR+0x1AE8, 4,767 tiles, cursor TR+0x2328, one tile per frame when
  nothing else is pending) bakes every land tile once. Within 40 s of entering, 3,083 tiles held caches and 10 slots
  were free. The other **1,805 tiles were baked and then thrown away** by eviction: state now 0, state requested 1,
  never re-tried by the bake-ahead [L].
- **Eviction** [C]: each frame the cull sorts the cached tiles that are not visible by tile+0x1D4 (descending) and
  keeps at least 8 free entries per pool (0x574F40), releasing from the end of that order. When a visible tile cannot
  get a slot, up to 20 rounds release 16 tiles each. Water and low tiles (height class 1/2) are never listed as
  visible by the cull. *Corrected 2026-11-13 (3401, 2.4) [L]: the bake-ahead does bake class-2 tiles: 159 of the
  3,083 cached huge tiles (all state 1) and 63 of 1,307 Tiny ones have class 2; class 1 none. Replaced: "are never baked
  or listed as visible".*
- **Return to a visited place** [L, run 2]: after four camera jumps, a jump back to the area of the load view found
  113 of 121 visible tiles without a cache. That area's caches had been evicted, or had never survived the bake-ahead.

### 3.3 What requests a bake, and the budget [C, rates L]

- Per frame the cull 0x51B070 calls 0x7BA820 with a "settled" argument = (TS+0x75FE4 == 0) and 0xD90F58.
  - Settled: up to **16 bakes per frame**.
  - Otherwise: **0xD90F60 = 1 bake per frame**.
  - A visible tile that wants a bake beyond the budget gives its slot back and waits for the next frame.
- **TS+0x75FE4 is 1 while the camera moves** [L, run 2, `watch_flag_20_20.json`]: after a `UI.LookAt` it was 1 from
  0.09 to 0.66 s. During that time the uncached visible tiles stayed at 126-139 and the free count fell by about one
  slot per frame. Once it was back to 0, the tiles drained in 0.15 s, the free count dropping in steps of 16.
  - Writers of the flag: 0x6F4DF0 sets it to 1; 0x4B2069 and 0x69F150 set it to 0. Not traced further [I that it means
    "camera in motion"].
- States (tile+0x310 now, +0x314 requested): 0 none, 1 baked by the bake-ahead or pre-bake, 3 baked for view, 4/5
  near-cache variants, 2 re-bake. The bake wrapper 0x627810 re-bakes in state 3 only when the old state was not 1. It
  copies +0x314 into +0x310 and stamps +0x1D0.
- **The content generation is checked only in state 5** [C]. A tile that has been baked in state 1 or 3 and whose
  roads or improvements change (+0x1CC raised) is not re-baked by the cull: its cache stays as baked until it is
  evicted, or until a reload. In run 2, 12 of 3,083 cached tiles had +0x1CC > +0x1D0 just after the load [L]. That
  this is the "wrong until a restart" case is [I]: the writers of +0x1CC are not traced.
  *Corrected 2026-11-13 (3407, terrain-bake-residency.md 3 and 3.3) [C + L]: +0x1CC is written by the rectangle
  invalidation 0x4387D0 (and two whole-map writers); a road built by Lua raised it on 16 tiles at once, which stock
  left stale in state 3 for 70.8 s while the camera stayed and re-baked when the camera left and came back - not
  "until eviction or a reload". Replaced: "until it is evicted, or until a reload" as the only ways out.*

### 3.4 Delay after a jump, measured on this machine (RTX 4080, run 2) [L]

| Jump (zoom) | Visible land tiles | Most visible tiles without a cache | Time from `LookAt` to all baked |
|---|---|---|---|
| (115,70), default zoom 14.3 | 35-117 | 99 | 0.57 s |
| (100,30), default | 121 | 113 | 0.66 s |
| (110,20), max zoom-out 20.5 | 303 | 134 | 0.92 s |
| (50,70), max zoom-out | 36-604 | 520 | 0.92 s |
| (100,45), max zoom-out | 513 | 494 | 1.51 s (about 0.8 s with no bake while the camera flew in, then about 580 tiles/s) |

Two quantities set the delay. While the camera moves it is 1 tile per frame (the frame rate in tiles per second).
Once the camera settles it is 16 per frame, limited by the bake's GPU time: about 1.2 ms per tile here [I: 16 tiles
in a frame of about 19 ms].

**Why the owner sees up to 10 s (D86)** [I, from the rates above]:

- Panning or scrolling keeps the camera moving, so the game bakes **one tile per frame** for as long as the movement
  lasts. At 30-60 fps on a late huge game, a zoomed-out view of 300-500 new land tiles needs 5-17 s, and a jump into
  an area that the pool evicted needs the same.
- After the camera stops, a GPU 5-10 times slower than an RTX 4080 needs 3-6 s for 500 tiles at 16 per frame.
- The pool cannot hold the land of a huge map (9,458 slots needed, 6,144 present), so a revisited area is often gray
  again.
- Zooming changes the wanted size class (+0x180), which re-bakes near tiles.

### 3.5 Memory figures for the server's choice (D86 question) [L counts x 87,360 B per tile]

Visible set from the EXE's visible list (land tiles only). A ring adds one tile on every side of the view's bounding
box. The counts include water tiles, which the EXE never bakes.

| Case | Land tiles | MB (BC1 t0+t8) | All tiles in the box (land + water) | MB |
|---|---|---|---|---|
| (a) every land tile of the huge map | 4,729 | 413.1 (394 MiB) | 10,752 | 939.3 (896 MiB) |
| stock pool, for comparison | about 3,072 | 268.4 | | |
| (b) default zoom 14.3, view box | 59 (50 visible) | 5.2 | 88 | 7.7 |
| + 1 ring | 83 | 7.3 | 130 | 11.4 |
| + 2 rings | 110 | 9.6 | 180 | 15.7 |
| + 3 rings | 138 | 12.1 | 238 | 20.8 |
| (b) max zoom-out 20.5, view box at (20,55) | 277 (219 visible) | 24.2 | 475 | 41.5 |
| + 1 / 2 / 3 rings | 315 / 355 / 390 | 27.5 / 31.0 / 34.1 | 567 / 667 / 775 | 49.5 / 58.3 / 67.7 |
| max zoom-out at (100,45), visible only | 513 | 44.8 | | |

Not measured: the closest zoom, other window sizes (the visible count grows with the window), and the near-camera
RGBA8 caches, which stock adds on top (144.7 MB at one moment, 3.1).

## 4. The capture: what the bake draws [L, run 3, RenderDoc 1.47]

- The capture taken at the load screen (`frame380.rdc`, 106.6 MB, `D:/c5-64bit-work/3023/captures/`) contains one
  tile bake. The two in-game captures right after a jump and a zoom (frames 7436, 8691) contain none: on Tiny all
  1,304 land tiles were already cached (array pool 3,604 free).
- The blend pass:
  - render targets: three 512 x 512 R16G16B16A16_FLOAT ("Albiedo/Normal/Spec Temp"), cleared to 0, mips generated
    after the draw;
  - blend ONE + ONE additive on target 0;
  - depth off, cull none;
  - one `DrawIndexedInstanced` of 26,136 indices (66 x 66 quads);
  - 24 texture slots: slot 0 / 8 / 16 = the layer's diffuse / height / spec (BC3 1024-2048), the rest a dummy;
  - 3 samplers;
  - a 1,280-byte constant buffer (holds the 0x4C0 per-layer block, asset-pipeline 3.3).
- **The vertex format (48 bytes)**:

  | Offset | Content |
  |---|---|
  | 0 | POSITION float3 (clip space, -1..1 in steps of 2/66) |
  | 12 | TEXCOORD0 float2 (the sample coordinate, e.g. 639..705) |
  | 20 | TEXCOORD1 float3 (world x, world y = sample y * 0.866, height) |
  | 32 | TEXCOORD2/3 R8G8B8A8_UNORM x 2 (**8 layer weights**) |
  | 40 | BLENDINDICES0/1 R8G8B8A8_UINT x 2 (**8 layer ids**) |

  The captured tile has 67 x 67 = 4,489 vertices, all with weight 255 for layer 43 and 0 for the others. The CPU
  writes this buffer at bake time (a 3.76 MB dynamic buffer, `Map` WRITE_DISCARD).
- Not yet read from the capture: the normal, decal, CacheBake and compression passes, their states, and the constant
  buffer's values. The `.rdc` stays on D for 3400. The dump scripts were scratch (`bakechunks.py`, `creates.py`,
  `vbuf.py`); they are easy to redo with `tools/harness/rdc.py` plus `iterparse`.
- 2026-11-13 (issue 3403): the passes after the blend (normal, macro shadow, CacheBake, GPU DXT compression, mips),
  their states and constants are read in `terrain-bake-passes.md`; the capture's initial contents hold the whole
  previous bake (tile (640, 2112)), which the server reproduces bit for bit there.
- A capture across the load itself was not taken: the Lua channel is silent during the load, so the first capture
  fired at the load screen.

## 5. Not verified

- The rule that turns material bytes into per-vertex weights when a tile has more than one material (only a
  single-layer tile was captured).
  *2026-11-13 (3402): found and verified byte for byte on 24 captured bakes of 1-5 layers, in `terrain-bake-blend.md`.*
- Run 1 used the old-pin DLL. The records are EXE state and do not depend on the DLL's game logic [I], and run 2 on
  the current pin gives the same list histogram and tables [L].
- What raises tile+0x1CC, and the exact meaning of the states 2/4/5.
- Delays on the owner's own GPU and frame rate (rates here are from an RTX 4080 at about 100 fps).
- Pool capacities of the per-size near-cache lists (only free counts were read).
