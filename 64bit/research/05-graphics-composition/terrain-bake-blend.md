# The terrain bake's blend pass: per-vertex layer weights, the 67 x 67 grid and the pass itself (issue 3402)

Written 2026-11-13 from 03:05 EST by team-3400 W2 (epic 3400, bake stage 1). Marks: `[C]` from a static reading of the
DX11 EXE (Ghidra 12.1.4 headless on a read-only copy of `D:/c5-64bit-work/2900/2903/ghidra`, the objdump listing
`D:/c5-64bit-work/2900/2903/dx11.asm`, the EXE's own `.rdata` constants read from the file), `[L]` read from a live
capture or memory, `[I]` inferred. Addresses are VAs at ImageBase 0x400000. Following D83 the behaviour is given in
words and tables; no decompiler output, EXE bytes or game data is committed (scratch on `D:/c5-64bit-work/3400/w2/`).
Section 4 of `terrain-bake-records.md` describes the capture this file builds on; section 2 there (W1) the layer lists.

## 1. Answer: the weights are CPU-side, deterministic, and made long before the bake

- **The 48-byte vertices are not written in the bake 0x694A60.** The bake only copies them [C]:
  - 0x63CCD0 (first call of 0x694A60) reserves (w+2)(h+2) vertices in a per-frame dynamic vertex buffer
    (TR8+0x530, 3,763,200 B = 78,400 vertices; allocator 0x452A90: running count TR8+0x243C, base pointer TR8+0x2440;
    the base vertex goes to tile+0xC8) and (w+1)(h+1)*6 16-bit indices in a dynamic index buffer (allocator 0x799FC0,
    start index to tile+0xD0); w, h = tile+0xD4/+0xD8 (65, or 64 in the last column/row).
  - It writes the indices itself (0x5ABE70, section 3) and `memcpy`s the vertices from a CPU array at tile+0x304,
    then frees that array. The same function copies a second, 24-byte vertex array from tile+0x308 into TR8+0x52C
    (base to tile+0xCC): the CacheBake grid (W3's side; layout in 2.6).
- **The CPU array is made by jobs when the tile is (re)built for a bake**: 0x724600 (sets tile+0x136 = 1) calls
  0x5118E0 when the requested state is 1, or 3 after a state other than 1, or 5 after a state other than 4 (the same
  conditions under which the wrapper 0x627810 bakes). 0x5118E0 allocates the 48-byte array (tile+0x304), the 24-byte
  array (+0x308) and job records (+0x30C), and queues one job ("TerrainCell.cpp", vtable 0x9E6480, run 0x639D20) per
  block of at most 12 x 12 vertices over the (w+2) x (h+2) grid. Each job calls **0x5CE920 (tile, x0, x1, y0, y1)**,
  which fills its block of both arrays [C].
- So the rule is a pure function of the height field (heights and material bytes at 0xD91270), the global tables at
  TR8, the tile's layer list and the tile's origin. Jobs write disjoint vertices; nothing depends on thread order [C].
- **Verified live [L]: the rule below reproduces the captured vertex buffers of 24 bakes (1-5 layers) byte for byte**
  (section 4).
- **The blend pass is the first pass of the bake** (0x694A60 order [C]): blend ("HeightMap" marker, 3 targets) ->
  "Decal Layer" (when TR8+0x538) -> "Normal Calculation" -> "Macro Shadow" (0xD90F6C iterations) -> "Decal Macro
  Shadow" -> "Terrain Cache" (near caches, state 5) -> "Terrain Cache" t0/t8 (CacheBake) -> "DXT Compress" per mip ->
  copy into the cache slots. The blend targets are TR8+0x68C / +0x690 (spec, only with tile+0x2FC) / +0x694 and are
  read by the later passes (NormalCalculate draw object 0x515630 binds +0x694; CacheBake 0x6CF830 binds +0x6A0,
  +0x68C, +0x690, +0x698) [C; target names L in terrain-bake-records.md 4].

## 2. The vertex (0x5CE920) [C]

Grid: vertex (i, j), i = 0..w+1 (x), j = 0..h+1 (y), stored row-major, index j * (w+2) + i. It stands on the height
sample (sx, sy) = (tile+0xDC + i - 1, tile+0xE0 + j - 1): the tile's 65 samples plus one sample of margin on each
side (67 = 65 + 2). All arithmetic is IEEE single precision (SSE scalar), conversions to bytes truncate.

| Bytes | Field | Rule |
|---|---|---|
| 0-3 | position x | x87 at double precision, rounded to float once: x = float(t + t - 1), t = double(i) / double(w+1) (the only non-single step; a single-precision version differs in the last bit for 2,144 of 4,489 vertices) [C, L] |
| 4-7 | position y | u = float(j) / float(h+1); y = 1 - (u * 2) |
| 8-11 | position z | 0.5 |
| 12-15 | sample x | float(sx), not wrapped (can be -1 or samplesX) |
| 16-19 | sample y | float(sy), not wrapped |
| 20-23 | world x | TR8+0x1AC4 * float(sx) |
| 24-27 | world y | TR8+0x1AC8 * float(sy) |
| 28-31 | height | (float(h16) * (1/255)) * TR8+0x1ACC, h16 = the u16 height at the wrapped sample (below) |
| 32-39 | 8 weights | slot k = byte(int(W_k * 255)) (truncation), see 2.2 |
| 40-47 | 8 ids | slot k = low byte of the tile's layer id k (tile+0x1FC[k]); 0 for k >= count |

Wrapping for the height read: x' = (samplesX + sx) mod samplesX, y' = (samplesY + sy) mod samplesY, unsigned, with
samplesX/Y = TR8+0x1AA8/+0x1AAC; the y coordinate wraps too (a tile in row 0 reads the last row at j = 0). Then the
height field's own x wrap (0x548980, when 0xD90FBC == 1) and its tile directory (constant or pooled 16 x 16 tile, as
in `heightfield.h`); no clamping. TR8+0x1AC4/+0x1AC8/+0x1ACC are the plot steps [I: 1.0, 0.866, 0.2 like
0xD91270+0xA8..; to be read live].

### 2.1 Slope class of the vertex

The normal n of sample (sx, sy) comes from 0x728240, a block function over the job's block (the same code as 0x747580
that the list builder 0x568430 uses, terrain-bake-records.md 2.2 step 3: identical signatures and bodies [C]). Class:
`nz > 0.9` -> 0 (top-down); else 1 when |ny| > |nx| (x87 `fabs`/`fcomip`, strict), else 2 [C]. A NaN nz gives a
slope class (the `jbe` is taken on unordered).

**Settled 2026-11-13 by issue 3405** (replaces "the normal itself ... not yet re-implemented here"): 0x728240 is 0x747580
with a 16 x 16 window instead of 76 x 76, nothing else differs [C]; the normal is a pure function of the sample
(terrain-bake-records.md 2.2a's filter, x wrapped and clamped, y clamped, D3DX's normalisation = `rsqrtss` + one
Newton step); vertex (i, j) uses sample (ox + i - 1, oy + j - 1); the same single-precision normal gives the slope class
here and, as halves, the CacheBake vertex's NORMAL. Details and checks: terrain-bake-passes.md 5.1. The server's
classes: `c5r::bake::tileSlopeClasses` (`render/include/c5r/bake/grid.h`), the `SlopeClasses` `fillBlendVertices` takes.
Check [L]: the normals equal the EXE's CacheBake halves bit for bit on all 107,736 vertices of the 24 captured bakes
(so the classes agree except for a float within half a half-ulp of 0.9, which no compared vertex had); the blend
vertices made with these classes equal the EXE's on all 112,225 vertices of the 24 captured bakes and one PID catch
(terrain-bake-passes.md 5.1, test `game_blend_vertices_with_these_slope_classes_equal_the_exe`). None of those tiles has
a slope-projected material within the kernel, and no bake of the 10 slope tiles of `C5H-3402-SLOPE` was caught live
(terrain-bake-passes.md 5.1a: the vertex arrays are freed within a frame, mostly before a read completes), so the slope path rests on the normal's
bit-exactness, not on a slope tile's weights.

### 2.2 Per-layer weights at a vertex (0x527720) [C]

1. W[l] = 0 for all l < layerCount (TR8+0x1110).
2. For each of the K = TR8+0x3CC kernel taps t (in table order): dx = TR8+0x000[t], dy = TR8+0x144[t], weight
   c = TR8+0x288[t] (float). Material byte m at ((dx + sx) mod samplesX, (dy + sy) mod samplesY), **computed in
   unsigned 32-bit arithmetic without adding samplesX first** (so -3 becomes 0xFFFFFFFD mod samplesX: right only when
   samplesX is a power of two; on Tiny, samplesX 3,584 maps -3 to 2,045, not 3,581: an EXE quirk at the x = 0 column
   and the y = 0 row), then the height field's x wrap and byte layer (0x517BF0 = 0xD91270+0x4C, as in 0x568430).
   Layer l = TR8+0xC10[m]; when the slope class is not 0 and TR8+0xB10[m] == 1, l += class (1 or 2). The class is the
   **centre vertex's**, applied to every tap. W[l] += c.
3. Sum S over l in pairs: even indices into one accumulator, odd into another, the last odd one alone;
   S = (odd + even) + last. inv = 1 / S; W[l] = W[l] * inv for every l.

**The kernel [C]**: written once by 0x6BB2B0 from the renderer's start-up 0x678290 (unconditional): 25 taps, dy outer
-2..2, dx inner -2..2, c(dx, dy) = k[dx] * k[dy] with k = (0.125, 0.636056, 1, 0.636056, 0.125) (constants 0xA02860,
0x9DBB20), all single precision: for each row factor f = k[dy], a = f * 0.125, b = f * 0.636056, the row's taps are
(a, b, f, b, a) and the running sum becomes ((((a + b) + f) + b) + a) + sum; at the end inv = 1 / sum and every tap
c = inv * c. The live table equals this bit for bit (section 3).

### 2.3 From layer weights to the 8 slots [C]

Slots follow the tile's list order (tile+0x1FC, count tile+0x1F8, at most 8). W_k = W[id_k]. Weights of layers not in
the list are dropped without renormalising (the list covers samples -2..w+1 with the samples' own slope classes; the
kernel reaches -3..w+2 with the centre's class). If the sum of the listed W_k (even and odd pairs, then the last) is
below 0.001 (a double constant 0x9F9268 compared as float), W_0 = 1. Bytes: int(W_k * 255) truncated. Unused slots:
weight 0, id 0.

With the global 0xD90F24 != 0 (TerrainUseAdvancedGPU, pass 1) the weights are not written and byte 40 holds the slope
class [C]; not the configuration of the captures (pass 2).

### 2.4 Index order (0x5ABE70) [C]

Indices are relative to the tile's first vertex, vertex (x, y) = y * gw + x for a gw x gh grid. For x0 = 1, 7, 13, ...
(bands of 6 quad columns), for y = 1 .. gh-1, for x = x0 .. min(x0+5, gw-1), with A = (x, y), B = (x, y-1), C =
(x-1, y-1), D = (x-1, y): even x emits A B C, D A C; odd x emits A B D, D B C. (gw-1)(gh-1)*6 indices, 16-bit. (The
EXE's function takes a step argument, 1 here; with step s it walks every s-th vertex.) Equal to the captured index
buffers [L, section 4].

### 2.5 The pass (TerrainShaderScreen, material 76) [C, states L in 4]

- With 0xD90F28 != 0 (spec maps on) and 0xD90F24 == 0: **pass 2, VS 0x067D9550 + PS 0x067F1E40** (SM5; terrainsets
  `TerrainShaderScreen` pass 2). The VS reads cb0[0].xy (added to the position) and cb2 = TerrainLayers[96] (x =
  texture scale, y = projection) indexed by the vertex ids; the PS reads no constant buffer, samples t0-7 (diffuse,
  s0), t8-15 (height, s1, .x), t16-23 (spec, s2) for each slot whose weight > 0.01, squares the colours, weights each
  layer by (diffuse alpha^4 * weight) and writes the normalised sums to o0 (albedo), o1 (height), o2 (spec).
- Draw object: 24 textures, t i = TR8+0x708[k_i], t 8+i = TR8+0x908[k_i], t 16+i = TR8+0x808[k_i] or the dummy
  TR8+0xB08 when the set has no spec map; all three = TR8+0xB08 for i >= count (k_i = TR8+0xF10[id_i]).
- **cb0 (own block, 0x4C0 B in a 1,280 B buffer)**: [0].x = -1/(2 * 0xD90F2C), [0].y = +1/(2 * 0xD90F2C)
  (0xD90F2C = the largest cache size, 512); [1..8] per slot (scale as in TerrainLayers, projection, 0, 0); [9..72] per
  texture set k < 64 (slot of its top-down layer, slot of projection 1, slot of projection 2, TR8+0xB10[k]); [73].xy =
  float(tile+0xE4), float(tile+0xE8). Only [0].xy is read by pass 2's shaders.
- **TerrainLayers (shared id 18, 0x600 B), written per frame by 0x6C47F0**: for each layer l < layerCount, with
  scale s = TR8+0xE10[TR8+0xF10[l]], p = TR8+0x1010[l], X = float(mapW * 64 - 1) (0xD90FD8 - 0xD90FCC = 0.5X - (-0.5X),
  written by 0x535F07..): x = float(int((s * 0.33) * X)) * (1/X) * 3.030303 when p == 0, else
  float(int((s * 0.66) * X)) * (1/X) * 1.515152; y = float(p); z = w = 0.

### 2.6 The 24-byte CacheBake vertex (tile+0x308; for W3) [C]

Same grid and loop: float3 position (t*2 - 1 - 1/size, (1/size - u*2) + 1, 0.5) with size = float(tile+0x17C), then
six halves made by `D3DXFloat32To16Array`: NORMAL (nx, ny, nz, 1.0) of the vertex's normal (the one the slope class
comes from) and TEXCOORD0 (i/(w+1), j/(h+1)). Corrected 2026-11-13 by 3405 (was: "1.0, and values of the slope class;
not decoded further"); terrain-bake-passes.md 5.1.

## 3. Live values (Tiny save, 2026-11-13 03:26 EST) [L]

Read by PID (`render/tests/bakeblend/tools/bake_inputs.py`) from c5h-c, CP/VP/EUI, stock `config.ini`, DLL
`i0ship-bbfacca1-ia` (SHA-256 e589306e...5c54, the current i0-ship pin), save copy `C5H-3402-TINY` of
`c5h-9028-tiny-t0008` (Tiny 56 x 36, turn 9):

- kernel: 25 taps, dx/dy and weights **bit-equal** to the rule of 2.2 (test `ref_kernel_equals_the_live_table`);
- TR8+0x1AC4/+0x1AC8/+0x1ACC = 1.0, 0.866, 0.2 (bits 3f800000, 3f5db22d, 3e4ccccd); samples 3,584 x 2,560;
- 0xD90F24 = 0, 0xD90F28 = 1 (pass 2), 0xD90F2C = 512; 0xD90FCC / 0xD90FD8 = -1791.5 / 1791.5 (X = 3,583); wrap 1;
- layer count 48; the slope-projected materials 4, 8, 17 map to layers 4-6, 10-12, 21-23; **no tile of the Tiny map
  lists any of them** (Tiny has no Mesa, Gibraltar or crater), so the slope path is not covered live.
- List lengths on Tiny: 1 layer 1,610 tiles, 2: 226, 3: 250, 4: 129, 5: 25 (2,240 tiles).

## 4. Checked against RenderDoc captures [L]

Session 2026-11-13 03:23-03:38 EST, same instance and setup, RenderDoc 1.47 preload (D57), 21 captures in
`D:/c5-64bit-work/3400/w2/captures/` (not committed). Bakes were caught at the load screen (first capture only) and
while panning at close zoom with an arrow key held (the camera moves, so the EXE bakes one tile per frame; captures right
after a zoom or a `UI.LookAt` caught none). Blend draws extracted with `render/tests/bakeblend/tools/make_ref.py`:

| Draw | Tile (x, y) | Layers | Vertices equal |
|---|---|---|---|
| frame1293_c1970 (load screen) | 53, 9 | 43 | 4,489 / 4,489 |
| frame114032_c1478 | 21, 20 | 43 | 4,489 / 4,489 |
| frame147879_c2242 | 7, 21 | 15 | 4,489 / 4,489 |
| frame148903_c1781 | 7, 35 | 43 | 4,489 / 4,489 |
| frame218923_c1746 | 24, 18 | 43 | 4,489 / 4,489 |
| frame218011_c2171 | 13, 24 | 15, 16 (77 vertices with split weights) | 4,489 / 4,489 |
| frame228673_c1933 | 48, 17 | 24, 25, 27 (3,010 vertices with split weights) | 4,489 / 4,489 |

**Second session, 2026-11-13 04:00-04:13 EST** (c5h-a, same DLL, mods and settings; c5h-a's import wrapper replaced
for the session by the current build and restored by hash afterwards): to get slope-projected materials onto Tiny, the
Tiny save was loaded, Grand Mesa (feature 9) put on plot (10, 15), Rock of Gibraltar (12) on (40, 18) and Barringer
Crater (7) on (47, 13) through Lua (`Plot:SetFeatureType`), saved as `C5H-3402-SLOPE` and reloaded: the load built the
height field with them, and 10 tiles now list slope-projected layers (Mesa 4-6, Gibraltar 10-12, Crater 21-23; e.g.
tile (10, 16): 4, 5, 6, 7, 8, 15, 16, 26). Inputs dumped to `D:/c5-64bit-work/3400/w2/ref-slope/`, 26 captures in
`captures2/` (saves kept in `D:/c5-64bit-work/3400/w2/saves/`). 17 blend draws were caught (approaching a place with
an arrow key held at close zoom catches a bake in the first capture almost every time), **none of them of the 10 slope
tiles**: 1 single-layer, 16 multi-layer (2 to 5 layers, e.g. tile (10, 20): 13, 14, 15, 17, 38). **All 17 byte-equal.**

Over both sessions: 24 draws, 22 different tile lists (6 with 1 layer, 7 with 2, 6 with 3, 2 with 4, 1 with 5), every
vertex byte-equal.

- **The rule of section 2 reproduces every captured vertex byte for byte** (position, sample, world, height, 8 weights,
  8 ids), multi-layer tiles included, with slope class 0 everywhere (no slope-projected material on these tiles).
- The index buffers equal the rule of 2.4 (26,136 indices each).
- The captured VS/PS are the EXE's blobs 0x067D9550 (4,000 B) and 0x067F1E40 (6,300 B): same SHA-256.
- States as captured: blend ONE+ONE add with **IndependentBlendEnable = false, so additive on all three targets**;
  depth off (LESS_EQUAL, no write); rasterizer solid, cull none, FrontCounterClockwise, depth clip on; three samplers,
  all the same state (MIN_MAG_MIP_LINEAR, wrap, LOD 0..FLT_MAX); viewport 512 x 512; targets R16G16B16A16_FLOAT 512^2,
  10 mips, GENERATE_MIPS, cleared to 0; `GenerateMips` on all three after the draw; the dummy texture is 32 x 32
  R8G8B8A8, all zero; cb1 (TerrainFrame, 592 B) is bound but not read.
- cb0: [0] = (-1/1024, +1/1024) as 2.5 says; slots [1..count] as 2.5. **The rest of the block holds values of earlier
  tiles** (the EXE never clears it): a whole-buffer comparison is meaningless, only the written parts compare.
- cb2 (TerrainLayers, 1,536 B): entries l < 48 equal the rule of 2.5; entries >= 48 hold stale bytes.

### 4.1 The server's blend pass against the captures (2026-11-13 03:53 EST, RTX 4080, hardware device) [L]

`render/src/bakeblend` (`c5r::bake`): `fillBlendVertices`, `buildBlendIndices`, `fillOwnBlock`, `fillTerrainLayers`,
`BlendPass` (the EXE's VS/PS from the `.shader` section, states as captured), `BlendTextureCache` (diffuse / height /
spec by the CSV's names through `c5r::gamefs`). Test `c5r_bakeblend_test` (`render/tests/bakeblend`), 19 tests,
0 failed, 0 skipped; reference data `C5R_BAKE_REF` = `D:/c5-64bit-work/3400/w2/ref-tiny;D:/c5-64bit-work/3400/w2/ref-slope`
(not committed). Figures below are for both sessions (24 draws); the first commit of the module had the 7 of ref-tiny.

- Vertex buffers: 24 of 24 draws byte-equal (4,489 vertices each).
- Index buffers: 24 of 24 equal. Kernel: equal to the live table bit for bit (both sessions).
- Constant buffers: cb2 equal for all 48 layers (x, y); cb0 [0].xy, the slots' entries and [73].xy equal.
- Textures: every layer texture the server loads by name equals the captured texture subresource by subresource
  (BC3; diffuse 512 or 1,024, heights 512 to 2,048, as the files are: no mip was dropped).
- **Blend targets: the server's targets equal a replay of the captured inputs (captured vertex, index and constant
  buffers and captured textures, same states, same GPU) on every texel and channel of all three targets for all 24
  draws** (equal share 1.0000, max / median / p95 difference 0 float16 units; per-channel figures in
  `bakeblend_targets.csv` of the test's work folder). With the inputs byte-equal this is what is expected; it shows that
  the pass setup (states, slots, samplers, cb0 offset, dummy texture, mips) adds no difference. The EXE's own target
  contents are not in the captures (RenderDoc stores inputs; a replay re-runs the draw), so the replay is the reference.

### 4.2 Two findings about the inputs [L]

- **TerrainDef.csv: the EXE reads the loose DLC copy, gamefs's name lookup does not.** The live set scales (TR8+0xE10)
  equal `Assets/DLC/Shared/Terrain/TerrainDef.csv` row for row; `c5r::gamefs` resolves the name to
  `Resource/Common/TerrainModels.fpk:terraindef.csv`, an older table (row 0 has diffuse "NULL"; 12 of 41 scales differ,
  e.g. Generic_Mesa 0.03 vs 0.05, Gibraltar 0.2 vs 0.1). A server that loads the CSV by name gets wrong scales. The test
  reads the DLC file by path; the lookup rule needs fixing in gamefs (asset-pipeline.md 1.2) or the loader must take
  the DLC path. For W1 (3401, CSV -> LayerTables).
- **The EXE has 42 texture sets, the CSV 41 rows**: set 41 (layer 47) has scale 0.01 and no CSV row; no Tiny tile lists
  layer 47. Where set 41 comes from is not traced [open, for 3401].

## 5. Not verified / open

- The normal filter of 0x728240 / 0x747580: ported by 3405 (2.1, 2026-11-13; `c5r::bake::tileSlopeClasses`). The
  rest of this point is the state before that: needed only where a slope-projected material (sets 4, 8, 17) is within
  2 samples of a vertex; the module takes the slope classes as an input (empty = all 0). The filter is a 4-lane SSE function over a 16 x 16 sample block (the
  12 x 12 job block plus 2 on each side): heights * (1/255), per-direction weights from the squared height
  differences with a rational fall-off and factors at height field +0xFF6C.. [C, read only far enough to size it; not
  described here]. **No live check of the slope path yet**: the save `C5H-3402-SLOPE` (on D, section 4) has 10 slope
  tiles and their inputs are dumped, but none of their bakes was caught; the huge save runs out of 32-bit address space
  under RenderDoc (renderdoc-capture.md 4). Once the filter is ported, a capture of one of those tiles checks it; until
  then, a capture alone can show the class per vertex (try 0, 1, 2 against the captured bytes).
- The 0xD90F24 != 0 path (TerrainUseAdvancedGPU, pass 1) and the 0xD90F28 == 0 path (pass 0, 16 textures).
- Tiles in the last column or row (extent 64, 66 x 66 grid, 25,350 indices): the code takes the extent as an input;
  no such bake was captured (they are water or polar on Tiny).
- How a bake is triggered on purpose: captures right after a jump or a zoom caught nothing; the first capture after
  starting to pan (arrow key held, close zoom) caught one bake in 17 of 21 tries (second session). A way to force the bake of a given
  tile would make the slope check cheap.
