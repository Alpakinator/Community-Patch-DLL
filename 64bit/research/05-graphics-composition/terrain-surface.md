# The terrain surface drawn by the server (epic 3200, seam A of package I2-E)

Written 2026-11-11 by team-3200 W1 (issues 3201, 3202). What `render/` now draws: the EXE's height field, through the
EXE's own tessellated TerrainShader programs, at a camera from the camera block or the ground truth; the premises it
rests on (P11, P16, P17 of `docs/briefs/epic-3200-terrain-surface.md`); the offline comparison with the stock shots.
Code: `render/include/c5r/terrain/terrain.h`, `render/src/terrain/terrain.cpp`, `render/tools/terrainshot/`, tests
`render/tests/terrain/`. Addresses are DX11 VAs at ImageBase 0x400000. Marks: [C] decompile, disassembly or dump;
[L] live game; [I] inferred.

## 1. Premises (static, 2026-11-11 10:50-11:05 EST, each within its 15 minutes)

Sources: the SM5 disassembly under `D:/c5-64bit-work/2900/2902/dump/asm/` (`067d6490_vs_5_0.asm`, `067f98f0_hs_5_0.asm`,
`067fa080_ds_5_0.asm`, `067e8d68_ps_5_0.asm`, `067e6e58_ps_5_0.asm`, `067ea680_ps_5_0.asm`), the listing
`D:/c5-64bit-work/2900/2903/dx11.asm`, the decompile `D:/c5-64bit-work/2900/2903/out/r6.txt` (0x5c37e0), the strings
`D:/c5-64bit-work/2900/2903/strings_all.txt`, and the c5h-d instance's `GraphicsSettingsDX11.ini` (read-only).

### P11: TerrainFrame, TerrainTile and DistanceFog suffice for a draw - holds [C], and [C, run] once the draw runs (section 3)

Every register the tessellated pass reads is a traced member (asset-pipeline.md 3.3):

| Stage | Reads | Members |
|---|---|---|
| VS 0x067d6490 | cb0[4], cb0[6].xyz; cb1[16].z, cb1[22].xy; t0 / s0 | TileOffset, LodStep, LodMip; HexScale.z, HeightTexelScale; the tile height texture, sampler state 0 |
| HS 0x067f98f0 | cb1[14].x | TessFactor (all four edges and both inside factors); passes the control point's height, and control point 5's local position as the patch constant |
| DS 0x067fa080 | cb0[0..6]; cb1[0..13], cb1[16] | TileWorld, TileOffset, InvGridSize, LodStep; ViewProj, OverlayProj, FOWAnim, BicubicBasis, HexScale |
| PS 0x067e8d68 (pass 1) | cb1[19], cb1[25], cb1[28], cb1[31..34]; cb2[0..1]; t0-t7 | CameraPos, Arg6, FlagC, ShadowMatrix0; FogColor, FogParams |

What the shaders compute [C, disassembly]:
- **VS**: `i = id & 255`, `j = id >> 8`; `local = (i - 3, j - 3) * LodStep`; height texel = `local + TileOffset.xy +
  TileOffset.zw + 0.5`, times `HeightTexelScale` = (1/1024, -1/1024), sampled at mip `LodMip` with sampler state 0
  (linear, wrap); output height `z = texel.w * HexScale.z * 255` and `local`. **The height is the texel's ALPHA**
  (`sample_l_indexable ... t0.wxyz` into `.x`) [C; and run, 2026-11-11 12:25 EST: an R32_FLOAT texture, whose alpha
  reads 1, drew every sample at z 51.0 = 1 * 0.2 * 255]. So the EXE's per-tile height texture has an alpha channel
  (format [I]; RGB may hold the normal that NormalCalculate makes).
- **HS**: quad domain, integer partitioning, triangle_ccw, 16 control points in and out, every factor = TessFactor.
- **DS**: `z` = bicubic over the 16 control heights with the rows of `BicubicBasis` for both directions (weights at a
  domain value t: `t^3*row0 + t^2*row1 + t*row2 + row3`; at t = 0 they are (1/8, 3/4, 1/8, 0), so the surface does
  **not** pass through the samples; it is the EXE's smoothing, reproduced exactly); `xy = (domain * LodStep + local of
  control point 5 + TileOffset.xy) * HexScale.xy`; world = `(x, y, z, 1) * TileWorld`; clip = world * ViewProj; the
  cache uv = `(local + 2) * InvGridSize`; the overlay uv through OverlayProj; the FOW base uv `(world.xy + FOWAnim.zw)
  * FOWAnim.xy`; world position to TEXCOORD2.
- **PS (pass 1)**: colour = `t0(uv)` only when `FlagC` (TerrainFrame 0x1c0) is non-zero, else black; shadow: lerp to
  `LUT7(colour)` by `saturate(t3(ShadowMatrix0 screen uv)) * Arg6` (*2026-11-14, 0190: not a screen uv: p = (world, 1) *
  ShadowMatrix0, uv = (0.5 p.x / p.w - 0.5, -0.5 p.y / p.w - 0.5), section 9*); overlay `colour * (1 - t1.a) + t1.rgb`; FOW: lerp
  from `t5` by `t4.x`, then to `LUT6(colour)` by `t4.x * (1 - t4.z)`; distance fog by FogParams and the ramp `t2`;
  alpha 1. *Corrected 2026-11-14 by issue 3953 (`fog-of-war.md` 3.3, 3.4, 7.4): the LUT weight reads the mask's z (B,
  visible), not y; was "`t4.x * (1 - t4.y)`". The mask's channels are R explored, G unexplored, B visible [C, L]; the
  module's tests confirm the PS's behaviour per class (7.4).*
- Consequence: `TileWorld` (the translation 0x5c37e0 builds from minus the floats at TR8+0x1ab0/+0x1ab4, TR8 =
  tile+0x13c) must be (-ox, -oy, 0): the DS adds it after HexScale, and world x = sample * sx - ox is the known relation
  (asset-pipeline.md 4.2 point 5) [C for the order, I that TR8+0x1ab0 holds ox]. TerrainFrame.MapSize (0x180) is read
  by none of the pass-1 programs, so its meaning does not matter here.

### P16: tile size, height texture, tile grid - not settled statically [I]; the module takes the tile size as a parameter

Static facts [C]: TerrainTile is written per LOD object by 0x5c37e0 from tile+0xd4/+0xd8 (InvGridSize = 1/(d4+1),
1/(d8+1); LodStep = (d4-1)/(n-1) with n halved tile+0x1c4 times, so **at LOD 0 one patch is one height sample**),
tile+0xdc..+0xe8 (TileOffset) and TR+0x2424 (LodMip). The VS reads the height texture in texel units divided by
1024 under wrap addressing, so the EXE's height texture is addressed as a 1024 x 1024 page (with TileOffset.zw the
tile's place in it) [I: a paged or ring texture; the `Terrain64Chunks`..`Terrain512Chunks` pool settings, 4096 / 512 /
256 / 64 chunks, suggest pools of 64..512-texel pages]. The vertex-ID scheme limits a tile's vertex row to 256.
Not found within the 15 minutes: the writer of tile+0xd4..0xe8 (0x50d2d1 and 0x5da0c1 only zero them in
constructors), the texture format, and TR+0x2424's runtime value. Issue 3204 (W3) reads them live.

What the module does instead (section 2): a tile is `tilePlots` x `tilePlots` plots (default 2, allowed 1..3), one
height texture per tile (height in alpha) with a border of 3 samples, the EXE's VS addressing it through TileOffset.zw and
HeightTexelScale = (1/S, -1/S) for the texture side S. **The tapped colour caches land right only when tilePlots and
the cache uv convention (InvGridSize, taken as 1/(P + 2) with P samples per tile edge [I]) equal the EXE's**; the
geometry does not depend on it.

### P17: the pass at the owner's settings - pass 1 [C static + settings file; L not seen]

0x5c37e0 [C]: `TerrainTessLevel` (0xd90f70) != 0 and `BicubicTerrainTessellation` (0xd90f1c) != 0 select **pass 1**
(VS 0x067d6490, HS 0x067f98f0, DS 0x067fa080, PS 0x067e8d68); tile+0x2fc != 0 selects pass 0 instead (PS 0x067e6e58,
which also reads t8 and t9); TessLevel != 0 with Bicubic 0 selects 4, TessLevel 0 selects 6 (both untessellated).
The settings are registered at 0x5d8e22 (0xd90f70 = "TerrainTessLevel", 0xa1b6fc), 0x5d932a (0xd90f1c =
"BicubicTerrainTessellation", 0xa1bf00) and 0x5d9380 (0xd90f14 = "BicubicTerrainTessSubdiv", 0xa1bfb8), all in
`[GraphicsDetailSettings]`. The start-up clamp at 0x454038..0x454138 zeroes TessLevel when a capability bit is missing,
zeroes Bicubic below shader level 10, and rounds Subdiv down to a power of two.
c5h-d's `GraphicsSettingsDX11.ini` (read 2026-11-11 10:55 EST): `TerrainTessLevel = 2`, `BicubicTerrainTessellation =
1`, `BicubicTerrainTessSubdiv = 2`, `TerrainDetailLevel = 3`. On the RTX 4080 (shader level 10): **pass 1, TessFactor
= 2** at LOD >= 0 (0x4241e0: `TR+0x2428 = [0xd90f14]` when the LOD value is >= 0, else `(1 << int(lod * k)) *
[0xd90f14]` with k = [0x9f2544], and LodMip 0). The module's default is pass 1, tess factor 2, LOD 0 everywhere.
What tile+0x2fc means is not known [I].
*Corrected 2026-11-14 by issue 3019 (`terrain-colour-reference.md` 1) [L, two stock captures on Tiny]: tile+0x2FC is
"the tile has a spec cache t8" (`terrain-bake-records.md`), and **pass 0 is the common case**: 145 of 150 and 57 of 58
terrain draws use the pass-0 PS 0x067e6e58 (t8, t9 bound), the rest pass 1. LodMip (TR+0x2424) read live: **2.0 at zoom
level 17** (zoomed out), 0.0 at zoom 11; TessFactor 2.0 at both. The server still draws pass 1 at LodMip 0. Replaced:
"at the owner's settings - pass 1" as the pass of the frame.* *Done 2026-11-14: the LOD by issue 0191 (section 7), the pass per
tile, t9 (= `specscreenmap.dds`) and FlagB by issue 0189 (section 8): the server draws pass 0 where the tile has a t8.*
**The LOD value [C, partial; 11:20 EST]:** 0x4241e0 takes the minimum over the visible tiles (list TR+0x1af0, count
TR+0x2334) of 0x6497c0(tile, ViewProj), with hysteresis against TR+0x242c/+0x2430. 0x6497c0 = log2-like of
`(max(tile+0xd4, tile+0xd8) - 1) / (0x744ae0(tile, ViewProj) * [0x9fcf90] / BicubicTerrainTessTriSize (0xd90f18, 4 at
the owner's settings) / BicubicTerrainTessSubdiv)`, clamped by [0x9f7b40] and [0x9ed65c] (0x744ae0 [I]: the tile's projected
size). So the EXE picks one LOD per frame from the nearest tile's screen size: LOD >= 0 gives TessFactor = Subdiv (2) and
LodMip = floor(LOD) (the height texture's mip, i.e. smoothed heights when > 0); LOD < 0 gives LodMip 0 and a larger
TessFactor. The module draws LodMip 0 (only mip 0 is built) with TessFactor 2; if the stock frames ran at LodMip >= 1
the EXE's surface is smoother than ours by one mip level. TR+0x2424 / +0x2428 live values: issue 3204 [I until read].
**Measured 2026-11-14 (issue 0154, renderer-in-server-design.md 4.16, last block) [L]:** in stock frames LodMip = L,
**LodStep = 2^L** (tile+0x1C4 = L on every visible tile, so the vertex grid thins with the mip), TessFactor 2; on the
workstation (2554 x 1411, the owner's settings) L = 0 at camera height 261 (3 ticks in), **L = 1 at the default zoom (z 424.26,
9055's and 3200's default views)**, L = 2 at z 640 (five captures); on the huge save on oldpc (1914 wide) LOD 1.98 / 2.02 /
2.02 at the load view, Thebes, Akhetaten (L 1, 2, 2). The projected size 0x744AE0 is multiplied by the back-buffer width
`[0x301056C]` [C, live disassembly], so the LOD falls by log2 of the width ratio on a wider window. So **at the default zoom
the EXE draws mip 1 at LodStep 2**, not mip 0: its surface is 0.05-0.12 world units lower on average (truncating mip), up
to ~0.28. Corrects "LodMip = floor(LOD)" above only by adding LodStep; the pass and TessFactor are as stated.
*Completed 2026-11-14 by issue 0191 (section 7): the "log2-like" value is exactly clamp(0.5 + log2((max(d4, d8) - 1) /
max(1, p * 0.5 / TriSize / Subdiv)), -2, 4), p = 0.25 * width * the largest NDC diagonal of the tile's full-height box; the
list is the class-3 tiles whose bounding sphere is in view; the hysteresis rule and the module's implementation are there.*

## 2. The draw (`c5r::terrain::Renderer`)

- **Programs**: the EXE's VS 0x067d6490, DS 0x067fa080, HS 0x067f98f0 (passes 0, 1) / 0x067f9cb8 (pass 2) and PS
  0x067e6e58 / 0x067e8d68 / 0x067ea680, read from the EXE file at run time (`c5r::shaders::ShaderSection`, VAs from
  `c5r::terrainsets`). Own HLSL (compiled at init): the PS for colour sources Material, Constant, Height, Normal and
  WorldPos (same input signature as the DS output), the water VS/PS.
- **Height textures**: one per tile, R16G16B16A16_UNORM, S = P + 6 texels a side (P = tilePlots * 64 samples), texel
  (u, v) = sample (tile origin + u - 3, + v - 3), rows stored flipped (the VS's negative y scale under wrap reads row
  S-1-v), **alpha = the raw u16** (rgb 0), and TerrainFrame.HexScale.z = sz * 65535/65025 so that the VS's
  `alpha * HexScale.z * 255` is exactly `u16 / 255 * sz` = HeightAtGrid 0x6D8B60 [I: the EXE's own texture format and
  scale are not known (P16)]. *Corrected 2026-11-13 by issue 0134 (renderer-in-server-design.md 4.16): the EXE's own
  texture is the A8 page of high bytes with HexScale.z 0.2 (terrain-bake-passes.md 5.0, 5.3), so the EXE draws
  `(u16 >> 8) * 0.2`, up to 0.25 below HeightAtGrid; the module now writes alpha = (u16 >> 8) * 257 with HexScale.z = sz
  and draws that height. The text of this bullet describes the module before the change.* *Replaced 2026-11-14 by issue
  0191 (section 7): the module binds the EXE's own 1024 x 1024 A8 pages with all 11 mips; no per-tile texture is left.*
  Samples outside the grid read 0 (as HeightAtGrid), x wraps when the map wraps. Memory:
  Huge 128 x 84 plots with tilePlots 2 is 2,688 tiles x 134^2 x 8 B = 386 MB of video memory; Tiny 56 x 40 = 560 tiles,
  80 MB (a first version with R32_FLOAT, half of that, drew nothing right: see P11). Built in 12:25 EST's run in well
  under a second on Tiny [not timed].
- **Patches**: one shared index buffer, 16 control points per height sample: patch (a, b) = vertex IDs `(a + c + 2) +
  256 * (b + r + 2)`, c, r = 0..3, so control point 5 sits at local (a, b); P^2 patches per tile, `DrawIndexed(16 P^2)`.
- **Constants**: TerrainFrame by member name from the layout (asset-pipeline.md 3.3): ViewProj = view * proj,
  BicubicBasis, TessFactor, HexScale (sx, sy, sz), MapSize = plots (unread), HeightTexelScale (1/S, -1/S), CameraPos and
  CameraAxisZ from the inverse view, FlagC = 1 (the PS reads its cache only then), Arg6 = 0 (no shadow), ShadowMatrix0 =
  OverlayProj = identity, ScreenSize, OverlaySize 2048; TerrainTile per tile: TileWorld = Translation(-ox + k * W, -oy,
  0) (k = wrap copy), TileOffset = (origin, 3 - origin), InvGridSize 1/(P + 2), LodStep 1, LodMip 0; DistanceFog with
  density 0 (no fog: seam F). PS inputs from other systems are 1x1 constants: overlay (0,0,0,0), fog ramp, shadow, FOW
  mask (1, 1, 1, 1: visible, no TransFOW LUT), spec map white, FOW base black, identity 32^3 LUTs.
  *Corrected 2026-11-14 by task 3704 (epic 0167): the overlay is now an input: `Options::overlay` binds the EXE's
  Terrain Overlay target as t1 and each wrap copy's OverlayProj (`overlay-system.md` section 10); the transparent 1x1
  and identity above remain the default, bit-equal.*
  *Corrected 2026-11-14 by issue 0188: DistanceFog is now the EXE's block from the camera (density 0.05, colour, scale,
  ramp row) and t2 the install's `fogramp.dds` (section 6); without an install t2 is black and the picture is the one
  before, bit-equal. The "density 0" and "fog ramp" constant above describe the module before 0188.*
  *Corrected 2026-11-14 by issue 0189 (section 8): a tile with a t8 is drawn with pass 0, t9 is the install's
  `specscreenmap.dds` and FlagB 1 (black and 0 without an install); the "spec map white" above describes the module before.*
  *Corrected 2026-11-14 by issue 3953 (`fog-of-war.md` 7.4): the FOW constants above are "FOW mask (1, 1, 1, 1): x = 1
  and z = 1, so the cloud lerp keeps the colour and the LUT weight x * (1 - z) is 0", not "visible, no TransFOW LUT" read
  through .y; they stay the default (Options::fow off, bit-equal). With Options::fow on the module binds the server's
  mask as t4 (sampled through OverlayProj, the overlay's matrices per wrap copy), the cloud base as t5, the tile style's
  TerrainFOW volume as t6 (the install's `TerrainFOW_<Style>_ColorKey.tga`, identity when missing) and FOWAnim at
  TerrainFrame 0x090 (0 before).*
  *Corrected 2026-11-15 (epic 0169; section 9, 11.10, 12.6): the shadow is an input too: `Options::shadow` binds the
  EXE's ShadowBlur as t3 (R16_UNORM, or R16G16B16A16_FLOAT at quality 1 since 4308), Arg6 = the EXE's factor and
  ShadowMatrix0 per wrap copy (0190's module), fed in the product by the shadow tap since 4308; t7 is the tile style's
  TerrainShadow volume (since 4305; Generic's from 0190 to 4305). "Arg6 = 0 (no shadow)", "ShadowMatrix0 = identity"
  and the shadow and identity-LUT constants above stay the default (no shadow bound, bit-equal).*
- **States** (not recovered from the EXE, asset-pipeline.md 3.7): opaque, depth LESS_EQUAL with write, no culling;
  the module's own D32 depth buffer when the caller gives none.
- **Culling**: per tile and wrap copy, the tile's box (one sample of margin, its height range + 1) against the clip
  volume. Wrap: copies at x +- samplesX * sx when the map wraps.
  *Found 2026-11-15 by 4311 (13.6, finding) [L picture, C code]: this is one world unit more than the EXE's wrap period
  (0xD90FE4: 3583 on Tiny, 8,191 on huge; `(samplesX - 1) * sx`, as the water's `worldFromGrid`), so the terrain's wrap
  copies stand 1 world unit (about 4-5 px at tiny9v `x1`) off stock's; the same width feeds `overlayProjForCopy` and
  `shadowMatrixForCopy`. Not fixed when this was written; the lead names the ticket (13.6).*
- **Water** (D58 point 7): one quad over the grid and its wrap copies at waterZ (the grid's 0xD91274: 10.117 on the
  huge map, read from the ground truth's header bytes; P5's "about z 12" is the picking plane, not this value [I]),
  one colour, depth-tested after the terrain.
- **GPU time**: timestamp queries per draw (four in flight), read without stalling at the next draw (`FrameStats::
  gpuMs`, the previous frame's) or with `gpuTime(ctx, wait)`.
- **Load cost**: `setHeightField` on the huge field (8192 x 5376 samples, 2,688 tiles of 2 plots) took 1,125 / 1,111 /
  1,092 ms (three runs, 2026-11-11 12:35 EST, terrainshot, hardware device; one-off figures, not a PERFORMANCE.md
  series): once per load, on the thread that calls it.
- **For the users (W4 server, W3 caches)**: the API is in `terrain.h`; `setTileCache(tx, ty)` uses this module's tile
  grid (`tileGrid()`), which equals the EXE's only once P16 is known; a cache view is AddRef'd and released when
  replaced, cleared or the field changes. Linking from outside `render/`: `d3d11 dxgi dxguid d3dcompiler windowscodecs
  ole32`.

## 3. Results (2026-11-11, render/ at team-3200-w1, RTX 4080 hardware device, no window)

**The draw runs and is the EXE's surface [C, run].** `c5r_terrain_test` (15 tests, 0 skipped; suite 9/9 executables,
0 skipped, 12:35 EST): the world position read back at every pixel of 160 x 120 draws of synthetic fields lies on the
EXE's bicubic patch surface computed on the CPU from the same samples with the BicubicBasis rows: worst |z - surface|
0.0011 world units on a ramp across four tiles (tile seams at x = 0 and y = 0), and 0 of 9,600 wrap-copy pixels off by
more than 0.005 on a field periodic in x; on Tiny's real field at the v1 camera (2554 x 1411, tess 8), 1,205 sampled
terrain pixels, 0 off the surface by more than 0.01; terrain 2,152,064 px and water 1,451,630 px of 3,603,694. The EXE's
pass-1 PS with a red 4x4 cache on tile (0, 0) and the module's constant inputs draws exactly the cache colour there
(3,000 px, 0 other) and the fallback elsewhere: the PS constants (FlagC, Arg6, FOW mask (1, 1), fog density 0) give
the cache colour unchanged.

Frame figures from terrainshot (single draws at 2554 x 1411, tess 2, LOD 0, GPU time from timestamps; not a performance
measurement in the sense of PERFORMANCE.md): Huge 18-29 tiles drawn of 2,688 (the rest culled), 0.39-1.84 ms GPU;
Tiny 13-36 of 560, 0.39-1.53 ms GPU; CPU inside draw() 0.2-1.1 ms.

**Geometry against the EXE's own pixels: the hex-highlight band (method of `grid_accuracy.py`)**, at the one camera
per map where the ground truth has the EXE's highlight (`camera_validate.json` = the v3 camera; 5 plots outlined;
`render/tools/terrainshot/highlight_accuracy.py`). For each magenta pixel the drawn surface point under it is mapped
to the nearest outline point and projected back; the error of a cross-section (1/20 of a hex edge) is the median
signed screen distance of its band pixels (the band is 10-15 px wide, so its centre, not its pixels, is what lines
up); cross-sections cut short by models (< half the typical pixel count) and the corner twentieths (the band is
mitred) are dropped; the outline scale is fitted (0.8975 huge, 0.900 Tiny, against 0.9-0.925 in validation.json).

| Map | Cross-sections | Median | 95th pct | Within 1 px | Within 3 px | Mean signed |
|---|---|---|---|---|---|---|
| Huge t350 (`camera_validate`) | 297 | **0.82 px** | 4.98 px | 58 % | 87.5 % | +0.47 px |
| Tiny t18 (`camera_validate`) | 195 | **0.97 px** | 2.98 px | 54 % | 94.9 % | -0.12 px |

With the corner twentieths kept: huge median 0.87 / p95 4.88, Tiny 0.99 / 3.60. Against the definition of done (median
<= 1 px, 95 % within 3 px): the median holds on both maps; the 95 % share holds on Tiny (94.9 %, at the edge) and not on
Huge (87.5 %). Where the large errors are (`render/evidence/3200/huge_validate_highlight_sections_512.png`, red = >= 3 px):
hex corners, band stretches beside the river, and band pieces half covered by city models. The huge plots are all flat
at z 12.02, so these errors cannot come from the drawn heights [I: they are the method's floor at this band width];
on Tiny the cross-sections with surface z < 11 (next to water) have median 2.2 px against 0.94 for z 11-13 (the EXE raises
its outline over water, bug 3012). Not separated: how much of the residual is the EXE's LOD choice (LodMip >= 1 would
smooth its surface; P17 above).

**Geometry against the six stock shots: not established [C, negative result].** `compare_shots.py` (masked normalised
template match per 48-px block, the correlation of `shift_match.py`) finds no block with a clear peak on five of six
shots and one on Tiny v1: the stock picture is dominated by the baked texture colour, and the drawn relief (normal
shading of an untextured surface) does not correlate with it in band-passed luminance or gradient magnitude (probe on
huge v1 with four features: NCC peak median 0.14-0.26 per block, the 12-47 blocks with a peak > 0.5 at shifts of about
10 px in no common direction, i.e. false matches). Nor does matching the light: shading the drawn normals (from the
world image) with 48 sun directions gives at best a global correlation of 0.10 with the stock luminance (azimuth 330
deg, elevation 35-65 deg; worst -0.09) and still 0 of 1,059 blocks with a clear peak (huge v1, 12:45 EST): the stock
picture carries almost no relief shading at these zooms. The method itself is
sound: the stock shot against itself shifted by (2, -1) gives (2.000, -1.000) on 1,096 blocks, and every run's self-check
recovers (-3, 2) within 0.007-0.011 px. The coastline is no substitute either: the drawn waterline (z 10.117) lies 8.5-16
px (median per shot) from the edge of the stock shot's blue pixels, because the EXE's shallow-water band is not blue;
this measures the water shading (seam C), not the terrain [I]. So the six-camera geometry check needs the live D33
passthrough comparison of issue 3206 (the same draw composited into the EXE's frame), or a stock shot with a
geometric overlay (hex grid or highlights) at each camera. *Done live 2026-11-11 by issue 3206 (section 5): the D33
passthrough holds no geometric feature (the EXE draws no highlight in D33), so the six cameras were measured with the
stock EXE's highlight band against the drawn surface, and the live frame was shown to be this draw.*

Colour statistics on the comparison mask (reported, not judged: offline has no caches; normal shading against textured
stock): mean absolute difference per channel R, G, B 50.6-97.3, 57.0-97.7, 93.0-126.0; share of pixels within 16 on
every channel 0.4-18 %. Per shot: `render/evidence/3200/offline_summary.json`.

## 4. The offline comparison tools

- `terrainshot (--gt DIR | --c5hf FILE) --camera CAM.json --out OUT.png [--world OUT.f32] [--exe PATH] [--color
  constant|material|height|normal] [--tile-plots N] [--pass N] [--tess F] [--no-water] [--water-z Z] [--warp]`: binary
  `<build>/tools/terrainshot/terrainshot.exe`; exit 0, 1 on any input or device error (the message names the check), 2
  usage; the world image is float32 x, y, z, w per pixel (w 1 terrain, 2 water, 0 none) plus a `.json` with its size.
- `offline_all.py --terrainshot EXE`: the six ground-truth cameras through `quietlock.py run`, then `compare_shots.py`;
  full-size outputs under `D:/c5-64bit-work/3200/w1/offline/`, 512-px evidence and JSON in `render/evidence/3200/`.
- `compare_shots.py`: the block method above. Mask: drawn terrain above waterZ + 0.3, stock pixel not dark, minus the
  HUD (fixed rectangles at 2554 x 1411), minus capture.json's plots with a route, improvement, resource or city (16 px),
  minus a 16-px frame. Limits: capture.json lists only 25 plots around the city; the Tiny shots show unrevealed land as
  white clouds, which the "not dark" test does not remove.
- `highlight_accuracy.py --gt-dir DIR --world WORLD.f32 [--debug-png P]`: the band method above.

## 5. Live at the ground-truth cameras (issue 3206, W5, 2026-11-11 15:00-16:50 EST) [L]

Set-up, data path and server figures: `shared-texture-and-compositor.md` section 14. Per map: one launch in D33 with the
composite (tap + bake tap, tagged insertion) and `shell_server.exe --scene terrain --fallback constant 0xFFFF00FF`
(tiles without a cache in magenta), where at each view the composite is switched by its ini between passthrough and
composite so that the D33 passthrough shot and the D33 + terrain shot share one camera byte for byte
(`shell/tools/terrain_live.py`, run folders `D:/c5-64bit-work/3200/w5/huge2/views`, `tiny1/views`); a second launch in
the stock configuration at the same views with the EXE's hex highlight on five plots (`--stock --highlight`,
`huge_stock`, `tiny_stock`); a third launch only for the plot lists within radius 20 with a visibility field
(`--census`, `huge_census`, `tiny_census`). Comparison: `shell/evidence/3200/w5_compare_live.py` (terrainshot at each
camera + `shell/tools/terrain_compare.py`); results `compare_summary_<tag>.json` in the run folders; 512-px evidence
`shell/evidence/3200/w5_*`.

**Cameras.** UI.LookAt to the plan's plot does not reproduce a camera across launches: the largest view/proj element
difference between this D33 run and the ground truth is 4.3e-2 / 4.7e-2 / 6.1e-4 (huge v1/v2/v3) and 1.6e-2 / 7.6e-2 /
1.0e-2 (Tiny); between this D33 run and this stock run 7.6e-3 / 3.2e-2 / 6.2e-2 and 6.4e-3 / 2.9e-3 / 2.9e-2. **The load
view (no camera move) is identical in all three launches and in W3's (0.0).** Inside one launch every shot of a view
had the same camera (raw block bytes equal).

**Geometry (definition of done 2), full frame, hex-highlight band [L].** The EXE draws no hex highlight in D33 (Overlay
off; 0 band pixels in every D33 highlight shot), so the reference is the stock launch: the band of the EXE's magenta
outline (stock `<v>_stock_hl.png` minus `<v>_stock.png`) against the drawn surface at the stock camera (terrainshot's
world image; the method of section 3, `terrain_compare.geometry_highlight`: per cross-section of 1/20 hex edge the
median signed screen distance, cut-short sections and corner twentieths dropped, outline scale fitted). That the live
composited frame IS this draw: at each camera terrainshot at the D33 run's camera and the live D33 + terrain frame agree
in class (terrain / water / nothing) on 99.76-99.996 % of the passthrough-clear pixels, waterline chamfer median 0 px
both ways (`geometry.live` in the results).

| Camera | Cross-sections | Median | 95th pct | Within 1 px | Within 3 px | Signed mean | Scale | Median <= 1 | 95 % <= 3 px |
|---|---|---|---|---|---|---|---|---|---|
| Huge v1 | 372 | 0.69 px | 2.31 px | 69.9 % | **97.8 %** | -0.07 | 0.900 | pass | **pass** |
| Huge v2 | 380 | 0.91 px | 3.74 px | 56.3 % | 89.7 % | +0.46 | 0.8975 | pass | fail |
| Huge v3 | 423 | 0.81 px | 3.29 px | 58.2 % | 94.1 % | +0.42 | 0.8975 | pass | fail |
| Tiny v1 | 409 | 0.57 px | 1.98 px | 70.4 % | **98.8 %** | -0.31 | 0.9025 | pass | **pass** |
| Tiny v2 | 364 | 0.97 px | 5.11 px | 51.4 % | 89.8 % | +0.18 | 0.9025 | pass | fail |
| Tiny v3 | 329 | 0.84 px | 8.22 px | 58.1 % | 90.0 % | +0.79 | 0.900 | pass | fail |

*Checked 2026-11-13 by verification 9038 (`terrain-surface-verification.md` 8): the table reproduces exactly with this
tool; an independent metric (own code, outline scale fixed at 0.9, 36 bins per hex) on the same shots gives median
0.66-1.09 px, signed mean within 0.4 px (0.05 world units: no height or camera bias), within 3 px 91.5 / 96.5 / 97.7 %
(huge v2 / v3 / v1) and 94.0 / 93.6 / 99.3 % (Tiny v2 / v3 / v1): the 95 % share then fails at three cameras, not four;
the tail's pass or fail depends on the binning.*
The median holds at all six cameras; the 95 % share at the two default-zoom views and not at the four zoomed-in ones
(v2 = four wheel ticks in; v3 at the same zoom). Section 3's offline value at the huge validate camera (= v3) was
median 0.82 / 87.5 % within 3 px; live v3 gives 0.81 / 94.1 %. Why the zoomed views miss [I, not separated]: the band is
wider in pixels there, so the method's floor (corners, river stretches, model cut-offs, section 3) scales with zoom, and
the EXE's LOD choice (LodMip >= 1, P17) would smooth its surface where ours uses mip 0. *2026-11-14 (issue 0154, P17):
the zoomed-in views are the ones where the EXE draws L = 0 like the server (L = 0 at z 261, 3 ticks in [L]; closer zooms
have a lower LOD [C]); L = 1 applies at the default zoom (v1). So the LOD does not explain the zoomed views' misses.* Without the corner drop: huge
v1 97.5 %, v2 88.3 %, v3 91.5 %; Tiny 98.9 / 89.6 / 89.9 %. Load view: no highlight taken (no plot), not measured.

**Colour (definition of done 3, decision 1a, MC 2026-11-11 13:36): fails at every camera [L].** On the brief's mask
(passthrough clear minus 16 px around models, minus water, unrevealed, route, farm and forest plots from the radius-20
census, minus the server's water and a 16-px frame), restricted to cached tiles (magenta out), against the stock shot
of the stock launch (load view: W3's stock shot, same camera):

| Camera | Mask px | Cached px | Cached share | MAD R / G / B | Median | p95 | Max | Within 16 | Pass |
|---|---|---|---|---|---|---|---|---|---|
| Huge load view | 261,343 | 35,274 | 13.5 % | 15.4 / 14.2 / 23.0 | 21 | 85 | 194 | 32.8 % | no |
| Huge v1 | 602,464 | 574,635 | 95.4 % | 12.0 / 10.3 / 10.4 | 6 | 64 | 180 | 80.5 % | no |
| Huge v2 | 533,781 | 514,824 | 96.4 % | 17.6 / 15.4 / 16.7 | 10 | 76 | 204 | 62.2 % | no |
| Huge v3 | 253,496 | 216,715 | 85.5 % | 24.3 / 22.4 / 25.4 | 26 | 76 | 169 | 36.8 % | no |
| Tiny v1 | 735,727 | 735,727 | 100 % | 21.8 / 23.7 / 31.3 | 10 | 148 | 215 | 59.2 % | no |
| Tiny v2 | 860,719 | 860,636 | 100 % | 16.3 / 16.0 / 17.3 | 8 | 116 | 236 | 70.3 % | no |
| Tiny v3 | 427,464 | 427,464 | 100 % | 26.4 / 27.4 / 36.0 | 32 | 123 | 233 | 25.1 % | no |

(Median, p95 and max are of the largest channel difference; pass = MAD <= 4 per channel and 95 % within 16. Against the
ground truth's shots instead of the stock launch's: within 1.3 per channel of these, `compare_summary_gt.json`.) The load
view, the only camera identical across launches, has only 13.5 % of its mask on cached tiles (W3: 30 of 97 tiles have
a cache; the server drew 102 tiles there, 24 with a cache), so under the MC's rule it is judged on the cached mask too.

What the numbers are made of [L for the pictures, I for the attribution]:
- **The colour path itself is right where nothing else is drawn.** At huge v1 the signed mean difference (live - stock)
  on the colour mask is +0.6 / +0.6 / +0.4, i.e. no brightness or gamma offset (so the BC1_UNORM view of the cache, not
  an sRGB one, matches the EXE [I]); the median pixel differs by 6 (huge v1) to 10 (Tiny v1). The structure of the
  baked texture (fields, river beds, hills) lines up visibly (`w5_huge_<view>_pt_terrain_512.jpg`; a Tiny v1 crop at
  full size showed the same grass, hill and river-bed texture in both).
- **The tail comes from what the stock EXE draws over the terrain inside the mask, which D33 and the server do not
  draw:** river water (River system, seam C; the mask has no river class), culture borders and the selected unit's hex
  overlay (Overlay, seam E), unit and building shadows (PS Arg6 = 0 in our draw; huge v3 in the city has a +14 / +12 /
  +11 brighter live picture), trees (Forest is off in D33; forest plots are removed but their crowns reach over
  neighbouring plots). Fog of war is not the cause in these views: the census marks every revealed plot of the Tiny
  views visible, and removing fogged plots (`--exclude-fogged`) did not change the huge masks.
- **Misregistration across launches:** at huge v1 a 1-px shift of the live frame lowers the MAD from 10.9 to 8.65
  (best of +-6 px); the cameras differ by up to 6e-2 between the launches.
- **Contrast:** the per-channel linear fit stock = a * live + b at huge v1 has a = 0.69-0.71: the stock picture has
  less texture contrast than ours, as if the EXE sampled a blurrier mip or with other filtering [I, not traced].
So under the brief's definition the colour does not meet MAD <= 4 / 95 % within 16 against stock frames, and it cannot
while the stock frame contains seams C, E and the shadows inside the mask; a colour judgement of the tap itself needs a
reference without those layers (below).

**A cleaner reference is not available in D33 [L, negative results, 2026-11-11].** (1) The D33 system set without the
`viewmask.enabled` marker (Terrain view bit on: the EXE draws its own terrain under D33) **crashes the EXE at load**:
c5h-d 16:39:48, access violation writing 0x0000000C at file offset 0x4CC892 (VA 0x8CD492), return addresses near RVA
0x2570B0 (the river test 0x2570C5 of the view-mask DLL's patterns) and 0x3495A1 (the Arrows render test 0x3495D6):
River and Arrows are off in the configuration while their terrain-side consumers run [I]; minidump and crashes.log in
`D:/c5-64bit-work/3200/w5/crash_d33exe/`. (2) The same with River and Arrows kept on (c5h-e, 16:47-16:50) loads, but the
EXE's own terrain draws **black** at every camera (the terrain PS lerps to its FOW colour by the FOW mask, whose handle
is 0 without FluidFOW: the mechanism of disable-systems-and-masks.md 5 "Why the models are dark red") [L picture, I
mechanism]: MAD 106-174 against it, useless as a reference. A reference for the tap's colour alone would be the EXE's
terrain with the white FOW mask of one-off 3011 (`whitemask=2`, another test DLL), or a stock frame with River, Overlay
and shadows off and the FOW mask white: not done here.

### 5.1 Geometry tail by kind (issue 3206, W7b, 2026-11-12 20:38-21:30 EST; offline analysis of W5's live shots)

Question (MC 2026-11-11 17:12 point 3): of the cross-sections over 3 px at the four failing cameras of the table above,
which kind of ground and which screen region, and what is the cause; in particular whether it is the water outline
height of bug 3012. Data: W5's shots and world images of 2026-11-11 (huge save t350 copy, Tiny save; DLL i2-shell-test
b92373aeb; stock launch with the EXE's hex highlight on five plots per view, D33 passthrough at the same view, census of
the plots within radius 20), nothing new taken live. Tool: `shell/tools/geom_tail.py` (drafted by W7 on 2026-11-11, taken
over and extended by W7b): it reproduces `terrain_compare.geometry_highlight` exactly (same sections, same fitted scale:
372 / 380 / 423 / 409 / 364 / 329 sections, within 3 px 97.8 / 89.7 / 94.1 / 98.8 / 89.8 / 90.0 %, as in section 5) and
labels every kept section. Command (about 50 s per map, one core):
`python shell/tools/geom_tail.py --gt-dir D:/c5-64bit-work/i2-ground-truth/huge --w5-run D:/c5-64bit-work/3200/w5/huge2/views
--w5-stock D:/c5-64bit-work/3200/w5/huge_stock --w5-census D:/c5-64bit-work/3200/w5/huge_census --out <full.json>
--summary-out shell/evidence/3200/w7_geom_tail_huge.json --debug-dir <dir>` (Tiny: `tiny`, `tiny1/views`, `tiny_stock`,
`tiny_census`). Full outputs with every section: `D:/c5-64bit-work/3200/w7b/{huge,tiny}.json`, `.txt`, `dbg/*.png`;
evidence `shell/evidence/3200/w7_geom_tail_{huge,tiny}.json` and `w7_huge_v2_city_zoomin_sections_512.jpg`,
`w7_tiny_v3_second_zoomin_sections_512.jpg` (band coloured per section: green < 1 px, yellow < 3, red >= 3, blue dropped).

Labels: **kind** of the section's edge of the highlighted plot: water (the plot is water), coast edge (land, the plot
across this edge is water), coast (land, another neighbour is water), mountain, hills, flat (first match). **Region**:
edge = section centre in the outer 10 % of the frame's width or height. Also: contaminated (more than 10 % of the band
pixels within 4 px of another change between the highlight shot and the plain shot: the plot tooltip, an animated unit),
beside model (more than 10 % within 3 px of a non-clear pixel of the D33 passthrough at that view, i.e. a city building,
unit, banner or HUD; the passthrough camera differs from the stock camera by 0.0-0.8 px at the outline), river bed (land
section whose drawn surface lies below z 11.5; land here is 12.0, the carved river beds 11.14-11.31 [I: no river field
in the census]), completed (the error recomputed with the band pixels over the drawn water added back, each ray cut with
z = 10.0). There are no mountain sections at any of the six cameras.

**By kind and region, acceptance set (count over 3 px / sections; median / p95 px) [measured on the live shots]:**

| Camera | Water | Coast edge | Coast | Hills | Flat | Edge region | Centre | Water excluded | Water + coast excluded |
|---|---|---|---|---|---|---|---|---|---|
| Huge v2 | - | - | - | 17/148 (0.97/3.72) | 22/232 (0.81/3.75) | 1/8 | 38/372 | 89.7 % (380) | 89.7 % (380) |
| Huge v3 | - | - | - | 1/13 (0.37/4.05) | 24/410 (0.82/3.22) | 4/21 | 21/402 | 94.1 % (423) | 94.1 % (423) |
| Tiny v2 | - | 14/99 (1.14/4.27) | 21/189 (0.89/31.4) | - | 2/76 (0.93/2.40) | 5/20 | 32/344 | 89.8 % (364) | 97.4 % (76) |
| Tiny v3 | 13/53 (1.13/7.47) | 3/34 (0.68/5.47) | 16/165 (0.97/18.7) | 0/18 (0.20/0.87) | 1/59 (0.61/1.36) | 5/10 | 28/319 | 92.8 % (276) | 98.7 % (77) |
| control Huge v1 | - | - | - | 4/144 | 4/228 | 0/0 | 8/372 | 97.8 % | 97.8 % |
| control Tiny v1 | - | 3/97 | 2/216 | - | 0/96 | 0/0 | 5/409 | 98.8 % | 100 % (96) |

**So the tail is not water: with water excluded none of the four cameras passes** (the huge views have no water or
coast section at all); with water and coast excluded the Tiny views pass and the huge views do not.

**By cause** (each section over 3 px gets the first matching cause, in this order) and the share within 3 px after
removing the first four causes (all their sections, also those under 3 px):

| Camera | Over 3 px | Contaminated (tooltip, animation) | Beside model | Band cut by waterline | River bed | Screen edge | Shared edge | Other shore | Unexplained | Within 3 px without the first four |
|---|---|---|---|---|---|---|---|---|---|---|
| Huge v2 | 39 | 4 | 24 | 0 | 10 | 1 | 0 | 0 | 0 | 99.4 % (167) |
| Huge v3 | 25 | 1 | 18 | 0 | 0 | 1 | 1 | 0 | 4 | 98.2 % (341) |
| Tiny v2 | 37 | 22 | 2 | 4 | 0 | 4 | 0 | 4 | 1 | 95.0 % (181) |
| Tiny v3 | 33 | 16 | 8 | 7 | 0 | 0 | 1 | 1 | 0 | 98.3 % (117) |
| Huge v1 | 8 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 100 % (173) |
| Tiny v1 | 5 | 2 | 0 | 2 | 0 | 0 | 0 | 1 | 0 | 99.5 % (188) |

**Cause: the tail is the reference band, not the drawn surface [I, strong; per item below].** Every one of the four
failing cameras reaches 95.0-99.4 % within 3 px once the sections whose EXE band is disturbed by something other than
terrain are removed, and in world units the error is the same at every zoom (p95 0.42-1.04 world units at all six
cameras; screen scale 4.2-4.3 px per unit at v1 against 6.9-7.3 at v2/v3), which is why only the zoomed views fail.
- **Models over the band** (the largest share on huge: 24 of 39, 18 of 25). Where a city building, unit or banner
  covers part of the band, the visible part's centre is not the outline. Huge v3 sections beside a model: 53.7 % within
  3 px (median 2.87 px), the rest 98.2 %. Method floor (section 3 named it from the pictures; now counted).
- **The plot tooltip in the highlight shot** (the largest share on Tiny: 22 of 37, 16 of 33, and every section over
  10 px: 13 at Tiny v2, 13 of 14 at Tiny v3, up to 97 px). The stock highlight shot shows the plot help popup over the
  city (its text has magenta words, which pass the band's colour test) and units that moved between the two shots
  (`w7_tiny_v3_second_zoomin_sections_512.jpg`). Method: the reference shot is not clean.
- **The band cut by the waterline** (Tiny 13 sections over 3 px at v1-v3). The measurement keeps only band pixels over
  drawn terrain; where the waterline runs along an edge, only the land half of the band survives and its centre moves
  inland (signs: negative on the land plot's coast edges, positive on the water plot's edges, as predicted). Test: with
  the band over water added back at z 10.0, all 13 drop to 0.1-2.9 px (e.g. Tiny v3 (7,23) +10.25 -> +0.17 px,
  (8,24) -7.65 -> -0.75 px). Method.
- **River beds** (huge v2: 10 of 39; huge v1 has the same sections under 3 px). On the carved bed (drawn z 11.14-11.31)
  the EXE's band sits at about the bank height: implied band height (z + error / screen px per unit of height) median
  11.85 (v1) and 11.79 (v2) against 11.83 and 11.96 on the flat land beside it (z 12.02), i.e. 0.6-0.9 world units
  above our bed (0.6-0.7 as measured, about 0.85 after the -0.2 offset seen on the flat land), flat across the channel (not a smoothed bed: LOD smoothing of a
  channel wider than a section would not lift its middle) [I]. The stock frame draws river water (River system, seam C)
  there; the band lies on it. Not the terrain draw; the river sections over 3 px measure 3.1-4.8 px at huge v2 (7.3 px per unit); at v1 (4.3 px per unit) the
  same sections stay under 3 px but one.
- **Screen edge and projection: not a cause.** Edge-region sections over 3 px with no other cause: 1 (huge v2, 3.43 px),
  1 (huge v3, 3.77 px), 4 (Tiny v2, (7,22) edge 2 at x 172-223, y 963-1078: the stock unit panel at bottom left covers
  part of the band, fill 0.59-0.68 [I]). Tiny v3's edge sections over 3 px are all tooltip.
- **Hills: the EXE's highlight follows the surface.** Hills sections free of the causes above: huge v1 66 of 66 within
  3 px, huge v3 9 of 9, Tiny v3 9 of 9; huge v2's 17 hills sections over 3 px are models (9), river beds (6) and tooltip (2). Mountains: none in view.
- **Left over:** huge v3 4 flat sections at 3.01-3.65 px and 1 shared edge; Tiny v2 1 flat at 3.43 px and 4 coastal
  slope sections (z 11.6-15.8) at 3.0-5.0 px. Not explained; small.
- **Not the terrain heights [C from section 3]:** the draw is the EXE's surface to 0.0011 units; the median holds
  (0.57-0.97 px) at every camera.

**Water outline height (bug 3012) measured from these shots [L shots, offline fit].** The band over the drawn open water
(excluded from the acceptance set) lines up best when its rays are cut at z 10.0: median section error 0.63 px (Tiny
v1, 40 sections), 1.55 px (Tiny v2; best 9.25 at 1.40), 1.04 px (Tiny v3, 99 sections); at z 12.0 it is 4.51 / 8.06 /
8.60 px. So the EXE draws its outline over water at about z 10.0 (field water plane 10.117), not 12: this is why the
server's grid with `--water-z 12` is -5 px off over open ocean (bug 3012, 9031). One-off 9035 should clamp to about
10.0-10.1, not 12. *Done 2026-11-13 (9035): `--water-z` default 10; live over open ocean: best shift (0, 0), 100 % within 1 px
(`camera-and-picking.md` section 10 item 6).* Bug 3012 does not enter the terrain acceptance numbers (no band pixel over water is in them, and the
terrain draw has no clamp).

**Consequence for the acceptance:** *(corrected 2026-11-13 by issue 3020, below: the clean re-measurement meets 95 %
within 3 px at all six cameras. Replaced: "as measured, 95 % within 3 px fails at four cameras ... stays an open item for
the lead and the MC".)* W5's shots fail at four cameras, and it is not water: the tail is the stock reference (models
and the plot tooltip in the highlight shot, the waterline cutting the band, river water over the carved beds).

**Clean live re-measurement (issue 3020, 2026-11-13 09:16-10:35 EST) [L shots, offline measurement]: the criterion is
met at all six cameras; the drawn surface has no tail of its own.** Set-up: stock configuration (`[DisableSystems]`
OverlayDebug and ColorKeyEdit only, no D33, no server) on the current pins: huge on c5h-d (DLL FF648358..., the
`i2-shell-test` build of bbfacca1), Tiny on c5h-c (`i0ship-bbfacca1`, E589306E...); save copies of W5's saves
(`C5H-3200-HUGE_0350`, turn 350; `C5H-3200-TINY`, turn 18) under `C5H-4000-3020-*` names, deleted afterwards; views from
the ground truth's plan.json (UI.LookAt + wheel ticks, as W5); 2554 x 1411. Changes against W5: (1) the cursor parked at
client (0.93, 0.93) (the minimap) before every shot: no plot tooltip in any shot; (2) instead of the city and four plots
around it, every revealed land plot whose centre is in the frame is highlighted once, in four sets per view with no two
plots of a set adjacent (no shared edge, no band beside another); per view one plain shot, four highlight shots, one
plain shot after; camera bytes equal in all six shots of every view; (3) the band completed over water (geom_tail's
`completed`, rays cut at z 10.0); (4) the drawn surface is terrainshot at each new stock camera (built from f7a5121; at
W5's Tiny v2 stock camera its world image equals W5's byte for byte), so no server run was needed (section 5: the live
composited frame is this draw). Model mask: W5's D33 passthrough shot of the same view, whose camera projects the
highlighted plots within 0.05-0.47 px of the new stock camera. Tools (scratch, not in the repository):
`D:/c5-64bit-work/4000/3020/make_sets.py`, `live_3020.py` (a changed copy of `terrain_live.py --stock`),
`measure_3020.py` (`geom_tail.measure` per set, rows pooled); raw data `D:/c5-64bit-work/4000/3020/{huge,tiny}_stock/`
(`measure.json`, `<view>_rows.json`). Difference images (band coloured by each section's completed error: green < 1 px,
yellow < 3, red >= 3, blue dropped): `evidence/4000-3020/<map>_<view>_sections_512.png`.

| Camera | All land sections (completed): n / within 3 px / median / p95 / max | Clean: n / within 3 px / median / p95 / max | W5 (section 5) within 3 px |
|---|---|---|---|
| Huge v1 | 4,175 / **99.3 %** / 0.61 / 1.59 / 6.98 px | 2,369 / **100.0 %** / 0.58 / 1.31 / 3.79 px | 97.8 % |
| Huge v2 | 1,685 / **95.7 %** / 0.95 / 2.74 / 10.36 px | 1,158 / **98.8 %** / 0.90 / 1.92 / 4.96 px | 89.7 % |
| Huge v3 | 1,506 / 94.5 % / 1.03 / 3.24 / 11.68 px | 926 / **99.8 %** / 0.98 / 1.78 / 5.00 px | 94.1 % |
| Tiny v1 | 1,371 / **99.3 %** / 0.63 / 2.00 / 5.58 px | 1,015 / **99.9 %** / 0.57 / 1.69 / 3.14 px | 98.8 % |
| Tiny v2 | 1,135 / **97.1 %** / 0.81 / 2.60 / 5.04 px | 846 / **98.6 %** / 0.78 / 2.24 / 5.04 px | 89.8 % |
| Tiny v3 | 630 / **96.0 %** / 0.76 / 2.66 / 5.56 px | 464 / **98.9 %** / 0.66 / 1.89 / 3.42 px | 90.0 % (92.8 % water excluded) |

"All land" is every kept section of the highlighted land plots (no water plot is highlighted, so water is excluded; the
uncompleted error differs by at most 0.4 points: Tiny v1 99.1 %, v2 96.7 %, v3 95.7 %, huge unchanged). "Clean" removes
the sections whose stock band is disturbed by something that is not terrain: beside a model (D33 passthrough within
3 px: buildings, units, banners, the HUD), on or across a forest, jungle or marsh plot (D33 has Forest off, so the model
mask misses trees), a carved river bed (land below z 11.5, the river water lies on it), or across an unrevealed plot
(clouds). The geom_tail `contaminated` flag (other changes between the plain and the
highlight shot) is **not** used: in these shots it marks mostly the animated cloud shadows and waves (15.0 % of the
pixels of Tiny v1 change between the plain shots before and after its four highlight shots), and the sections it flags are as accurate as
the rest (Tiny v1: 98.8 % within 3 px); with it removed as well the shares are 97.5-100 %.
- **The only camera under 95 % on all land sections, huge v3 (94.5 %), is models:** of its 83 sections over 3 px, 69
  are beside a model (the city's buildings, `huge_v3_second_zoomin_sections_512.png`), 9 on a river bed, 3 at trees, 2
  others.
- **What stays over 3 px in the clean set is 0.1-1.4 % per camera, scattered, 3.0-5.0 px:** huge v2 14 sections (12 on
  river plots, among them 9 coast sections of (102,31) on the river bed at z 11.09-11.23, which the land-only river-bed
  rule does not catch; 2 flat of (99,31) at 3.3-4.1 px), huge v3 2, huge v1 1 (3.79 px), Tiny v2 12 (6 on river plots, 3 on the coastal slope
  of (7,22) at z 15.2-15.7, 3 coast of (8,22)), Tiny v3 5 (4 coast at 3.0-3.4 px), Tiny v1 1 (3.14 px). No kind,
  region or plot collects them: no defect of the drawn surface is indicated [I]; the river-plot share points again at
  the stock river water (seam C), not at the terrain.
- The median holds at every camera (0.57-1.03 px); p95 of the clean set 1.31-2.24 px.

## 6. Distance fog (issue 0188, 2026-11-14 19:10-19:40 EST, impl-B-0188, branch `task-0188-terrain-fog`)

The largest single cause of the terrain colour difference of `terrain-colour-reference.md` 2 (distance fog: camera 1
p95 89, signed B -16.9 over the land). The module now sets the EXE's DistanceFog block and binds the EXE's fog ramp.

**What the PS does with it** [C, D3DDisassemble of PS 0x067e8d68 (pass 1) read from the EXE at run time, scratch only;
pass 0's PS 0x067e6e58 binds the same cb2 and t2]. With d = the pixel's world position - CameraPos (TerrainFrame
cb1[19]), a = 1 - exp(d.z * FogParams.x), f = 1 - min(exp(|d|^2 * FogParams.y * a / d.z), 1), the fog amount is
f * t2.x sampled at (0.5 + 0.5 * v5.y, FogParams.w) with sampler state 5 (linear, clamp), and the colour is mixed toward
FogColor.rgb by it as the last step (after shadow, overlay and FOW). The ramp's u runs over the screen height (v5.y: the
haze grows toward the top of the picture), its v (one of 8 rows) is the zoom fraction. The ramp's red channel is 0 for
u below about 0.5 in every row, 36-101 at u = 96/127 and 189-215 at u = 127 (rows 0-7) [L, the capture's t2].

**The EXE's block at 3601's two stock captures** [L, `render/evidence/0188/fog_block.py` on exports of
`c5h-c_20261113-235332_frame{309938,265572}.rdc` (Tiny `C5H-3600-TINY_0009V`, 32-bit pin `i0ship-26234371-ia`, VT
present), 2026-11-14]: one distinct cb2 in all 150 (camera 1) and 58 (camera 2) scene terrain draws:

| Camera | CameraPos z | FogColor | FogParams |
|---|---|---|---|
| 1 (zoom 17) | 640.31537 | 0x3f129293, 0x3f36b6b7, 0x3f73f3f5, 1 (= 146, 182, 243 times 1/255, rounded as the float product) | -0.05, -1.24660312e-17 (0xa365f52b), 0, 0.631578922 (= 6 / 9.5) |
| 2 (zoom 11) | 261.00354 | the same | -0.05, -2.14971085e-09 (0xb113ba1d), 0, 0 |

- **FogParams.y = exp(-(density * CameraPos.z)) * (-1 / density) * 5e-5 with the product in extended precision** [L]:
  every step in double gives the EXE's bits at both cameras; the product rounded to float first (as `water.cpp` does
  for WaterFrame 0x144) is 16 ulp (camera 1) and 4 ulp (camera 2) off. The two writers differ: the same capture's
  WaterFrame 0x144 at camera 1 is 0xa365f51b, the float product's value [L, export `cam1_frame309938` draws 350 and
  356], so each module's formula is right for its own block.
- **FogColor = c * (1/255) in float** [L]: blue 243 * (1/255) = 0x3f73f3f5; 243 / 255 is 0x3f73f3f4, one ulp lower.
- **FogParams.w is the controller's TARGET zoom fraction, not the camera's** [L]: 0.631578922 is exactly (17 - 11) /
  9.5 while the camera at rest is at 16.9997 (fogZoomFraction 0.631544); at camera 2 the EXE writes 0 where the camera
  is at 11.017 (0.0018). The server has the camera only (the VPShellCamera block holds no zoom target), so the module
  computes w from the camera height as the water does (`water-system.md` 10.7). Effect: 0 at both cameras (below).
- **t2 = `fogramp.dds`** (`Resource/DX9/MiscTextures.fpk`, 128 x 8, 8 mips, A8R8G8B8 in the file) [L]: the capture's
  t2 (R8G8B8A8, 5,500 bytes) equals the file's pixels with R and B swapped, every byte.

**The module** (`render/src/terrain/terrain.cpp`, `terrain.h`): `DistanceFog` (enabled, colour, density 0.05, scale
5e-5, w < 0 = fogZoomFraction of the camera height), `distanceFogBlock(fog, camPos, out[8])`, `fogZoomFraction`,
`Options::fog` (default on), `FrameStats::fogOn`, `Renderer::fogRamp()`. `init(dev, exe, gameDir)` now reads
`fogramp.dds` through `c5r::gamefs` when gameDir is given (a missing or unreadable ramp is an init error); without
gameDir t2 is a black 1x1, the fog amount is exactly 0 and the picture is the one before 0188, bit-equal (test
`draw_without_a_ramp_is_bit_equal`). The block is written whether or not a ramp is bound. The server
(`server/src/render/srv_render_scene.cpp`) passes its install (`render_bake_install`, default the game folder); an
install that gives no ramp is one LOUD line and the terrain is drawn without distance fog (as the residency goes on
without an install, 3308); no install at all (a test or tool configuration) is a plain note; the init line ends
`distance fog 0|1`. `terrainshot` and the other tools still init without an install: no fog there.
**Cost** [L, workstation, indication only (COMMON 23), 2026-11-14 19:55, two runs of `c5r_terrain_test`]: the init
takes 67-75 ms without an install and 1,329-1,505 ms with it; nearly all of it is `gamefs::GameFiles::open` indexing
the install (an `fpkscan lookup` of the name alone: 1.2-1.6 s). *Corrected 2026-11-15 by issue 0198 (6.1): the water
module opens three indexes, not one, and 0198 made the terrain read a shared index; this paragraph replaced "the water
module opens its own `GameFiles` in its init as well, so the server's start pays the index twice. Proposed, not done:
one `GameFiles` opened by the scene and handed to both modules".*

**Checks** [L, 2026-11-14, RTX 4080]:
- `render/tests/terrain` (`c5r_terrain_test`, 29 tests): `fog_block_equals_the_exe_capture` (members 0-6 bit-equal to
  the captured bytes at both cameras with the defaults; w within 4e-5 / 2e-3; all 8 bit-equal with w given),
  `fog_block_off_and_edges`, `negative_fog_not_finite` (BadLayout), `draw_without_a_ramp_is_bit_equal`,
  `game_fog_ramp_is_the_exes_and_fogs_the_far_terrain` (the module's t2 red channel equals the capture's at 40
  texels; a top-down draw at height 300: the top eighth's blue 48.0 -> 59.6, the bottom half unchanged on every
  channel), `negative_init_install_without_a_fog_ramp`.
- **3019's replay with the module's block** (`render/evidence/0188/replay_0188.py`: the EXE's scene terrain draws as
  captured, with every draw's cb2 replaced by the 32 bytes `distanceFogBlock` gives for the capture's CameraPos, w from
  the camera height): **0 pixels differ** from the replay with the EXE's own block at both cameras (camera 1: 150 draws,
  1,419,353 land px; camera 2: 58 draws, 2,795,230 land px; the whole 2554 x 1411 target compared). The w difference
  does not reach the picture: at camera 2 any w below 1/16 samples row 0 only (clamp), at camera 1 the row weight moves
  by 3e-4.
- Against the stock picture of the same launch (`D:/c5-64bit-work/3600/captures/cam{1,2}_stock.png`), over 3019's land
  mask (drawn, world z above the water plane + 0.5, minus a 16-px frame; NOT the clean mask: water at the shore, rivers,
  trees, units and the interface are in it). Largest channel difference per pixel: median / p95 / max, share within 16,
  MAD R/G/B, signed R/G/B:

| Camera | Replay with the module's block (= the EXE's) vs stock | Replay with FogParams 0 (the server before 0188) vs stock |
|---|---|---|
| 1 (zoom 17) | 0 / 116 / 245; 60.6 %; 19.4/15.9/13.7; +11.0/+6.0/-4.0 | 21 / 125 / 245; 45.7 %; 20.6/18.6/26.4; +7.8/-0.7/-20.9 |
| 2 (zoom 11) | 0 / 102 / 255; 74.7 %; 14.5/13.7/10.8; -2.4/-3.1/-5.6 | 3 / 106 / 255; 65.4 %; 15.7/15.9/17.9; -3.8/-6.7/-15.1 |

  The fog term alone (module block vs FogParams 0, both replays): camera 1 median 0 / p95 89 / max 149, 71.9 % within
  16, signed B -16.9; camera 2 0 / 54 / 79, 80.7 %, B -9.5 (3019's `nofog` row, reproduced). Difference images and the
  replays: Visual Checks vc-20261114-192434-workstation-b376 (camera 1, module block, against stock),
  vc-20261114-192436-workstation-f100 (camera 2), vc-20261114-192435-workstation-efd7 / -4f12 (FogParams 0, against the
  EXE's replay); all DIAGNOSTIC (D112): terrain draws replayed, not the product.

**Not verified:** the live product (the server's own frame with the fog on: no live run in 0188; the server wiring is
covered by `server/tests/render`); other zoom levels and saves than 3601's two cameras; w at a camera still moving
toward its target (the EXE writes the target, the module the current height: the fog row leads the camera's easing by
the easing's lag).

### 6.1 One index of the install for the scene's init (issue 0198, 2026-11-14 23:57 - 2026-11-15, impl-B-0198, branch `task-0198-fog-ramp-index`)

**Who builds an index of the install at the server's render start** [C, code read at main 1d505f34 + this branch,
2026-11-15]. `gamefs::GameFiles::open` (mount the 45 base packs, every DLC package, scan `Assets/` loose) costs
1.2-1.8 s on the workstation's install (below). Before 0198, in `Scene::init` (`server/src/render/srv_render_scene.cpp`)
with an install and `render_tiles=bake` (the default):

| Opener | Where | Before 0188 | 0188 | 0198 |
|---|---|---|---|---|
| terrain renderer (fog ramp, spec map, TerrainShadow ColorKey) | `terrain.cpp` `Renderer::initFrom` | none (no install read) | own | the scene's |
| scene | `Scene::init` | - | - | one, shared |
| water renderer (ocean art) | `water.cpp` `Renderer::init` | own | own | own |
| rivers | `river.cpp` `loadRiverArt` | own | own | own |
| coast waves | `srv_water_scene.cpp` `C5rWaterModule::init` | own | own | the scene's |
| residency (bake) | `srv_residency_source.cpp` `ResidencySource::init` | own | own | own |
| **index builds** | | **4** | **5** | **4** |

With `render_tiles=tap` or `none` there is no residency (3 / 4 / 3). With `render_water=0` or placeholder the water
module is not initialised: then 0198 has the scene's index alone (1 / 2 / 1 with the residency's). If the scene's open
fails, the terrain is initialised without an install (LOUD, as 0188) and the waves open their own (which fails as
before).

**The change** (functions changed, for 0168's merge):
- `c5r::terrain::Renderer::init(dev, exe, const gamefs::GameFiles&)` (new, `terrain.h`): the install's three files
  read from the caller's index, which the renderer does not keep. `init(dev, exe, gameDir)` is unchanged in behaviour;
  both now call the private `Renderer::initFrom(dev, exe, files, openDir)`, the former body, where the index is the
  caller's or (gameDir given) one opened at the same point as before, so errors keep their order (device, EXE, shader
  blobs, install).
- `srvrender::Scene::init`: opens one `GameFiles` of `o.install`, passes it to the terrain and through
  `WaterSceneOptions::files` to the water scene; the index lives until the end of `Scene::init`, as the modules' own did.
  The init line now says `the install's index <ms> of it, shared with the waves 0|1`.
- `srvrender::WaterScene::init` hands `o.files` to the module with the new `WaterModule::shareFiles` (default: ignore)
  and keeps no copy (`m_opt.files` null); `C5rWaterModule` reads `Wave_Coastline_ALL.dds` from it, else opens its own.
- Not done (other owners' files): the water renderer and rivers (`render/src/water/`) and the residency
  (`srv_residency_source.cpp`, epic 0301) still open their own; handing them the scene's index too would take the start
  from 4 builds to 1.

**Checks** [L, 2026-11-15, RTX 4080, install `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`, EXE
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`]:
- `c5r_terrain_test` 48 run, 0 failed, 0 skipped; new: `shared_index_init_equals_the_install_folder_init` (the shared
  index gives the same t2 red channel at all 1,024 texels, the same t9, FNV-1a 0xfff35e0f81f61405 over 1,398,128 bytes,
  and the same t7 source as the folder's init), `shared_index_is_read_not_reopened` (a fake install indexed, then its
  `Resource/` removed: the index's init succeeds, the folder's init fails with gamefs Io, so the index init builds no
  index of its own; the renderer works after the index is gone), `negative_shared_index_without_a_ramp_or_with_a_bad_spec_map`
  (NotFound naming `fogramp.dds`; Truncated naming `specscreenmap.dds`; nothing half-made; null device BadLayout; a missing
  EXE Io before any install read). ASan (`render/build.py --asan --test -R terrain`): passed, 0 ASan reports, peak
  commit 1,386 MB.
- `server/tests/render/water/test_water_scene.py` 19 passed; new cases `shared_index_handed` (the index reaches the
  module once, right before init; the next init without one hands null; mode off hands nothing) and
  `real_module_shared_index` (the real module takes its waves from a shared index, and from its own without one; an index
  of an empty install leaves the waves unavailable with the reason naming `Wave_Coastline_ALL.dds` while the ocean
  initialises). Under ASan (`build_water_tests.py --asan`, every case): 17 PASS, no report.
- `server/tests/render/test_render_host.py` (the render units of server.exe, `Scene::init` included): 47 passed.

**Start time, both ways** [L, workstation, an INDICATION (COMMON 23: no testbed, no window), 2026-11-15
00:16:10-00:29:48]. `render/tools/initbench` times the install-reading part of `Scene::init` with the real modules
(terrain init, then the water module's: ocean, rivers, waves; no residency, so 3 / 4 / 3 index builds) in one arm per
process; `render/evidence/0198/initbench_series.py`, 10 rounds, arm order rotated per round, `quietlock.py check` before
every arm (all 30 runs disturbed by other owners' games and suites, 20 s wait each; lock not held). Install and EXE as
above. Raw runs: `render/evidence/0198/results_0198.json`; summary `acceptance_0198.txt`.

| Arm | Mean ms | sd | Median | Min - max | Terrain init mean | Water init mean |
|---|---|---|---|---|---|---|
| pre0188 | 5,287 | 1,272 | 5,103 | 4,125 - 8,583 | 57 | 5,215 |
| 0188 | 6,330 | 1,054 | 6,319 | 4,862 - 7,621 | 1,546 | 4,764 |
| 0198 | 4,680 | 792 | 4,758 | 3,479 - 5,769 | 66 (+ index 1,564) | 3,050 |

Paired by round (indication only): 0188 - pre0188 +1,044 ms (95 % CI +230..+1,857), 0198 - pre0188 -607 ms
(-1,324..+111; the smallest difference this series could detect is about 1.3 s), 0198 - 0188 -1,650 ms (-2,011..-1,289).
One index build: 1,564 ms (sd 267, 1,199-2,110) on this busy machine; the terrain's reads of its three files from the
shared index: 66 ms against 57 ms without an install. **0198's result is the index-build count (4 / 5 / 4 above), not
a speed claim** (MC on the ticket, 2026-11-15, queue entry #1290: a start-time gain would need a quiet testbed series,
D40, D106; none is ordered). Not verified: a live server start's init line with an install; a quiet testbed series.

## 7. The EXE's terrain LOD, decoded, and the module at it (issue 0191, 2026-11-14 19:55-, impl-B-0191, branch `task-0191-server-terrain-exe-lod`)

Static reading of the objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm` (0x4241E0, 0x6497C0, 0x744AE0, the tile set-up
at 0x5C4ACF, 0x5C37E0, 0x7BA820, 0x4AD810, 0x8E5030, 0x8CEEE0, 0x678C4B), constants read from the `.rdata` of the EXE copy
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`; no decompiler output kept (D83). This completes P17's
"log2-like" description; P17's measured values stand.

**The tile box** [C, 0x5C4ACF..0x5C4CB9, at tile set-up]: tile+0x10C..+0x114 = min = (originX * TR8+0x1AC4 - TR8+0x1AB0,
originY * TR8+0x1AC8 - TR8+0x1AB4, -TR8+0x1AB8), i.e. (originX * sx - ox, originY * sy - oy, -oz); tile+0x118..+0x120 =
max = min + (d4 * sx, d8 * sy, TR8+0x1ACC * 255), d4 / d8 = tile+0xD4 / +0xD8 (65; 64 in the last column / row). **The
box's z range is the whole height range (0 .. 51 at sz 0.2), not the tile's heights.** tile+0x124..+0x12C = 0.5 * (min +
max) (centre), tile+0x130 = 0.5 * |max - min| (radius). [I] that oz (TR8+0x1AB8) is 0 and TR8+0x1ACC is sz.

**The projected size 0x744AE0(tile, ViewProj)** [C]: the box's four space diagonals ((lo,lo,lo)-(hi,hi,hi),
(lo,lo,hi)-(hi,hi,lo), (lo,hi,hi)-(hi,lo,lo), (lo,hi,lo)-(hi,lo,hi)), both ends through ViewProj (D3DXVec4Transform) and
multiplied by the float 1 / w, d_k = 0.5 * |xy_a - xy_b| (x87 sqrt, stored as float), **p = 0.5 * [0x301056C] * max_k
d_k** (the back-buffer width as an unsigned int; the product unrounded), i.e. 0.25 * width * the largest NDC length of a
diagonal. 0.5 = [0x9FCF90], 1.0 = [0x9F72F4].

**The tile LOD 0x6497C0** [C]: q = p * 0.5 / BicubicTerrainTessTriSize [0xD90F18] / BicubicTerrainTessSubdiv [0xD90F14] (4
and 2 at the owner's settings); q = 1 when q < 1; LOD = 0.5 + ln((max(d4, d8) - 1) / q) * 1.4426950408889634 ([0xA08870]);
4.0 ([0x9F7B40]) when LOD >= 4; -2.0 ([0x9ED65C]) when the float LOD is not above -2. With n = 64: **LOD = clamp(0.5 +
log2(1024 / p), -2, 4)**.

**The list it runs over** [C 0x7BA820 / 0x4AD810; L below]: TR+0x1AF0 (count TR+0x2334) is view 0's list (the wrap views
keep their own, TR+0x1AF4.. with counts TR+0x2338..). A tile enters it when its height class tile+0x328 is neither 1 nor 2
(class 3: the highest u16 of the window [origin + 4, origin + 67) / 255 is not below 30.0, terrain-bake-residency.md 1),
tile+0x1C0 != 0 [I: not separated from class 3; every class-3 tile passes in the checks below], and its bounding sphere is
not wholly outside one of the six planes at 0xDA1510 + 0x60 * view (dot(plane.xyz, centre) + plane.w < -radius culls).
The planes (0x8E5030) come from the camera's own perspective (D3DXMatrixPerspectiveFovRH of FCamera's fov, aspect, near,
far) through 0x8CEEE0: the clip matrix's columns c0..c3 give far c3 - c2, near c2, c3 + c0, c3 - c0, c3 + c1, c3 - c1, each
normalised by its xyz length, then moved to world space (D3DXPlaneTransform by the transposed inverse of the camera
matrix). The module builds the same six planes from ViewProj directly [I: equal while g_Proj is the camera's
perspective]. The radius of a 65 x 65 tile is 49.99 world units, so the list reaches about one tile beyond the screen.

**The frame update in 0x4241E0** [C, 0x42431E..0x4244EA], when BicubicTerrainTessellation != 0, the view count > 0 and the
list is not empty: M = min over the list of 0x6497C0 (start 10.0, [0xA0888C]), float; S = TR+0x242C, H = TR+0x2430, d = M -
S (float). **M is accepted when |d| > |H|, or when H * d >= 0 and d != 0**; else nothing changes (after a level change up
H = +0.25: any rise is taken at once, a fall only beyond 0.25; mirrored after a change down). On acceptance: M >= 0:
TR+0x2424 = M, TR+0x2428 = Subdiv; M < 0: TR+0x2424 = 0, TR+0x2428 = (1 << int(M * -0.75)) * Subdiv (cvttss2si;
[0x9F2544] = -0.75); then TR+0x2424 = floor(TR+0x2424) (msvcrt floor, import slot 0x9D6884); when floor(M) != floor(S),
TR+0x2430 = (floor(M) - floor(S)) * 0.25 ([0x9F3534]); TR+0x242C = M. All four are 0 (TR+0x2428 = Subdiv) at terrain
creation (0x678C4B). Per tile 0x5C37E0 takes L = int(floor(TR+0x2424)) (shader level >= 10) and rebuilds the tile's patch
grid when tile+0x1C4 != L (0x768160); the vertex count halves L times by n = (n >> 1) + 1 from d4 and LodStep = (d4 - 1) /
(n - 1) (0x5C3961..0x5C39C3): 2^L for d4 = 65, 63 / 32 at L 1 for the last column's d4 = 64. ViewProj = g_View 0xDA1310
times g_Proj 0xDA12D0 (D3DXMatrixMultiply at 0x424349).

**Checks of the decoded rule [L, 2026-11-14 20:10-20:28 EST].** (a) The five stock captures of 0154 (main-view ViewProj,
ScreenSize and drawn tiles extracted by `render/evidence/0191/capture_lod_inputs_0191.py`; numbers in
`render/evidence/0191/results/capture_lod_inputs.json`): the rule gives LodMip / LodStep / TessFactor 0/1/2 (camera z 261.0,
M 0.712), 1/2/2 (z 424.26: M 1.565, 1.551, 1.595) and 2/4/2 (z 640.3: M 2.196), all five as captured. For the three
captures of 3601's Tiny saves (C5H-3600-TINY_0008 / _0009V; heights from 3609's stock tap
`D:/c5-64bit-work/3600/3609/stock/heights`) **the class-3, sphere-visible list equals the tiles the EXE drew, tile for
tile: 58 / 150 / 80**; the sphere test without the class filter gives 58 / 233 / 108 (every drawn tile among them).
(b) **Live on oldpc** (testbed job `20261114-203537-79a7`, 2026-11-14 20:39-20:40 EST: tb1, its own Steam EXE and DLL,
CP/VP/EUI, huge TB-HUGE_0350 turn 350, 1914 x 1051 back buffer, stock; `render/evidence/0191/drive_live_0191.py --arm
stock`, cdb -pv: TR+0x2424..+0x2430, the list, g_View, g_Proj; numbers in
`render/evidence/0191/results/live_stock_oldpc.json`). The oldpc projection is not the workstation's (g_Proj 2.1325252 /
3.8835900 against 2.1351130 / 3.8646908: the y scale differs too). With the cameras as read, **the rule's list equals the
EXE's tile for tile at all three cameras (84 / 121 / 113)**, and at the load view **the frame minimum 1.98247969 equals the
held TR+0x242C 1.98247933** (float rounding). At Thebes and Akhetaten the EXE holds 2.01593 / 2.02268 (LodMip 2, H +0.25)
while the frame's minimum is 1.99572 / 1.97560: the fall during the LookAt's pan stayed below 0.25 and was refused, as the
hysteresis says. (An earlier estimate from 0134's workstation cameras with only the x scale changed gave 122 tiles and
minima 0.01-0.04 off: the projection was the cause.) (c) Tests `game_lod_reproduces_the_stock_captures`,
`game_lod_at_the_oldpc_huge_reads` (c5r_terrain_test; the latter compares the lists with the live file). Scratch: a
Python twin of the rule, `C:/c5-64bit-work/0191/scripts/lodcheck.py` (not kept; its output for (b) is
`render/evidence/0191/results/live_stock_oldpc_rule_check.txt`).

**The module (c5r::terrain, `terrain.h`, Options::lodLevel = kLodExe by default) [C, run as marked]:**
- Height textures are the EXE's pages: `setHeightField` builds the 1024 x 1024 A8_UNORM pages its tiles use, 11 mips, with
  `c5r::bake::buildHeightPage` (terrain-bake-passes.md 5.0: byte-equal to captured pages) and binds them with TileOffset.zw
  = 64 - px * 896 (tile+0xE4 / +0xE8) and HeightTexelScale (1/1024, -1/1024): the VS samples the EXE's texels at every mip.
  1- and 2-plot tiles always fit their page at L <= 4 (64 and 128 divide 896); a 3-plot tile that does not gets a page of
  the same rule whose window starts 64 samples before it. This replaces the per-tile R16G16B16A16 mip-0 textures of
  section 2 (huge: 60 pages, 84 MB, against 2,688 x 134^2 x 8 B = 386 MB). With it the edges follow the EXE: the pages wrap
  in both axes, where the old textures read 0 outside the grid.
- Per-tile z bounds for culling over the texels of levels 0-4 the tile can read (the sharpen can leave mip 0's range).
- Per frame: the list = the EXE tiles of class 3 (`exeTileClass3`, at `setHeightField`) whose sphere is visible in
  ViewProj; M, the hysteresis, LodMip and TessFactor as above (`exeFrameLod`, `exeLodUpdate`; state `Renderer::lodState`,
  reset by `setHeightField` and `resetLod`, advanced only by a draw that succeeds); the width = `Options::lodWidth` (the
  server passes the camera block's viewport width vpW, [I] equal to [0x301056C]) or the target's. Tiles are drawn with
  LodMip = L, LodStep and (n - 1)^2 patches from `exeLodGrid(P + 1, L)` (one index buffer per level). `lodLevel = 0..4`
  fixes a level (the module before 0191 drew 0). FrameStats: lodLevel, lodMip, lodStep, lodMin, lodValue, lodVisible,
  lodAccepted; the server's 1 s line ends with ` | lod L= mip= step= tess= min= value= visible=`.
- Tests [run 2026-11-14 20:27, RTX 4080, `c5r_terrain_test` 37 run, 0 failed, 0 skipped]: `lod_rule_tile_values_and_grid`,
  `lod_hysteresis_follows_the_exe`, `negative_exe_frame_lod_inputs`, the two `game_lod_*` above;
  `draw_lod_levels_follow_the_page_mips` (levels 0-4 on a bumpy field: worst |z - CPU rule| 0.0015 / 0.0008 / 0.0005 /
  0.0004 / 0.0056 at tess 8-64; 85-98 % of the pixels differ from the level below by > 0.05);
  `draw_exe_lod_picks_the_level_and_keeps_state`; `game_l1_surface_at_vertices_huge`: the huge field at Thebes (window x
  6368-6623, y 1632-1823), L 1, tess 2, an orthographic target with one pixel per sample: **all 49,152 vertices on the CPU
  rule within 0.00036**, on average 0.075 below the level-0 surface (p95 |dz| 0.26, max 0.55), and **equal to 0154's
  independent `lod_surface_0154.py` within 0.00036 (p95 0.00021)** (`render/evidence/0191/compare_l1_0191.py`, result
  `render/evidence/0191/results/l1_vs_0154.json`). The existing surface tests pin `lodLevel = 0`. Also run
  [2026-11-14 20:30-21:05]: the terrain test under ASan (0 reports), the whole render suite (23 of 23), server/tests/render
  (139: all pass once the overlay and water folders' tools are built) and shell/tests/test_terrain_scene.py (92 passed;
  the Tiny v1 terrain share of shell_server's terrain scene, which now draws the EXE's LOD, is 0.5912 against 0.5972 at
  L = 0 (2,130,664 px against 2,142,865): the lower surface leaves a little more water visible; the test allows 0.015).
- Not reproduced: the last column / row's d4 = 64 in the patch grid (every module tile has P + 1 vertices; d4 = 64 enters
  only the rule's box and n); tile+0x1C0's own meaning; TriSize / Subdiv other than Options' (the server keeps 4 / 2).

**Live, the product path against stock at 9055's cameras [L, oldpc, 2026-11-14 20:39-20:49 EST].** Stock arm: job
`20261114-203537-79a7` above. Product arm: job `20261114-204449-616f` (`render/evidence/0191/live_pp_0191.py`:
product_path.py's D33 prep with Overlay on, tb1, the same save; server.exe 476B72A7... and proxy 43490192... of this branch at
a55c287a (release, x64 DLL 74588135..., the D96 pin's), composite 42003E0B...; in game 134 s), driven by
`drive_live_0191.py --arm product` through the same LookAt sequence. **The server's LOD equals the EXE's at all three
cameras, hysteresis included** (the server's 1 s line against the stock arm's TR read):

| Camera | EXE (stock): LodMip, TR+0x242C, list | server (product): L, held value, frame minimum, list |
|---|---|---|
| load view | 1, 1.98248, 84 | 1, 1.9825, 1.9825, 84 |
| LookAt Thebes (101, 25) | 2, 2.01593, 121 | 2, 2.0158, 1.9957, 121 |
| LookAt Akhetaten (94, 16) | 2, 2.02268, 113 | 2, 2.0228, 1.9756, 113 |

So on this machine the server draws mip 2 at LodStep 4 at the two city cameras, as stock does, though the frame's own
minimum is below 2: it followed the camera's path with the EXE's hysteresis. Whole-screen grabs (1920 x 1080, both arms
by the same method; client area 1914 x 1051 compared; `compare_live_0191.py`, `results/live_compare.json`), product against
stock, largest channel difference per pixel: load median 9 / p95 119 / max 250, 62.1 % within 16; Thebes 6 / 117 / 249,
66.8 %; Akhetaten 7 / 87 / 250, 73.0 %. These are dominated by what the product does not draw yet (D33: forests, features,
decals; the minimap; units and ships animate between the two launches) and are no measure of the LOD; the pictures and
difference images are DIAGNOSTIC (D112) in the Visual Checks feed under 0191. Asset set (D94): oldpc tb1, VT present.
The rims' metric of 0134 needs the workstation's passthrough set-up and was not re-run [not verified].

**3019's replay with the module's LOD [L, workstation RTX 4080, 2026-11-14 20:55 EST].** `render/evidence/0191/replay_0191.py`
on 3601's two stock captures (exported again by 3019's `terrain_export.py`; 3019's `terrain_replay.exe`): every scene
terrain draw's TerrainTile LodMip / LodStep and TerrainFrame TessFactor replaced by the module's values for the capture's
camera and width (camera 1, zoom 17: 2 / 4 / 2; camera 2, zoom 11: 0 / 1 / 2; the rewritten buffers are byte-equal to the
captured ones in all 150 + 58 draws): **0 pixels differ** from the replay as captured at both cameras (1,419,353 and
2,795,230 land px; the whole 2554 x 1411 target). The lod0 variant (the module before 0191) against the same replay:
camera 1 median 2 / p95 15 / max 162, MAD 3.3/3.2/2.0, 95.9 % within 16 (3019's row); camera 2 0 (LodMip 0 there).
Results `render/evidence/0191/results/replay_cam{1,2}.json`; pictures DIAGNOSTIC in the Visual Checks feed under 0191.

## 8. The EXE's pass per tile, the spec map t9 and FlagB (issue 0189, 2026-11-14 21:34-, impl-B-0189, branch `task-0189-terrain-pass0`)

The spec item of `terrain-colour-reference.md` 2 (`pass1`: camera 1 p95 1, max 182). Facts from fresh exports of 3601's two
stock captures (`c5h-c_20261113-235332_frame{309938,265572}.rdc`, Tiny `C5H-3600-TINY_0009V`, 32-bit pin
`i0ship-26234371-ia`, RTX 4080, 2554 x 1411, VT present, D94) by 3019's `terrain_export.py` into
`C:/c5-64bit-work/0189/exports/cam{1,2}` (game data, D83), read by `render/evidence/0189/pass_inputs_0189.py` (numbers in
`render/evidence/0189/results/pass_inputs_cam{1,2}.{txt,json}`).

**What the pass-0 PS does** [C, the SM5 disassembly of PS 0x067e6e58, `D:/c5-64bit-work/2900/2902/dump/asm/`, read only]. It
is pass 1's PS (P11) with one step more before the shadow: with c = t0(uv) when FlagC (cb1[28]) else 0, s = t8(uv).rgb
(the tile's spec cache, sampler 8) and m = t9(0.5 + 0.5 * v5.xy).rgb (sampler 9; v5 is the DS's TEXCOORD3, the same input
whose y gives the fog ramp's u, i.e. a screen-space lookup), the colour is **(c^2.2 + s^2.2 * m)^(1/2.2) when FlagB
(cb1[27], TerrainFrame 0x1B0 = TR+0x241C != 0) is non-zero, else c** (a `movc` on FlagB), then shadow, overlay, FOW and
distance fog exactly as pass 1. So with FlagB 0 pass 0 is pass 1's picture.

**The EXE's choice at the two cameras [L]:** 150 (58) scene terrain draws, 145 (57) with the pass-0 PS (SHA-256
3803fb8b...), 5 (1) with pass 1 (77e9b77a...). **Every pass-0 draw binds a t8 of its own** (a tile cache: 256 or 512 texels,
bound by no other draw), every pass-1 draw keeps one shared 8 x 8 RGBA texture (resource 871, the same in both captures) in
the slot its PS does not declare: the rule "pass 0 iff the tile has a t8" (0x5C37E0's tile+0x2FC, P17) gives the EXE's PS
in 150 of 150 and 58 of 58 draws. **FlagB = 1.0 in every pass-0 draw**, FlagC 1, Arg6 1; LodMip 2.0 (camera 1) / 0.0
(camera 2) in every draw, the module's values since 0191 (section 7).

**t9 is `specscreenmap.dds`** [L]: resource 43161, BC3_UNORM 1024 x 1024, 11 mips, 1,398,128 bytes, one texture in every
pass-0 draw, **byte-equal to the payload of the install's `specscreenmap.dds`** (`Resource/DX9/TerrainTextures.fpk`, DXT5,
11 mips; SHA-256 9f4ca750...; install `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`). This confirms
asset-pipeline.md 3.4's [C] row for t9 (0xDA12C8 `assets/terrain/lighting/specscreenmap.dds`).

**Seen in passing [L, not this ticket]:** the EXE's t0 / t8 in these draws are mostly the near-camera RGBA8 caches
(tile+0x190 / +0x194, terrain-bake-passes.md 3.5): camera 1 107 draws R8G8B8A8 256 (9 mips) and 32 R8G8B8A8 512 (10 mips),
6 BC1 256; camera 2 32 R8G8B8A8 512 and 25 BC1 256. The server's residency draws BC1 caches only, so its caches differ from
the EXE's there by the BC1 rounding; 3019's replays use the EXE's own caches and do not see it. Proposed as a new ticket in
0189's hand-back. *Measured by 0306 (2026-11-15): section 10 (camera 1 t0 counts corrected there: 112 RGBA8 256, not 107,
which is t8's count); when the EXE makes them: terrain-bake-passes.md 3.5.1.*

**The module** (`render/src/terrain/terrain.cpp`, `terrain.h`) [C, run as marked]:
- `Options::pass` -1 (the default; the server's `render_pass` -1) draws a tile whose cache has a t8 (`TileCache::second`,
  which the residency sets exactly when the bake made a t8: `residency.cpp` `drawSrv`, 0137) with pass 0 and the others with
  pass 1; 0..2 still force one pass for every tile. The HS (0x067f98f0 for both) is set again only when the pass changes.
- `init(dev, exe, gameDir)` reads `specscreenmap.dds` with the fog ramp through one `c5r::gamefs::GameFiles`; a missing or
  unreadable spec map is an init error that names the file (as the ramp's). Without gameDir t9 is black and FlagB 0.
- FlagB = `Options::spec` (default true) and the spec map bound; t9 = the spec map (sampler state of the set's table, 5).
- FrameStats: `tilesPass0` / `tilesPass1` (draws through the EXE's PS by pass, wrap copies counted), `specOn`, and over the
  EXE's visible list (the tiles the EXE draws, 0191; counted with kLodExe, Caches and 1-plot tiles, whether or not the
  module's own box cull draws the tile) `listPass0`, `listPass1`, `listNoCache` and `listPass0Hash` (FNV-1a 32 over the
  pass-0 tile indices ty * tilesX + tx, 4 bytes little-endian, increasing), to compare with the EXE's tile+0x2FC live.
- The server (`server/src/render/srv_render_scene.cpp`): the init line ends `distance fog 0|1, spec map 0|1`; an install
  that gives no ramp or no spec map is one LOUD line and the terrain is drawn without both (the init falls back to no
  install); the `terrain 1s:` line ends ` | pass p0= p1= spec= list_p0= list_p1= list_nocache= list_p0_hash=` (-1 / 0 when
  not counted).
- `FrameStats::passUsed` is now `Options::pass` as given (-1 for the per-tile choice); it was the resolved pass (1).

**Checks [L, 2026-11-14 21:40-21:52 EST, RTX 4080]:**
- `c5r_terrain_test` 41 run, 0 failed, 0 skipped (plain), and under ASan (`render/build.py --asan`) 0 reports; the whole render
  suite 23 of 23; server/tests/render 140 passed (main, overlay, water); shell/tests/test_terrain_scene.py 92 passed.
  New tests: `draw_pass_per_tile_follows_t8` (2 of 4 tiles with t8 drawn with pass 0, the other 2 with pass
  1; forced passes; without an install the per-tile, pass-0 and pass-1 pictures are bit-equal, 0 bytes differ),
  `draw_pass_counts_over_the_exe_list` (16 listed tiles: 5 / 8 / 3 and the hash as computed),
  `game_spec_map_is_the_exes_and_lights_pass0` (the module's t9 is 1024 x 1024 BC3 with 11 mips, 1,398,128 bytes,
  FNV-1a 64 0xfff35e0f81f61405 = the capture's t9; with a white t8 the pass-0 tile gets brighter on 7,011 of 7,207 pixels, up
  to +140, none darker, the pass-1 tiles bit-equal), `negative_init_install_without_a_spec_map` (NotFound naming
  `specscreenmap.dds`; a 7-byte file: Truncated naming it; nothing half-made).
- **3019's replay with the module's pass inputs** (`render/evidence/0189/replay_0189.py`, replayer
  `render/tools/terrainreplay` of this branch's render build): every scene terrain draw with the PS of the module's pass,
  t9 = the install's payload and FlagB 1 (the rewritten inputs are byte-equal to the captured ones in all 150 + 58 draws):
  **0 pixels differ** from the replay as captured at both cameras (1,419,353 and 2,795,230 land px; the whole 2554 x 1411
  target). The `pass1` variant (the module before 0189) against the same replay: camera 1 median 0 / p95 1 / max 182, MAD
  0.8/0.8/0.9, 98.8 % within 16, 191,325 pixels differ; camera 2 0 / 4 / 52, MAD 0.4/0.4/0.6, 99.2 %, 445,716 pixels
  (3019's rows). Against the stock picture of the same launch on 3019's land mask (NOT the clean mask: shore water,
  rivers, trees, units and the interface are in it), median / p95 / max, within 16, MAD, signed: camera 1 with the
  module's inputs 0 / 116 / 245, 60.6 %, 19.4/15.9/13.7, +11.0/+6.0/-4.0 (pass 1: 1 / 118 / 245, 59.7 %); camera 2
  0 / 102 / 255, 74.7 %, 14.5/13.7/10.8, -2.4/-3.1/-5.6 (pass 1: 0 / 102 / 255, 73.9 %). Results
  `render/evidence/0189/results/replay_cam{1,2}.json`; difference images DIAGNOSTIC (D112) in the Visual Checks feed under
  0189 (vc-20261114-215126-workstation-70c5 .. -56f3).

**Live, the product path against stock at 9055's cameras [L, oldpc, 2026-11-14 21:53-22:04 EST].** Huge TB-HUGE_0350 turn
350, tb1 (the machine's own Steam EXE, VT present, D94), 1914 x 1051, default zoom, the cameras of 0191 (section 7).
Stock arm: job `20261114-215345-7292` (`tools/testbed/jobs/live_session.py`), cdb read of every tile of the EXE's visible
list with its tile+0x2FC (`render/evidence/0189/drive_live_0189.py --arm stock`, numbers in
`render/evidence/0189/results/live_stock_oldpc.json`). Product arm: job `20261114-215857-be8d`
(`render/evidence/0191/live_pp_0191.py`: proxy, server.exe ServerRenderer = 1, composite, D33 with Overlay on; server.exe
065A5B25... and proxy FC71DC77... of this branch at 4d3c404c, release, x64 DLL 74588135... = the D96 pin's, composite
CF011BF1...), the server's `terrain 1s` line (`--arm product`, `results/live_product_oldpc.json`). Both rc 0, restored,
provision verify ok; the product run's server, render, proxy and composite logs hold 0 LOUD lines; the init line ends
`distance fog 1, spec map 1`.

| Camera | EXE: listed, tile+0x2FC = 1 (pass 0), = 0 (pass 1) | server: listed, list_p0, list_p1, no cache; spec |
|---|---|---|
| load view | 84, 84, 0 | 84, 72, 12, 0; 1 |
| LookAt Thebes (101, 25) | 121, 120, 1 | 121, 64, 57, 0; 1 |
| LookAt Akhetaten (94, 16) | 113, 113, 0 | 113, 93, 20, 0; 1 |

- **The pass rule holds, the server's t8 set does not yet equal the EXE's.** The EXE draws nearly every listed tile with
  pass 0 (317 of 318); the server gives a t8, and so pass 0, to 229 of them: every listed tile has a cache, but 12 / 57 / 20
  have no t8. The server's bake makes t8 by the layer list only on a first bake (`areaChanged` false; 0137) and the EXE's
  tile+0x300 ("area changed since load", set by the rectangle invalidation 0x4387D0 from the decal, road and improvement
  paths, terrain-bake-comparison.md 7) is not known to the server at load: 3407 measured the same gap offline ("with the
  list rule alone the server makes no t8 for 182 (Tiny) / 827 (huge) of the EXE's t8 caches, and their t0 come from
  CacheBake pass 0 instead of pass 1"). The gap is largest at Thebes (the capital's improvements and roads). Those tiles
  lose the spec term (3019's `pass1` size: p95 1-4, max 52-182 per camera) and their t0 is CacheBake pass 0's. Fix proposed
  as a new ticket in 0189's hand-back: the server's tile+0x300 at load (which tiles the EXE's load invalidates) [I: the
  decal system's placements over the plots with improvements, roads and other decals].
- The tile-for-tile check of the pass-0 set (`list_p0_hash` against the hash of the EXE's tile+0x2FC set) is therefore not
  equal on this save; the counts above are the comparison. The grabs of both arms are DIAGNOSTIC in the Visual Checks feed
  under 0189 (oldpc); an `Apple Software Update` window of oldpc's desktop stands over the product grabs (noted in the
  entries; nothing on a test machine is changed, D41).

## 9. The models' shadows on the ground: t3, ShadowMatrix0, Arg6, t7 (issue 0190, 2026-11-14 23:20-, impl-B-0190, branch `task-0190-terrain-shadow`)

The shadow item of `terrain-colour-reference.md` 2 (`noshadow`: 1.7 % / 2.5 % of the land pixels change by more than 16,
max 81 / 82). The ticket's first question: under D33, with the server drawing the terrain and the EXE the models, which
process draws the models' shadow map, and can the server take it.

**Answer: the EXE draws it every frame, also under D33; the server takes it (a tap), it cannot draw its own (it has no
casters).** Evidence:
- **The shadow pass 0x523CA0 [C, disassembly `D:/c5-64bit-work/2900/2903/dx11.asm`, read only].** Called every frame by
  both 3D render passes (0x589524, 0x749500). It returns at once when ShadowRaw `[0xDA12B8]` is 0 or the shadow quality
  `[0xD90F9C]` is 0. Then the camera's eased shadow factor GVS+0x78C: when the quality is 1..2 and the flag `[0xD90F80]` is
  set, its target is 0 if `[0xD90FB0]` > `[0xD90FB8]` - 2, 0.5 if `[0xD90FB0]` >= `[0xD90FB8]` - 3, else 1, and the factor
  moves 0.1 of the way per frame (snapping within 0.01); otherwise (quality above 2, or the flag clear) it is 1.0. The
  factor is stored to `[0xD90FA8]`, **which is TerrainFrame.Arg6** (asset-pipeline.md 3.3) and WaterFrame 0x220
  (water-system.md 10.1). [I: 0xD90FB0 / 0xD90FB8 are the camera's zoom and its maximum: shadows fade out at the far
  zooms on lower qualities.] *(Confirmed 2026-11-15 [L], 11 S4 and 11.9: the zoom and its maximum 20.5; the fade seen at
  q 2 and q 1; the quality is the ini's `ShadowLevel`.)* A factor below 0.001 clears ShadowBlur and draws nothing. Otherwise it clears ShadowRaw, binds
  it, and lets each caster system draw into it, **each gated only by its own bit of the per-view mask GVS+0x1B4**: Unit
  0x40 (0x6D6830), City 0x20 (0x63BBE0), Landmark 0x2 (0x559DF0, then DynamicGeometry 0x674AD0), Particle 0x1000
  (0x4C0190); then it tail-calls TerrainSystem 0xD165E0's 0x5580B0, which at quality >= 2 binds ShadowBlur `[0xDA12BC]` and
  draws one resample with the TerrainSystem's own draw object (TS+0x75F9C). **Nothing in the pass reads Terrain's bit 0x1**,
  the bit the proxy clears under D33; Terrain stays on in config.ini under D33 (`shell/tools/darkred.py` D33_ON), so its
  start-up makes ShadowRaw, ShadowBlur and the resample's draw object. Under D33 Unit, City and LandmarkSystem are on and
  Particle is off: the EXE's map holds the units', cities' and landmarks' (improvements, wonders, resources) shadows.
  Trees are not casters here (Forest is not called by the pass; their shadows are terrain decals, `tree_shadow_2`,
  terrain-rendering-inventory.md 2).
- **The chain in the frame [L, `render/evidence/0190/shadow_draws_0190.py` on RenderDoc's zip.xml conversions, 2026-11-14
  23:25].** 3601's stock capture `c5h-c_20261113-235332_frame309938` (Tiny 9V, c5h-c, 32-bit pin `i0ship-26234371-ia`,
  2554 x 1411, VT present): draws 29-168 (140) into a 2554 x 1411 R16G16B16A16_FLOAT target (no depth, the back buffer's
  size: ShadowRaw) with their own textures (BC1 / BC2 diffuse, the fog ramp), by shaders not in the material table; draw
  169 (6 indices, one quad, viewport 2048 x 2048) reads it as t0 and writes the 2048 x 2048 R16_UNORM target (1 mip:
  ShadowBlur, resource 13347); then every one of the 300 TerrainShader draws (scene and pass 2) binds 13347 as t3 with Arg6
  1.0, and the four ocean draws bind it as t13. 3310's black arm `c5h-c_20261114-010946_frame14671` (the D33 configuration
  without the view-mask write): the same chain, 104 caster draws, one resample (draw 104), 160 TerrainShader draws with t3
  = the R16 target and Arg6 1.0. **Not seen live: a capture of the product path (Terrain's bit cleared)**; the [C] reading
  above is the evidence for it. *Seen 2026-11-15 by 4302 (11, S1 and the RenderDoc arm) [L]: on the D33 product path
  (proxy, server, Terrain's bit cleared by the proxy's rule) the pass runs every map frame: 115 / 73 caster draws into
  ShadowRaw and the one resample into the 2048 x 2048 R16_UNORM ShadowBlur, and no EXE draw reads ShadowBlur.*
- **What the PS does with it [C, PS 0x067e8d68 disassembly; L, the module's test below].** p = (world, 1) * ShadowMatrix0
  (cb1 registers 31-34 as rows, the world position the DS passes as TEXCOORD2), **u = 0.5 p.x / p.w - 0.5, v = -0.5 p.y /
  p.w - 0.5** (under sampler state 0's wrap the same texels as the usual 0.5 x + 0.5, -0.5 y + 0.5), s = saturate(t3(u,
  v).r) * Arg6, colour = lerp(colour, t7(colour), s) before the overlay, the FOW and the fog (P11). *First written here
  (23:40) without the divide and the NDC mapping; the module's wrap test failed on that reading and passed on this one.*
  So ShadowMatrix0 maps world positions to ShadowBlur's
  clip space: an orthographic light projection [I, from its values: camera 1 row 0 (0.001563, 0.000285, 7.2e-6), row 2
  (-0.000782, 0.000569, 1.44e-5), row 3 (-0.5516, 0.2932, 0.4135); it moves with the camera: camera 2 (0.003835, ...), row
  3 (-5.645, 0.4659, 0.4050)], one per view (0xDA1790 + 0x40 v, written by the shadow view setup 0x615950). Being a world
  mapping, a wrap copy takes its view's matrix as the overlay's (Translation(-k W) * M_k, overlay-system.md 4).
- **t7 is the TerrainShadow ColorKey volume [L].** Both captures bind two t7 resources (2449, 2455: two styles) with the same
  131,072 bytes (SHA-256 b68239d5...; FNV-1a 64 0xdf1432ef669171e5). The install's five `Assets/ColorKeys/TerrainShadow_<Style>
  _ColorKey.tga` are byte-equal (SHA-256 62268bac..., c5h-c's install, 2026-11-14), so one volume serves every style.
  *(2026-11-15: equal on all ten installs read by 4303, 12.2; the module binds each tile style's volume anyway since
  4305, 12.6, so an install whose files differ is drawn as the EXE does.)*

**The server's route (MC #1218, 2026-11-14, route (a)):** a shadow tap like 0167's overlay tap (copy ShadowBlur on the EXE's
device into NT-shared slots at each map frame, with the frame's ShadowMatrix0 per view and `[0xD90FA8]`, and a reader in
server.exe) is its own ticket, drafted as `tracker/issues/NEW-shadow-tap.md`; 0190 delivers the module side.
*2026-11-15 (epic 0169; replaces "drafted" as the state): numbered 0312; its premises checked live by 4302 (11), the
tap and reader built by 4304 (11.9), bound into the terrain and water by 4308 (11.10), live on the product build by
4310 and 4311 (11.11, 13.6, 13.7).*

**The module** (`render/src/terrain/terrain.cpp`, `terrain.h`) [C, run as marked]: `Options::shadow` (`Shadow`: srv, a
Texture2D R16_UNORM view; strength = Arg6, finite in [0, 1]; matrix[3][16] = the EXE views' ShadowMatrix0 per wrap copy);
`shadowMatrixForCopy` (the overlay's rule); init reads `TerrainShadow_Generic_ColorKey.tga` through
`bake::buildColorKeyLut` (missing: identity, as the EXE's loader; not parsing: an init error naming the file);
`Renderer::shadowLut`, `shadowLutFromFile`; `FrameStats::shadowOn`, `shadowLutFromFile`. With a shadow bound: Arg6 =
strength, t3 = the view, ShadowMatrix0 per wrap copy, t7 = the LUT. Without (the default): Arg6 0, t3 white, ShadowMatrix0
identity, t7 identity: bit-equal to the picture before. Functions changed: `Renderer::init` (the LUT), `Renderer::draw`
(validation, Arg6, ShadowMatrix0 per copy, t3 / t7), `Impl::release`; added `shadowMatrixForCopy`, `shadowLut`,
`shadowLutFromFile`. The server's scene does not bind a shadow yet (no source until the tap).
*Corrected 2026-11-15 (epic 0169; replaced as the current state: "a Texture2D R16_UNORM view", "init reads
`TerrainShadow_Generic_ColorKey.tga`", "does not bind a shadow yet"): `Options::shadow` also accepts R16G16B16A16_FLOAT
(quality 1, the PS reads .r; 4308, 11.10); init reads the five `TerrainShadow_<Style>_ColorKey.tga` and the draw binds
the tile style's volume as t7 (4305, 12.6); the server's scene binds the shadow tap's triple in every frame since 4308
(11.10, 11.11).*

**Checks [L, 2026-11-14 23:33-23:45 EST, RTX 4080; acceptance text `render/evidence/0190/acceptance_0190.txt`]:**
- `c5r_terrain_test` 45 run, 0 failed, 0 skipped. New: `negative_shadow_checks` (a non-finite matrix of each copy, a
  strength NaN / inf / -0.01 / 1.01, an R8G8B8A8 or R32_FLOAT view: BadLayout, stats untouched), `draw_shadow_off_is_bit_equal`
  (no shadow against a zero map at strength 1 and a map of ones at strength 0: 0 bytes differ),
  `game_shadow_map_matrix_and_lut_are_the_exes` (the module's t7 is 32^3 R8G8B8A8_UNORM with FNV-1a 64 0xdf1432ef669171e5 =
  the captures' t7; a half-shadowed 256 x 1 map: copy 0's ground under the map's shadow texels equals the full-shadow colour
  on 6,720 of 6,720 px (cache (192,160,96) -> (112,87,51) through the LUT), copy +1 through Translation(-W) * matrix[2]
  unchanged on 6,720 of 6,720 px, strength 0.5 between off and full on every channel), `negative_init_install_shadow_lut`
  (no file: init ok, identity t7; a 4 x 4 TGA: BadLayout naming the file, nothing half-made).
- The whole render suite 23 of 23 (plain), and under ASan (`render/build.py --asan --test`) 23 of 23 with 0 reports.
- **3019's replay with the module's shadow inputs** (`render/evidence/0190/replay_0190.py`; exports of 3601's two stock
  captures by `cli.py rdc-export --select terrain` into `C:/c5-64bit-work/0190/exports`, game data): in all 150 + 58 scene
  terrain draws t3 is the 2048 x 2048 R16_UNORM ShadowBlur, the module's t7 equals the captured one byte for byte, Arg6 1.0
  and ShadowMatrix0 equal: **0 pixels differ** from the replay as captured at both cameras. The server before 0190 (`noshadow`,
  Arg6 0) against the same: camera 1 on 1,419,353 land px median 0 / p95 0 / max 81, MAD 0.7/0.7/0.3, 98.3 % within 16,
  52,454 px differ; on the 51,086 land px the shadow changes median 15 / p95 55 / max 81, 52.8 % within 16, signed
  +19.6/+18.3/+7.1 (too bright). Camera 2 on 2,795,230 land px 0 / 0 / 82, MAD 1.2/1.1/0.7, 97.5 %, 102,731 px differ; on its
  100,915 shadowed land px 35 / 69 / 82, 32.2 %, signed +33.4/+30.6/+19.8. Against the stock picture (3019's land mask, not
  the clean mask) the shadow moves camera 1 from 60.0 % to 60.6 % within 16 and camera 2 from 73.8 % to 74.7 %. Results
  `render/evidence/0190/results/replay_cam{1,2}.json`; pictures DIAGNOSTIC (D112) in Visual Checks under 0190
  (vc-20261114-234336-workstation-5a5f .. -89c6).
- Not verified: the product path live (no source until the tap); a capture of the product path with Terrain's bit cleared
  (the [C] reading stands for it); shadow qualities other than the instances' (Arg6 1.0 in every capture); the EXE's
  wrap views' ShadowMatrix0 (0xDA1790 + 0x40 v, v > 0, not read: the per-copy rule is the overlay's [I], tested
  synthetically).
  *Each settled 2026-11-15 by epic 0169: the product path live in 11 (4302: the chain under Terrain's bit cleared, the
  RenderDoc arm's captures of the product path) and bound in 11.10 (4308: frames_bound = every drawn frame) [L];
  qualities 3, 2, 1, 0 live through the tap (11.9 [L]; q = `ShadowLevel`; Arg6 eases at q 2 and 1); the wrap views'
  ShadowMatrix0 read live (11 S3: view 1 = view 0 moved by k W, k = +-1, within 2.2e-7; bitwise equal to the tap's on
  Tiny, 11.9, and on huge, 13.7, where g_NumViews is never above 2) [L, C]. Still not verified here: qualities other
  than 3 drawn live by the server (11.10's not verified).*

## 10. The EXE's near-camera RGBA8 caches against the server's BC1: the replay (issue 0306, 2026-11-15 00:59-, impl-B-0306, branch `task-0306-near-camera-caches`)

Section 8's "Seen in passing" measured: the EXE draws most visible tiles from its near-camera pair (tile+0x190 / +0x194,
R8G8B8A8, no compression; when and which tiles: terrain-bake-passes.md 3.5.1), the server's residency draws BC1 caches only.
What that costs a pixel-faithful picture (D66), from 3601's two stock captures (exports by 3019's / 0182's
`render/tools/terrainreplay/export.py` in `C:/c5-64bit-work/0306/exports/cam{1,2}`, game data, D83, removed at the hand-back: re-export with the same command; captures as section 8;
the workstation's install with Vibrant Textures, D94).

**The encoder [L].** `render/evidence/0306/bc1enc.cpp` repeats the server's DXT Compress step (c5r::bake::BakePost, the
EXE's CompressBC blobs 0x06743180 / 0x067434f0 read from the EXE at run time; same quad, states, sampler, viewport, one draw
per mip) on any RGBA8 chain. Against the EXE's own blocks of 3403's tile P (`D:/c5-64bit-work/3400/w3/ref380`,
`P_cache{0,1}` -> `P_blocks{0,1}`; `encoder_check_0306.py`): **5,460 of 5,460 blocks equal for t0 and for t8 on the
hardware device** (RTX 4080); on WARP 65 of t0's 5,460 blocks differ (t8 equal), so the replay uses the hardware device.
Results `render/evidence/0306/results/encoder_check_{hardware,warp}.json`.

**The replay [L, 2026-11-15 01:07-01:09 EST]** (`render/evidence/0306/replay_0306.py`, replayer `terrainreplay` of this
branch's render build): every RGBA8 t0 / t8 of the scene terrain draws (camera 1: 283 textures, 219 of 256 and 64 of 512, in
144 of 150 draws; camera 2: 64 of 512 in 32 of 58 draws) replaced by its BC1 encoding, everything else as captured, against
the replay as captured. Variant `bc1`: the same size, mips 0 .. 5 (256) or 0 .. 6 (512) encoded from the cache's own mips
(the BC1 rounding alone). Variant `bc1_256`: a 512 cache replaced by a 256 BC1 encoded from its mips 1 .. 6 (an
approximation of a 256 cache of the same tile, which the EXE renders at 256 rather than box-filtering 512). Largest channel
difference per pixel (3019's colourstats), on 3019's land mask and on every pixel of the draws whose caches were replaced
(the replay's id buffer, under-water terrain included):

| Camera, variant, pixels | median / p95 / max | MAD R/G/B | signed R/G/B | within 4 / within 16 | equal |
|---|---|---|---|---|---|
| 1 (zoom 17), `bc1`, land 1,419,353 | 3 / 8 / 53 | 2.0 / 1.8 / 2.3 | -0.2 / -0.1 / +0.2 | 78.0 % / 99.8 % | 1.1 % |
| 1, `bc1`, near-cache draws 2,362,162 | 2 / 7 / 53 | 1.7 / 1.6 / 2.0 | -0.2 / -0.1 / +0.1 | 84.0 % / 99.8 % | 2.0 % |
| 1, `bc1_256` | **bit-equal to `bc1`** (no 512 cache's mip 0 is sampled at this zoom) | | | | |
| 2 (zoom 11), `bc1`, land 2,795,230 | 0 / 5 / 72 | 0.6 / 0.6 / 0.7 | -0.1 / -0.0 / +0.1 | 93.7 % / 99.9 % | 66.5 % |
| 2, `bc1`, near-cache draws 953,595 | 3 / 8 / 72 | 1.9 / 1.7 / 2.1 | -0.2 / -0.1 / +0.2 | 81.6 % / 99.7 % | 1.7 % |
| 2, `bc1_256`, near-cache draws | 3 / 8 / 72 | 2.0 / 1.8 / 2.3 | -0.2 / -0.1 / +0.3 | 79.2 % / 99.5 % | 1.6 % |

Pixels differing anywhere in the 2554 x 1411 target: camera 1 2,387,921, camera 2 986,667 (`bc1_256` 987,085). Against the
stock picture of the same launch on the land mask (NOT a clean mask: water edges, rivers, trees, units and the interface
are in it), median / p95 / max, within 4, equal: camera 1 replay as captured 0 / 116 / 245, 53.7 %, 53.1 %; `bc1` 7 / 116 /
246, 40.7 %, **0.5 %**; camera 2 as captured 0 / 102 / 255, 69.5 %, 66.8 %; `bc1` 0 / 102 / 255, 65.5 %, 51.0 %.

Reading: the BC1 rounding is small (MAD about 2, p95 7-8, 99.5-99.8 % within 16, no colour shift: signed means within
0.3) but it touches nearly every pixel of a near-cache tile (1.6-2.0 % stay equal); the pixels that equal the stock picture
exactly fall from 53 % to 0.5 % of the land at camera 1. Where the server draws BC1 for a tile the EXE draws from its near
pair, a pixel-for-pixel match is out of reach by this much. Results `render/evidence/0306/results/replay_cam{1,2}.json`;
difference images (x16 and x4, DIAGNOSTIC, D112: offline replay, terrain only) in `F:/c5-64bit-work/0306/replay/cam*/diff/`
and the Visual Checks feed under 0306 (vc-20261115-010923-workstation-2ca7, -010925-0d01, -010926-6077, -010927-f024).

**Live: which tiles carry a near pair [L]** (terrain-bake-passes.md 3.5.2): at 3601's camera 1 (zoom 17) 144 of 150 visible
tiles (112 at 256, 32 at 512), at camera 2 (zoom 11) 32 of 57, at the load view 42 of 110, one tick out of camera 1 144 of
251; the rest draw BC1. The split depends on the zoom, the camera's history and the EXE's fixed pools (256 x 256, 64 x
512 textures), with no fallback to 256 when the 512 pool is empty.

**Recommendation (0306 (c), 2026-11-15) [I from the numbers above].** For D66's pixel-for-pixel close views the server
needs the near pair: BC1 alone leaves almost every pixel of a near tile off by a few levels, which no other input can make
up. It needs the EXE's rule for which tiles carry it, not "every visible tile": in the EXE's own picture the closest
tiles can be BC1 (camera 2), so a server that gives every tile an RGBA8 pair would miss there by the same rounding the
other way. Under the D33 product path the EXE's cull does not run, so the server reproduces the state machine itself
(up-res two per settled frame in the cull's sort order, the pool counts, no fallback in array mode, release and re-request
rules; terrain-bake-passes.md 3.5.1). Proposal `tracker/issues/NEW-server-near-camera-rgba8-caches.md` (touches
`render/src/bakepost/`, `render/src/bakeresidency/`, `render/src/terrain/`, `server/src/render/` and their tests; 10-16 h;
about 179 MB of video memory at the stock pool sizes). Its rank: the residual is small (MAD about 2, 99.5-99.8 % within 16,
no colour shift) against the open terrain residuals of section 8 (the t8 set at load: p95 1-4, max 52-182 on the tiles it
touches) and the decals; it can follow those.

## 11. The shadow tap on the product path: the live checks (epic 0169, task 4302, 2026-11-15)

(4302: skeleton by lead-0169. The premises of 0312's plan checked live on the D33 product path: the shadow chain under
Terrain's bit cleared, the ShadowBlur handle and its view-table entry, g_NumViews and ShadowMatrix0 per view including the
wrap views, the factor `[0xD90FA8]` and the shadow quality, the frame timing at the composite's Present, the byte patterns
the producer checks, the copy's cost as an indication.)

**The run [L, 2026-11-15 12:44-12:51 EST, workstation, instance c5h-e (F:), worker 4302].** The D33 product path of
3308's driver (`shell/evidence/0169/4302/run_4302.py`, which imports `shell/evidence/3308/run_3308.py` as 3703 and 3904
did): proxy, server.exe and x64 DLL from the pinned layout `x64-04b1211b-pfdee656` (COMMON 18: proxy 75C40CEC..., server
5B254430..., DLL 29F58E17...), the composite built from this branch (`shell/build.cmd`, 69FCA9DA...), VP64Options.ini
`ServerRenderer = 1`, config.ini D33 by darkred.py's list with Overlay on (D93: OverlayDebug, Decal, Forest, River, Combat,
Particle, Projectile, FOW, FluidFOW, YieldIcons, DistanceFog, ColorKeyEdit, Arrows off), no viewmask marker (the proxy's
product rule clears Terrain's bit), the frame cap of the harness (30 fps). EXE SHA-256 04CFDC71... (the instance's install;
the Ghidra input copy differs as a file but is equal at every address below). Save `C5H-0169-4302-TINY_0009V`, a copy of
c5h-c's `C5H-3600-TINY_0009V` (SHA-256 27420a41..., turn 9, Tiny 56 x 36, wrapping in x, every plot revealed; CP/VP/EUI;
the instance's asset set, Vibrant Textures present, D94; no picture was taken). Reads by `shell/tools/shadow_probe.py`
(ReadProcessMemory by PID only, gt_reader's TimeDateStamp check) at seven camera steps and after one reload of the save:
33 reads in all; the composite's own list trace (trigger file `shell_composite.trace`, three map frames, Presents
1503-1505); table `shell/evidence/0169/4302/summary_run1.txt` (from `summarize_4302.py`); raw probe files and logs on
`F:/c5-64bit-work/0169/4302/run1/`. Static reading: 2903's llvm-objdump listing of the EXE (read only, no decompiler text
here, D83); the constants read from the EXE file by `exe_consts.py`.

**S1. The shadow pass runs every map frame on the product path: holds [L trace + C].** In each of the three traced map
frames the EXE executed, before the back-buffer clear the composite inserts after: one list that clears and binds a 2554 x
1411 R16G16B16A16_FLOAT target (ShadowRaw: the back buffer's size, format 10), 9 or 10 more lists that bind it (the caster
lists, recorded on the worker contexts), then exactly one list that binds a 2048 x 2048 R16_UNORM target (format 56:
ShadowBlur, the resample). The per-view mask GVS+0x1B4 read 0xFF8C0666 at every read: Terrain 0x1 clear (the proxy's rule),
Landmark 0x2, City 0x20, Unit 0x40 set, Particle 0x1000 clear. [C, 2026-11-15] The pass 0x523CA0 gates only on ShadowRaw,
the quality and those caster bits (section 9); **the TerrainSystem render that builds TerrainFrame (0x4241E0) and WaterFrame
(0x739AE0), 0x657070, and 0x6C3860 after it are called at 0x5895E5 / 0x5895EF only when Terrain's bit 0x1 is set**
(the test at 0x5895D6), so under D33 no EXE draw reads ShadowBlur, ShadowMatrix0 or the factor: the EXE draws the map
and nothing in the EXE samples it. *Not checked live: whether the frames with no camera change skip anything (all three
traced frames had the pass).*

**S2. Handles, view-table entry, texture: hold, with one difference from the overlay [L].** ShadowRaw `[0xDA12B8]` = 1754
(0x6DA), ShadowBlur `[0xDA12BC]` = 1755 (0x6DB): the same values in all 33 reads over seven minutes, at every camera and
after the reload; the view-table entries `0x414C348 + 12 h` and the view pointers did not change over the reload either
(the reload rebuilt no shadow target). **Each entry holds one view (entry [0]); [1] and [2] are null**, where the Terrain
Overlay's entry (handle 583 in this run) holds two. The view object's vtable is d3d11.dll+0x69F8, the same address as both
of the overlay's views (so the vtable alone does not tell an SRV from an RTV; the overlay tap's check against objects it
made itself stays the test). The texture records `0x4170B80 + 0x398 h`: ShadowBlur 2048 x 2048, 1 mip, format 56
(R16_UNORM), +0x29C = 0x800000 = 8,388,608 bytes; ShadowRaw 2554 x 1411, 1 mip, format 10, +0x29C = 28,829,552 bytes
(= 2554 x 1411 x 8: **+0x29C is the texture's byte size**). [I] The handles differ per run as the overlay's do (earlier
runs: 0x6B0/0x6B1, 0x6C1/0x6C2, 0x6D3/0x6D4, `shell/evidence/3000/*/handles_*.json`): read at start-up and after every load.

**The shadow setup 0x52FA70 makes ShadowBlur by quality [C, 2026-11-15].** With q = `[0xD90F9C]`:
- q = 3: ShadowBlur = a new 2048 x 2048 R16_UNORM target, 1 mip ("Shadow Texture Blur").
- q = 2: ShadowBlur = a new **1024 x 1024** R16_UNORM target.
- q = 1: no blur target: **ShadowBlur is set to ShadowRaw's handle** (0x52FC98), and the resample 0x5580B0 returns at
  once (it runs only at q >= 2), so the terrain samples the unblurred back-buffer-sized R16G16B16A16_FLOAT map.
- any other q (0): ShadowBlur = a 4 x 4 R8G8B8A8_UNORM texture filled with zeros (no shadow), and the pass returns at
  once (q = 0).
- ShadowRaw is always the back buffer's size, R16G16B16A16_FLOAT, 1 mip. When the setup runs again with a target already
  there, it compares the record (size, mips, format, byte size) and re-creates the texture **under the same handle**
  (0x8BF1B0) when it differs: a settings or window-size change can replace the texture without a new handle.
The instances' GraphicsSettingsDX11.ini has `TerrainShadowQuality = 3`, `ShadowLevel = 3`, `FadeShadows = 1`; the EXE
read q = 3 and the fade flag `[0xD90F80]` = 1 [L]. *(Corrected 2026-11-15 by lead-0169 from 4304 [L], 11.9: q is `ShadowLevel`, not TerrainShadowQuality; the guess below is kept as written.)* [I] q is TerrainShadowQuality and the flag FadeShadows (not traced to
the ini reader).

**S3. ShadowMatrix0 per view with wrap views: holds, exactly as the overlay's rule [L + C].** [C] The writer 0x615950
stores view 0's matrix at 0xDA1790 and, for 1 <= v < g_NumViews, ShadowMatrix0[v] = g_View[v] x g_InvView[0] x
ShadowMatrix0[0] at 0xDA17D0 + 0x40 (v - 1) (D3DX row vectors; g_View 0xDA1310, g_InvView 0xDA1410; no upper bound in the
EXE, the array ends at ShadowMatrix1 0xDA1890: four views). [L] Tiny's wrap period W = 3583 (grid block 0xD90FE4,
wrap_x 1). g_NumViews was 2 at every camera within about 1,760 units of the seam (the load camera at x = 1472, the plot
(0, 18) at x = -1761, the plot (55, 18) at x = +1761) at zooms 11.0, 14.26, 14.5 and 20.5 (the range is 11 .. 20.5), and 1
at the map's middle (x = 33); never more than 2 on Tiny. **View 1's g_View[1] x g_InvView[0] is a pure translation
(linear part within 6.0e-8 of identity) by (k W, 0, 0) with k = +1 when the camera is east of the map's centre and k = -1
when west** (shift 3582.99975, y and z below 2.1e-6). ShadowMatrix0[1] equals ShadowMatrix0[0] with row 3 moved by k W
times row 0 **within 2.2e-7 absolute (5.9e-8 relative to the matrix's largest element)**; rows 0-2 equal within 2.3e-10;
row 3's w came out 0.99999994 (float rounding of the product). So the reader's test "row 3 moved by k W times row 0" holds
with a tolerance of 1e-5 relative (two orders above what was seen), k in {-1, +1} on Tiny, and the scene's copy without
an EXE view takes view 0's matrix moved (marked derived) as 0312 says.

**S4. The factor and the quality: q = 3 makes the factor 1.0 at every zoom [L + C].** [L] `[0xD90FA8]` = 1.0, GVS+0x78C
= 1.0, the fade target `[0xB97A60]` = 1.0 at zooms 11.0, 14.26, 14.5 and 20.5 (the maximum), every read. `[0xD90FB0]`
was the camera's zoom and `[0xD90FB8]` = 20.5 = the zoom's maximum at every read (section 9's [I] confirmed [L]). [C] At
q > 2 the pass sets the factor to 1.0 every frame; only at q = 1 or 2 with the fade flag set does it ease toward 0 when
the zoom is above the maximum - 2 (18.5 here), 0.5 from maximum - 3 (17.5), else 1 (constants 2.0, 3.0, 0.5, 1.0, step
0.1, snap 0.01 read from the EXE file). **Below 0.001** (0x9EC9B8) it opens ShadowBlur in a "Clear" pass with the colour
(0, 0, 0, 0) and returns: no ShadowRaw clear, no casters, no resample: **ShadowBlur is all zero (= no shadow, since the
PS multiplies t3.r by Arg6)**. That case exists only at q 1-2 (at q 1 the cleared handle is ShadowRaw's). Not seen live
(the instances run q = 3; changing the quality would mean editing the instance's graphics settings, not done here).

**S5. Frame timing at the composite's Present: holds [C + L].** [L, lockstep-probe.md 2] UpdateViews, the
ExecuteCommandList calls and Present run on the EXE's main thread in that order. [C] The writers: ShadowMatrix0 only in
0x615950 (inside UpdateViews; every immediate store to 0xDA1790..0xDA18CF found is there, the others are reads by
`rep movs` sources), the factor only at 0x523D97 (the shadow pass); g_NumViews in UpdateViews (0x51DFFE ...). In stock the
TerrainFrame and WaterFrame builds that bind them run after the shadow pass in the same 3D render (0x589524 the pass, then
0x5895E5 / 0x5895EF), so the values at Present N are the values stock would bind in frame N. [L] Each traced frame
executes the resample list before Present; a CopyResource on the immediate context at Present is ordered after it on the
GPU [C, D3D11]. At a still camera the six ShadowMatrix0 reads of each step were byte-equal (one hash per step); a moving
camera was not read. *Not checked: stores through computed addresses (a pointer to 0xDA1790 passed elsewhere); the second
3D render 0x749500 (also calls the pass; one ShadowRaw clear and one resample per traced frame, so it did not run in
these frames).* Under D33 no EXE draw binds these values (S1), so a bind-time check like the overlay's
`overlay_verify` has no EXE bind to compare with: the producer's verify mode compares texels only.

**S6. The byte patterns [C, `shell/evidence/0169/4302/patterns_4302.txt`, 2026-11-15].** Each is equal in the Ghidra input
copy and the c5h-e install (SHA-256 prefixes of the ranges in the file; no bytes here, D83); the form of each check is as
`overlay-system.md` 11's:

| VA | Length | What it pins |
|---|---|---|
| 0x523CA6 | 7 | the pass returns when `[0xDA12B8]` (ShadowRaw) is 0 |
| 0x523CB4 | 5 | the pass reads the quality `[0xD90F9C]` |
| 0x523D97 | 8 | the store of the factor `[0xD90FA8]` (its only writer) |
| 0x523DD9 | 5 | the clear of `[0xDA12BC]` when the factor is below 0.001 |
| 0x523E2D | 6 | `[0xDA12B8]` opened as the casters' target |
| 0x523E51 | 7 | the Unit casters gated by the view-mask bit 0x40 (City 0x523E6B, Landmark 0x523E84 alike) |
| 0x523ED5 | 5 | the tail call to the resample 0x5580B0 |
| 0x5580B0 | 7 | the resample runs only at q >= 2 |
| 0x5580F4 | 5 | the resample's target `[0xDA12BC]` |
| 0x52FC4C | 20 | the setup: "Shadow Texture Blur" made as format 0x38 (R16_UNORM), size x size, stored to `[0xDA12BC]` |
| 0x52FC98 | 5 | the setup at q = 1: `[0xDA12BC]` := ShadowRaw's handle |
| 0x615B86 | 13 | ShadowMatrix0[0] written at 0xDA1790 (16 dwords), then the bound `g_NumViews` |
| 0x615BE2 | 6 | ShadowMatrix0[v] at 0xDA17D0 + 0x40 (v - 1) |
| 0x615BFA | 6 | the loop's bound v < `g_NumViews` |
| 0x4241E6 | 13 | the TerrainFrame build reads `g_NumViews` and the factor (Arg6) |
| 0x42456A | 6 | the TerrainFrame build reads ShadowMatrix0 at 0xDA1790 + 0x40 v |
| 0x73AB06, 0x73AB1B | 6, 8 | the WaterFrame build reads ShadowMatrix0 and the factor |
| 0x589524 | 5 | the 3D render calls the pass before the TerrainSystem render |

The producer needs the pass's handle uses (0x523CA6, 0x523E2D, 0x523DD9, 0x5580F4), the setup's store (0x52FC4C) and
q = 1 alias (0x52FC98), the writer (0x615B86, 0x615BE2, 0x615BFA) and the factor's store (0x523D97); the consumers
(0x4241E6, 0x42456A, 0x73AB06/1B) pin that stock binds these globals; plus the view-table write 0x8BC0CA that the overlay
and bake taps already check.

**S7. The copy's cost: small [I, from the overlay tap's numbers; not measured].** The copy is 8,388,608 bytes (R16, 1
mip) against the overlay's 22,369,620 (37.5 %). The overlay tap's CPU time in `CopyResource` + `End` per copy was 0.0013
to 0.0089 ms mean, 0.22 ms max (overlay-system.md 12.7, workstation, an indication, COMMON 23); the CPU call does not
depend on the size, so about the same is expected. The GPU work is 16.8 MB of traffic per map frame (read + write), about
0.02-0.03 ms at the RTX 4080's bandwidth [I]. Three slots: 3 x 8 MiB = 24 MiB of video memory on the EXE's device (the
overlay's three: 64 MiB). Measuring it needs the producer (no product code here).

**The RenderDoc arm [L, 2026-11-15 12:53-12:57 EST, c5h-e, the same prepared product path and save, run 2 of
`run_4302.py --rdc`; `launch_rdc_4302.py` preloads the harness's allow-listed RenderDoc 1.47 x86 `renderdoc.dll`].**
RenderDoc attaches to the product path's EXE, but **the composite then hooks nothing** ("target ... is in renderdoc.dll,
not in d3d11.dll/dxgi.dll - NOT hooked" for Present, ResizeBuffers, CreateSwapChain), so the capture holds the EXE's own
frame on the product path (proxy, server.exe running, D33, the proxy's rule: the mask read 0xFF8C0666 as in run 1)
without the server's picture: what S1 and S8 need. The view objects then carry renderdoc.dll vtables (as overlay-system.md
P6 found), so a producer's d3d11 vtable check refuses under RenderDoc (expected). Captures
`F:/c5-64bit-work/0169/4302/captures/c5h-e_20261115-125302_frame12141.rdc` (load camera, SHA-256 9b47599b...) and
`..._frame14322.rdc` (the seam, plot (0, 18); 15b94787...), converted by `cli.py rdc-summary` into
`F:/c5-64bit-work/0169/4302/analysis/frame*/` and read by 0190's `render/evidence/0190/shadow_draws_0190.py` and
`shell/evidence/0169/4302/casters_4302.py` (table `casters_4302.txt`). Game data stays on F: (D83, D141).
- **S1 confirmed by the capture [L]:** frame 12141: 634 draws in 45 executed lists; **115 draws into the 2554 x 1411
  R16G16B16A16_FLOAT ShadowRaw (no depth) on seven deferred contexts, then one draw (6 indices, one quad) that samples it
  as t0 and writes the 2048 x 2048 R16_UNORM ShadowBlur**; frame 14322: 558 draws, 73 caster draws and the one resample.
  **No draw of either frame reads ShadowBlur and no TerrainShader draw exists** (shadow_draws_0190's terrain count 0): the
  map is made every frame and nothing in the EXE uses it under D33.

**S8. The casters [L capture + mask, C code; the class of each shader pair I].** By the live mask and the pass's code the
EXE's map holds the shadows of Units (0x6D6830), Cities (0x63BBE0) and Landmarks (0x559DF0, then DynamicGeometry
0x674AD0: improvements, wonders, resources); Particle (0x4C0190, bit 0x1000) is off under D33 and no particle draw is in
either capture; trees are not casters (their shadows are terrain decals, section 9). The caster draws fall into five
vertex / pixel shader pairs (none in the material table, as in 0190's stock capture), in the frame's order:

| Draws (frame 12141 / 14322) | Shader pair (SHA-256 prefixes) | Shape | Class [I, from the pass's call order Unit, City, Landmark + DynamicGeometry and the shape] |
|---|---|---|---|
| 4 / 5 (first) | VS 8cb52b96, PS 72c27aea | 1,914-3,708 indices, **3-6 instances**, BC3 diffuse | units (one instance per formation member) |
| 17 / 30 | VS 88c2b7d4, PS 73daa800 | 102-894 indices, 1-9 instances, BC1/BC2 | cities (buildings) |
| 1 / 2 | VS 0a3f2230, PS e12e9eaf | 1,248-1,917 indices | a city or landmark mesh of its own vertex layout |
| 87 / 31 | VS 88c2b7d4, PS e12e9eaf | 3-4,692 indices, 1-3 instances, BC1/BC2 | landmarks: improvements, wonders, resources |
| 6 / 5 | VS 34b98e63, PS e12e9eaf | 510 indices each, 1 instance | DynamicGeometry (one repeated mesh) [I] |

The pixel shaders sample the casters' own BC1 / BC2 / BC3 textures and a 128 x 8 RGBA8 texture (0190: the fog ramp), as in
0190's stock capture. 9 or 10 caster lists per frame in the composite's trace (run 1) against 7 contexts here: a list per worker and
system. Naming each pair for certain needs the shaders' owners (the EXE's material table does not list them).

**What this means for 0312's producer and reader (changes to its plan named):**
1. **Description check by quality, not 2048 only (change).** Accept ShadowBlur 2048 x 2048 (q 3) and 1024 x 1024 (q 2),
   R16_UNORM, 1 mip; at q 1 the handle equals ShadowRaw's (back-buffer-sized R16G16B16A16_FLOAT, unblurred): either copy
   that (a fourth slot format; the terrain module's `Options::shadow` takes R16_UNORM only, 0190) or publish "no shadow"
   with a counted reason; at q 0 publish strength 0. Slots are made for the description seen and made again when it
   changes (the setup re-creates the texture under the same handle: re-check the record, +0x28C/+0x290/+0x294/+0x2B4/
   +0x29C, every copy, not only when the handle changes).
2. **One view in the entry (change).** The view-table entry of ShadowBlur holds only [0]; the producer takes [0] and must
   not require the overlay's second view.
3. **Strength.** At the instances' q 3 the factor is 1.0 every frame; the below-0.001 case (map cleared to 0, no casters)
   exists only at q 1-2: publish the cleared map with the strength, as planned.
4. **Wrap views.** k = +-1 on Tiny with two views; the reader's tolerance for "row 3 moved by k W times row 0" can be
   1e-5 relative (seen: 5.9e-8). The huge map's views (up to 4) were not read.
5. **Frame rule.** Present is the right point: texels, matrices and strength of frame N are all final there (S5). Under
   D33 no EXE draw binds them, so the verify mode checks texels (a second copy at the same Present) and cannot compare
   matrices with a bind as the overlay's does.
6. **Patterns:** the S6 list.

### 11.9 As built: the tap and the reader (task 4304, 2026-11-15 13:17-, worker 4304, branch `task-4304-shadow-tap`)

**Files** (C1 accepted, MC #1804: data reads and D3D copies only, guarded byte patterns, no EXE code called or written):
`shell/common/shell_shadow_tap.h` (layout), `shell/composite/shadow_tap.{h,cpp}` (producer in shell_composite.dll, wired in
`shell_composite.cpp` after the decal tap, key `shadow_tap`, **0 in the shipped ini until 4308 binds it**),
`server/src/render/srv_shadow_tap.{h,cpp}` (reader; build_server.py globs `server/src/render/*.cpp`, so server.exe compiles
it with no list change: there is no `server/CMakeLists.txt`), tests `shell/tests/test_shadow_tap.py`,
`server/tests/render/shadow/` (`build_shadow_tests.py`, `shadow_reader_tool.cpp`, `test_reader_shadow.py`), live driver
`shell/evidence/0169/4304/run_4304.py` with `summarize_4304.py` and `summary_live.txt`.

**Layout [C].** Its own section `Local\VPShellShadowTap.<exe pid>` of 4096 bytes (the main section is full, as 0166's
decal tap): control block `VPShellShadowTap` (320 bytes at 0: magic 'SHDW', layout version 1, header and slot sizes, pid,
state NONE / ACTIVE / FAILED / OFF, failReason, a 160-byte **failText** (the producer's reason in words, which the reader
quotes for D125), newestSlot, generation, quality, slotSet, copies, copy CPU; consumer inUseSlot / ackGeneration /
ackServerPid; counters missed, faults, descChanges, verifyCount, verifyDiffer) and three `VPShellShadowSlot` (368 bytes
each at 512: seqlock, valid, generation, numViews, presentCount, width / height / mips / format, quality, strength,
texEpoch, flags NO_TEXTURE / VERIFY, `shadowMatrix0[4][16]`, verifyHash / verifyBytes, loadSeq). Sizes equal on x86 and
x64 (static_asserts). **Four views** because the EXE's ShadowMatrix0 array runs from 0xDA1790 to ShadowMatrix1 at
0xDA1890 (S3): a g_NumViews above 4 would read past it and is refused. Slot textures are named
`Local\VPShellShadowTex.<pid>.<slot>.<texEpoch>`: the epoch in the name lets a new slot set (after a description
change) be made while the server still holds the old textures open.

**Producer [C, tests below].** 20 byte patterns before the first read: 4302's 19 (S6 table, lengths as there) and the
view-table write 0x8BC0CA the overlay tap also checks; a mismatch or an EXE the composite did not validate is FAILED
PATTERN at once with failText "pattern at VA 0x...... (what) does not match". At each map frame (camera OK and the
scene-clear list, as the overlay's) it reads, through SafeRead, the quality `[0xD90F9C]` (0..3, else QUALITY), the factor
`[0xD90FA8]`, g_NumViews (1..4, else VIEWS) and ShadowMatrix0[v]; at q 0 it publishes a slot with NO_TEXTURE and strength
0 at once (no copy, no texture made); at q 1..3 it reads `[0xDA12BC]` and `[0xDA12B8]` (q 1: they must be equal; q 2, 3:
they must differ: else HANDLE), takes the view-table entry's first D3D11 view (vtable against objects made here; [1]
also tried), its texture and **the texture's own D3D11 description, every map frame** (QI + GetResource + GetDesc:
it catches a texture re-created under the same handle, including at the same view address; the EXE's texture record
0x4170B80 + 0x398 h is not read, so no unpinned address is): q 3 2048 x 2048 R16_UNORM, q 2 1024 x 1024 R16_UNORM, q 1
R16G16B16A16_FLOAT at the composite's back-buffer size, 1 mip, 1 sample (else DESC). A new valid description makes the
slots again (counted in descChanges, new epoch and names); the frames already published stay with the reader, so a
triple never mixes descriptions. Then CopyResource into a free slot (never inUseSlot, never the newest) and an event
query; published at a later Present when the query has signalled. Before the install a problem counts towards
`shadow_fail_frames` (30) map frames, an access violation fails at once; after it, LOUD (a new text at once, the same text
every 1000th time) and counted, the last good frame stays. `shadow_verify=N`: every N-th copy also reads the EXE's texture
back in the same Present (staging copies of the source and of the slot): a byte compare on the EXE's device
("VERIFY copy ...: B bytes, D differ") and the FNV-1a 64 of the source rows in the slot (VERIFY flag), which the reader
compares with its own read of the shared texture on the server's device. Test modes `shadow_test=` (24, a synthetic
shadow state with real textures in the DLL) and `shadow_test_frame` / `shadow_test_ms` (when its event happens).

**Reader [C, tests below].** `srvrender::ShadowTapReader(exePid, hdr, dev)`; `acquire(ShadowFrame&)` each render-loop
iteration returns {srv (null at q 0), format, width, height, numViews, matrices[4][16], strength, presentCount,
generation, quality, slot}: texels, matrices and strength of one slot, the overlay's hand-over (inUseSlot before the
check, ack after). States Absent (no section), Off, Waiting, Active, Failed. **Failed is final and is the D125 reason**:
`installFailure()` and `lastWhy()` = "the shadow tap could not install: VPSHD_FAILED <NAME> (reason n: <failText>)", or
"the shadow tap's section is refused by the server: <layout difference>" (VERSION: magic, version, header or slot sizes,
pid). A slot is refused (LOUD once per text, counted, last triple kept) for numViews outside 1..4, a non-finite matrix
element, a strength outside [0, 1] or not finite, a quality above 3, a description that is not the quality's (q 0: no
texture and strength 0), presentCount 0, a shared texture that does not open or differs from the slot. A torn slot (seq
odd, valid 0, another generation) is never taken and not counted. Stale episodes in map frames as the overlay's (lag,
counted, never a fault). Verify: a VERIFY slot is read back and hashed: verifyOk / verifyBad.

**Tests [L, 2026-11-15, workstation, RTX 4080; builds: `shell/build.cmd` into C:/c5-64bit-work/0169-4304/shell-build,
the reader tool by `build_shadow_tests.py` with clang-cl /W3 /WX and with MSVC /fsanitize=address].**
`server/tests/render/shadow/test_reader_shadow.py` on crafted sections: 40 passed (every state; 7 layout refusals;
17 slot refusals; newest slot out of range; texture missing; 2 texture descriptions; 3 torn slots; the verify mismatch,
exit 7). `shell/tests/test_shadow_tap.py` on the stand-in: 35 tests (triples exact and consecutive at q 3 / 2 / 1 with the
known texel and the frame stamp in texels, matrix and strength; q 0; verify end to end at q 3 and q 1; 14 install
failures each with reason, failText, one LOUD line and the reader's Failed text; the section failure; the description
change q 3 -> q 2 (descChanges 1, slotSet 2, every triple of one description); the texture with a wrong description and
the texture gone in play; a NaN matrix and a strength of 1.5 refused by the reader; q 3 -> 0 in play; a slot in use never
written over 1.5 s; the kill switch as shipped and during copies; the map-frame gate; the real path refusing the
stand-in's EXE (failText naming VA 0x523CA6) and an unvalidated one; an unknown test mode; the 20 patterns against the EXE
on disk). Final runs (14:20-14:51 EST): reader 40 / 40 plain and 40 / 40 under ASan (0 reports); stand-in 35 / 35 plain
(12 min) and 35 / 35 with `shell\build.cmd asan` (0 reports); 0 skipped. Neighbours with the changed composite
(`test_build_cmd.py`, `test_errors.py`, `test_standin.py`): 67 / 68, the one failure `test_section_not_initialised`
(its 10 s wait for a log line under load) passed when run again alone; shadow_tap is 0 in those runs.

**Live on the product path [L, 2026-11-15 13:55-14:18 EST, workstation, instance c5h-e (F:)].** `run_4304.py` (3308's
product path as 4302's run: pinned layout `x64-04b1211b-pfdee656`, proxy 75C40CEC..., server 5B254430..., x64 DLL
29F58E17...; the composite of this branch 7A893035...; D33 by darkred.py's list with Overlay on (D93), no viewmask
marker; frame cap of the harness; EXE 04CFDC71...), save `C5H-0169-4304-TINY_0009V` (a copy of c5h-c's
`C5H-3600-TINY_0009V`, SHA-256 27420a41..., Tiny 56 x 36, turn 9; the instance's asset set with Vibrant Textures, D94; no
picture taken), composite keys `shadow_tap=1`, `shadow_verify=10`. Per arm: the reader tool beside the game for 25 s while
the camera moved (3 wheel ticks in, 6 out, 3 in, then UI.LookAt the seam plot (0, 18)) and 10 s at a still camera (the
map's middle); probes of the EXE by `shadow_probe.py` at still cameras, each followed at once by a read of the tap's
section. Table: `shell/evidence/0169/4304/summary_live.txt`; raw files on `F:/c5-64bit-work/0169/4304/<arm>/`.

| Arm (GraphicsSettingsDX11.ini) | q | ShadowBlur | copies / missed / faults | verify reads, 0 bytes differing (DLL) | reader triples (move + still), verify equal | strengths seen |
|---|---|---|---|---|---|---|
| as installed (ShadowLevel 3) + 1 reload | 3 | 2048^2 fmt 56 | 3,797 / 0 / 0 (3 in-flight lags at the stop) | 455 of 455 | 711 + 280, 105 of 105 | 1.0 |
| TerrainShadowQuality = 2 | **3** | 2048^2 fmt 56 | 2,280 / 5 (lags) / 0 | 299 of 299 | 706 + 284, 104 of 104 | 1.0 |
| ShadowLevel = 2 | 2 | 1024^2 fmt 56 | 2,291 / 0 / 0 | 299 of 299 | 751 + 300, 105 of 105 | **123 values 0 .. 1** |
| ShadowLevel = 1 | 1 | ShadowRaw's handle, 2554 x 1411 fmt 10 | 1,893 / 0 / 0 | 248 of 248 (28,829,552 bytes each) | 552 + 216, 86 of 86 | **112 values 0 .. 1** |
| ShadowLevel = 0 | 0 | 4 x 4 fmt 28 (ShadowRaw handle 0) | 2,420 published, no copy | none (no copy) | 751 + 301 | 0 |

In every arm: state ACTIVE, 0 refused, 0 stale episodes, 0 LOUD lines, generations and presentCounts only growing; at
every still-camera probe (3 or 4 per arm) the newest slot's ShadowMatrix0 for all views, numViews, quality and strength
were **bitwise equal** to the EXE's (max relative matrix difference 0.0); the reload (q 3) re-resolved the same handle
0x6DB and copies went on. Copy CPU in CopyResource + End per copy: mean 0.0013-0.0025 ms, max 0.011-0.284 ms per 1000
copies (an indication from the workstation, COMMON 23); slot memory on the EXE's device 24.0 MB (q 3), 6.0 MB (q 2),
82.5 MB (q 1). The first verify read cost 26-150 ms (staging textures made), later ones are not timed here.

**Findings that correct or settle section 11 (dated 2026-11-15, [L]):**
- **The quality `[0xD90F9C]` is `ShadowLevel`, not `TerrainShadowQuality`** (S2b's [I] guessed the latter): with
  TerrainShadowQuality = 2 the EXE kept q 3 and a 2048^2 ShadowBlur; ShadowLevel = 2 / 1 / 0 gave q 2 / 1 / 0. The
  lead's S2b line wants this correction (outside this subsection's grant).
- **S4's fade is live at q 2 and q 1**: while the camera zoomed out the factor eased through 123 (q 2) and 112 (q 1)
  values down to 0 and back, every one published with its frame's map (the map cleared to 0 when the factor is below
  0.001 was copied and verified like any other); at q 3 the factor stayed 1.0.
- **q 1 as S2b said**: ShadowBlur = ShadowRaw's handle 0x6DA, 2554 x 1411 R16G16B16A16_FLOAT, one view in its entry; **q 0**:
  ShadowRaw is not made (handle 0), ShadowBlur is a 4 x 4 R8G8B8A8_UNORM, the factor reads 0.0.

**For 4308 (the wiring) [I]:** construct `ShadowTapReader(exePid, hdr, renderDevice)` beside the overlay reader, call
`acquire` once per frame and bind the triple as one (terrain t3 + Arg6 + ShadowMatrix0 per wrap copy; water t13 +
WaterFrame 0x220 + ShadowMatrix0); `installFailure()` / `lastWhy()` is the D125 reason (an Absent section is not an
install failure by itself: the tap may be off in an older composite; 4308 decides its own timeout rule). At q 1 the SRV
is R16G16B16A16_FLOAT (the terrain module's `Options::shadow` takes R16_UNORM only since 0190: decision 2 of the C1 note
widens it; the PS reads .r); at q 0 the SRV is null and the strength 0 (stock samples a zero texture: bind the module's
"no shadow" path). Turn `shadow_tap=1` in the shipped ini with the binding.

**Not verified:** the huge save (3-4 views; not run, time *(2026-11-15, 4311, 13.7: the huge map never has more than 2 views [L].)*); the reader inside server.exe itself (the live arms ran the
same source as the test tool beside the game; server.exe compiles it but nothing calls it before 4308); a quality
change in play through the game's options screen (the stand-in's description-change test stands for it); RenderDoc
(the d3d11 vtable check refuses its wrapped views, as section 11's RenderDoc arm found: expected, not run).


### 11.10 In the scene: the triple bound into the server's terrain and water (task 4308, 2026-11-15 14:58-, worker 4308, branch `task-4308-scene-wiring`)

**Files** [C]: `server/src/render/srv_render_scene.{h,cpp}` (ShadowSource, MakeTapShadowSource, ShadowCopiesFromFrame,
WaterSceneShadow, ShadowFatalStep; Scene::shadowPoll, the binding in Scene::draw, the "shadow 1s:" line and the totals),
`server/src/render/srv_water_scene.cpp` (t13, 0x220, ShadowMatrix0 per view from WaterSceneShadow), `srv_render_host.cpp`
(render_shadow, render_test_shadow_wait_ms, the fatal step, the recorder hook), `render/src/terrain/terrain.cpp`
(Options::shadow accepts R16G16B16A16_FLOAT), `render/src/water/water.cpp` + `water.h` (FrameInputs::haveShadowMatrix0Wrap /
shadowMatrix0Wrap), the shipped `shell_composite.ini` `shadow_tap=1`; tests `server/tests/render/shadow/test_scene_shadow.{cpp,py}`
(+ `build_scene_shadow.py`), `render/tests/terrain/terrain_test.cpp` (`shadow_q1_float_map_reads_red`),
`render/tests/water/water_test.cpp` (`renderer_shadow_matrices_per_view_and_t13`).

**What the scene does [C, tests below].**
- The reader is made per EXE pid (the field tap's block, else the overlay tap's, as the decals') on the scene's device, polled
  once per loop iteration (`acquire`); a new generation, slot or view asks for a redraw. The triple is bound as one: terrain
  `Options::shadow` = {srv, strength, matrix[3]} with the matrices of `ShadowCopiesFromFrame`, water t13 = the same srv,
  WaterFrame 0x220 = the same strength, ShadowMatrix0 of each water view = the same three matrices (view 0 = copy 0, the views
  at -W / +W = copies -1 / +1, as the overlay's OverlayProj). Nothing of two slots is ever mixed: the bound frame is the one
  `acquire` returned in the same iteration.
- **ShadowCopiesFromFrame** (the overlay's rule with S3's test): copy 0 = view 0; a view v > 0 is copy k when rows 0-2 equal
  view 0's and row 3 = view 0's row 3 + k W x row 0, k = +-1, W = the camera block's wrap period (0xD90FE4), each within
  1e-5 of the matrix's largest element (S3 saw 5.9e-8); the EXE's own bytes are bound; a copy no EXE view shows gets view 0's
  moved (counted `derived`); a view that fits no copy is counted (`unmatched`, LOUD once per text). Refused (frame drawn
  without the shadow, LOUD, `bad`): numViews 0 or above 4, a matrix not finite (the reader refuses those first).
- **Quality 1:** the reader's srv is R16G16B16A16_FLOAT; the terrain module now accepts it (it read .r already: the PS samples
  t3.r) and the water's t13 takes any view. **Quality 0** (srv null, strength 0): the terrain's no-shadow path (Arg6 0, t3
  white) and the water's own zero t13 with 0x220 = 0: the picture of "no shadow" bit for bit (test below).
- **Options:** server.cfg `render_shadow=1` (default) / `0` (no reader: the picture before 4308, bit for bit);
  `render_test_shadow_wait_ms` (default 20000; 0 = no bound). Refused values end the parse (as every render_* key).
- **Failures (D118, D125, D132):** reader state Failed (`installFailure()`, `lastWhy()` quoted) -> LOUD once,
  `Scene::shadowFailed`, the host's `ShadowFatalStep` calls `HostConfig::fatal(5, why)` once. **Absent** (no section) is an
  install failure only after `render_test_shadow_wait_ms` from the first field with a valid camera grid (the decals' rule;
  4304 left the timeout to 4308). OFF (the composite's kill switch) is LOUD once and draws no shadow, never fatal. In play: a
  refused or torn slot keeps the last triple (the reader's contract; its own LOUD line), counted in `refused`; never fatal.
- **Lines:** the init line names render_shadow and the bound; once a second `shadow 1s: shadow=<state> bound=<frames drawn
  with a map> q=<quality> strength=<..> views=<n> fmt=<56|10> gen=<..> triples=.. refused=.. stale=.. derived=.. unmatched=..
  bad=.. frames_bound=.. frames_q0=.. frames_none=.. failed=0|1 | ice=...` (its own line, as the fog's); the totals line gains
  "shadow totals".

**Tests [L, 2026-11-15, workstation; WARP for the scene cases, the RTX 4080 for the module suites; EXE the Ghidra copy,
install `pure_vp` (VT present)].** `test_scene_shadow.exe` (clang-cl /W3 /WX and MSVC /fsanitize=address): shadow_copies
(every rule and refusal, 4302's float noise accepted, a 10 % different light unmatched), shadow_none_is_todays_picture
(render_shadow=0, a waiting source and a quality-0 triple: three pictures bit-equal), shadow_qualities_bound (q3 R16, q2
R16, q1 float: srv, strength, the three copy matrices and the generation are each triple's own), shadow_refused_keeps_last_good,
shadow_failed_is_fatal_once (fatal once with code 5 and the reader's text; no hook: a note only), shadow_off_loud_once,
shadow_absent_bound (the real reader on a pid without a section: failed after 200 ms naming the bound), options (render_shadow /
render_ice 0, 1, unset accepted; 2, yes refused; the wait keys' range) - with the ice cases and the bundle round trip (polar-ice.md 10) **13 of 13 plain and 13 of 13 under MSVC ASan (0 reports)**, final run 16:24-16:26 EST. Beside them (16:26-16:38 EST, this branch): the overlay scene cases and the fog scene cases all PASS, the scenebundle suite 4 of 4 programs (413 + 138 + 137 + 24 checks, 0 skipped, C5R_EXE set), render/ `c5r_terrain|c5r_water|c5r_seamg` 12 of 12 plain and terrain / water / seamg 4 of 4 under ASan (0 reports), `shell/tests/test_shadow_tap.py` kill-switch tests 2 of 2 (the shipped ini now `shadow_tap=1`).
`c5r_terrain_test` 49 of 49 with `shadow_q1_float_map_reads_red` (the float map r = 1 against an R16 map of ones: 0 bytes
differ; r = 0 against zeros: 0; ones against zeros 57,600 bytes differ). `c5r_water_water` with
`renderer_shadow_matrices_per_view_and_t13` (0x1A0 per view = the wrap matrices, 0x220 = the strength; zeros as t13 = the own
input bit for bit; the float map of r = 1 = the R16 map of ones; the flat down-looking test frame shows no darkening at all,
which is why that difference is reported, not asserted).

**Live on the product path [L, 2026-11-15 15:56-16:21 EST, workstation, instance c5h-e (F:)].** Driver
`shell/evidence/0169/4308/run_4308.py` (4304's and 3308's prep / run / restore), numbers in
`shell/evidence/0169/4308/summary_live.txt`. 32-bit EXE 04CFDC71..., pinned x64 DLL 29F58E17..., server.exe and proxy built from
this branch (release, on a git archive of the fork at 04b1211b8), composite of this branch 3777905D...; D33 + Overlay (D93),
no viewmask marker, `shadow_tap=1`, server.cfg defaults (`render_shadow=1`, `render_ice=1`); save `C5H-0169-4308-TINY_0009V`
(copy of c5h-c's `C5H-3600-TINY_0009V`, 27420a41..., Tiny 9V), ShadowLevel 3, Vibrant Textures present (D94). Three runs:
- **Every drawn frame binds the triple once the reader is ACTIVE**: frames_bound 3234 / 2974 / 3039, frames_q0 0,
  frames_none 0; q 3, 2048 x 2048 R16_UNORM, 2 views at the load camera (one wrap copy from the EXE's view, the other derived
  from view 0: `derived` = frames), unmatched 0, bad 0, refused 0, stale 0, failed 0; the 1 s line's `bound=` = the frames
  drawn that second (26-35 at the harness's 30 fps cap). **0 LOUD lines** in render.log, server.log, vpproxy.log and the
  composite log apart from the test keys' own (runs 2, 3). Composite: 20 patterns match, INSTALLED, 3 slots 24.0 MB, missed 4 /
  0 / 0 (lags at the start, D132), faults 0, CopyResource + End mean 0.0015-0.0025 ms (an indication, COMMON 23).
- **Pictures (DIAGNOSTIC, D112; popups none, units asleep as in 4309's stock arm, D114)** at 4309's cameras, against 4309's
  stock session 1 (whole frame, no mask): the capital (51,14) with its warriors, buildings and improvements: the models'
  shadows darken the server's ground under them as in stock (`vc-20261115-160106-workstation-ab6d`,
  `vc-20261115-161156-workstation-2cd5`; median 16, p95 91, 51.5 % within 16, the differences dominated by the forests that
  D33 does not draw). Per-class figures are 4309's tool's (done C).
- **The bundle round trip live**: run 3 recorded 8 frames with `render_test_record` (the recorder's shadow.bin, 8
  `shadow/*.shtx`, the ice mesh) and sceneshot played them: no MISMATCH (a triple bound, a map bound, the ice drawn in every
  frame as recorded), depth equal; colour differs in 17.8 % of the pixels, all in the **decal tiles: a gap of the harness, the
  bundle has no decal input and its render_config no render_decals** (0166's decals are drawn live, not replayed; for 0358 /
  the lead) [L]. Without the shadow members (`--set render_shadow=0`) 18,788 more pixels differ (the shadows), with them 192
  differ that did not (inside decal tiles) [L].
- *Found live and fixed (b67fc678):* `scenebundle::Writer::appendStart` accepted only feed / frames / cameras as streams;
  shadow.bin was refused ("not a stream member kind: shadow") and run 2's bundle had a record error. The round-trip case now
  writes the three members through the Writer.
- **Not verified:** qualities 2, 1, 0 live (the scene cases bind them; 4304 showed the tap at each); the huge save (3-4 views *(2026-11-15, 4311, 13.7: the huge map never has more than 2 views [L].)*);
  a quality change in play; a frame with a refused slot live (none occurred); the water's darkening under a model's shadow
  on the ocean (no caster over open water in these cameras).


### 11.11 Integrated: the shadow on the team branch built whole, live on the product build (task 4310, 2026-11-15 16:44-, worker 4310, branch `task-4310-integration`)

The team branch at 4379b668 (4304's tap and reader, 4305's styles, 4308's scene wiring with `shadow_tap=1` shipped, 4307's
ice tap and reader) built whole without a fix (server release, shell plain and ASan, render 32/32 plain and ASan; the
suites in `polar-ice.md` 11.3). The named product build for wave 5's acceptance is `polar-ice.md` 11.4
(`C:/c5-64bit-work/0169-build/x64-04b1211b-p0b0dc14df-0169`, manifest `shell/evidence/0169/4310/product_build_manifest.json`).

**Live [L, 2026-11-15 17:11-17:15 EST, c5h-e]** (`shell/evidence/0169/4310/summary_live.txt`; the run of `polar-ice.md`
11.2: that product build, D33 + Overlay, server.cfg defaults, no test key, save `C5H-0169-4310-TINY_0009V`, ShadowLevel 3,
VT present): the shadow tap's reader NONE -> WAITING -> ACTIVE; in **every** ACTIVE `shadow 1s:` line (84 of 120)
`frames_bound` = `frames` (last 2526 = 2526), `frames_none` 0, `frames_q0` 0, q 3 (2048^2 R16_UNORM), 2 views (one derived
from view 0, `derived` 2526), refused 0, stale 0, unmatched 0, bad 0, failed 0; the ice drawn in the same 2526 frames;
**0 LOUD lines**. The 1 s terrain line: state OK, draw_err 0. Picture: the pole, `polar-ice.md` 11.2 (the capital's
shadows were 4308's, 11.10, not repeated).

## 12. The colour inputs of the server's terrain and water against stock (epic 0169, task 4303, 2026-11-15)

Written 2026-11-15 12:31-12:52 EST by worker 4303 (lead-0169), branch `task-4303-colour-audit` from `main` fc8fc964.
Offline only (no game instance): the install read through `c5r::gamefs`'s lookup (`fpkscan lookup`), 3601's stock
capture exports (`D:/c5-64bit-work/3600/captures/cam{1_frame309938,2_frame265572}`, Tiny `C5H-3600-TINY_0009V`, 32-bit
pin `i0ship-26234371-ia`, RTX 4080), 3401's bake reference set (`D:/c5-64bit-work/3400/ref`, arm B) with 3406's live art
types (`D:/c5-64bit-work/3400/w6/live/{tiny,huge}_{art,tiles}.txt`), and the earlier tickets' results named per row.
Builds: `render/build.py --target bakecompare|fpkscan|shaderdump` (Release, clang-cl) of this branch. Asset set (D94): the
workstation's `pure_vp` install and the nine c5h instances, VT present, folder hash 45b3b433... Scripts and their
outputs' paths: `render/evidence/0169/4303/` (`styles_4303.py`, `run_styles.sh`, `water_inputs_4303.py`,
`run_bake_styles.sh`, `style_cost_4303.py`, `run_style_aggregate.sh`, `cubes_4303.py`, `signed_4303.py`); outputs (game
derived numbers and diff images) in `F:/c5-64bit-work/0169/4303/` (D141). No product code changed.

**In one screen.** Every colour input the server binds from the install is byte-equal (texel-equal where marked) to the
stock capture's: the fog ramp, the spec screen map, the TerrainFOW and TerrainShadow volumes, the Terrain ColorKey, the
Generic Sunlight / Shadowlight cubes, and every water texture (ocean t6, t8, t11; river t5, t6, t8, t11, t12). On every
install the project tests on, **the style changes only the bake's lighting cubes**: all five Terrain, TerrainFOW,
TerrainShadow and Buildings ColorKey volumes are equal; Sunlight differs only for Africa, Shadowlight for all four
continent styles. So 0341 (every tile Generic in the product) costs nothing in the terrain draw and costs only through
the bake's t0 (never t8): Africa tiles p95 12, max 72, signed +2.4 / +2.5 / -1.1 (server brighter, less blue); the other
three styles p95 0, p99 6-9, max 50-71. At 3019's two cameras 116 of 150 and 55 of 58 drawn tiles are Asia. No light term
enters the terrain draw per frame; the water's light and colour constants are the EXE's except a 1-ulp fog-colour default.
What is left of 3019's reference: list 12.5 (*corrected 2026-11-15 by 4315 as 12.6's last paragraph says; was "list
12.6"*). *2026-11-15, epic 0169: "0341 (every tile Generic in the product)" describes the product before 4305; the
product bakes and draws each tile's style since then (12.6).*

### 12.1 A1: each input, the server's bytes against stock

| Input | Server source | Stock resource (capture, id) | Equal | Hash (SHA-256 prefix unless marked) |
|---|---|---|---|---|
| Terrain t2 fog ramp | `fogramp.dds` (MiscTextures.fpk), `terrain.cpp` init | 3601 cam1/cam2: terrain t2 (0188); water t8 res 2416, 5,500 B all 8 mips | **equal** [L]: the capture's bytes = the file's payload with B and R swapped, every byte, all mips (this task, water t8 of 8 ocean and 2 river draws; 0188 for the terrain draw) | capture f9ed79fe...; file payload 974109fa... |
| Terrain t6 TerrainFOW volume, per style | `TerrainFOW_<Style>_ColorKey.tga` by the bake's TGA rule; product: style 0 everywhere (0341) (*2026-11-15: each tile's style since 4305, 12.6; was the product at fc8fc964*) | 3951 cam1_a: 2473 (Generic), 2476 (America); 3601: water t11 res 2434 | **equal** [L]: all five files give one volume, = 2473 / 2476 bytes (3953, `fog-of-war.md` 7.4) and = 3601's t11 in all 10 water draws (this task) | volume 49245115... (the capture's file name carries it), FNV-1a 64 6e4482dc769df29e by `styles_4303.py` (*7.4's FNV 0f547b99... is another seed or byte order; the SHA-256 is the cross-check*) |
| Terrain t7 TerrainShadow volume | `TerrainShadow_Generic_ColorKey.tga` only (*2026-11-15: the five `TerrainShadow_<Style>` files, the tile style's bound, since 4305, 12.6; "loading Generic only is exact" holds for the installs read, not for an install whose files differ*) | 3601 cam1/cam2: 2449, 2455 (two styles, same bytes) | **equal** [L] (0190, `terrain-surface.md` 9); all five files give the same volume (this task), so loading Generic only is exact | volume b68239d5..., FNV-1a 64 df1432ef669171e5 (= 0190's) |
| Terrain t9 spec screen map | `specscreenmap.dds` (TerrainTextures.fpk) | 3601 cam1/cam2: res 43161, BC3 1024, 11 mips | **equal** [L] (0189) | payload 9f4ca750... |
| Terrain t5 FOW cloud base | `FogCloudTile_dif_02.dds` (EffectTextures.fpk), 3955 | 3951: t5 | mip 0 equal [L] (3957, `fog-of-war.md` 2.4); lower mips not compared | payload 676602f7... |
| Terrain t1, t3, t4 | the EXE's overlay target (tap, 3704), the EXE's ShadowBlur (tap 0312, not yet in the product; *2026-11-15: in the product since 4304 / 4308, 11.9-11.11*), 3952's mask | - | the EXE's own targets by construction (t1, t3); t4 99.99 % of texels equal (7.1) | - |
| Bake t4 Sunlight cube, per style | `Sunlight_<Style>_Terrain.dds` (`bake::loadStyleInputs`) | 3403's tile P (`ref380`): Generic | Generic **equal**, 48 of 48 subresources [L] (5.2). Other styles: no capture of their cubes; the server's bake of non-Generic tiles is **bit-equal to the EXE's caches** on decal-free tiles of every style (America 92, Asia 112, Africa 74, Europe 166 tiles, 12.3) [L by consequence] | Generic / America / Asia / Europe payload 47b6b412..., Africa 3e2ce30d... |
| Bake t5 Shadowlight cube, per style | `Shadowlight_<Style>_Terrain(_2).dds` | tile P: Generic | as Sunlight [L] | Generic ffed2301..., America 704e14c5..., Asia 02fa397b..., Africa 8541e319..., Europe cf3b51be... |
| Bake t8 Terrain ColorKey, per style | `Terrain_<Style>_ColorKey.tga` | tile P: the Terrain LUT | **equal** byte for byte, alpha included [L] (5.2); all five styles one volume (this task) | volume a62b4351... (FNV-1a 64 b216e149cff96325); also = every Buildings volume |
| Bake t9-t11 (FOW mask, TerrainFOW LUT, spec map) | not bound | bound in stock, **not read** by CacheBake's PS [C] (`terrain-bake-passes.md` 3.3) | no effect | - |
| Bake constants (LightDir, AuxDir, HalfVector) | `stockBakeLight`, the EXE's float bits | tile P's TerrainFrame | **bit-equal** [C + L] (`terrain-bake-passes.md` 5.3) | - |
| Water t6 ocean WaterColor | `waterdepthcolor_new.dds` (**VT**: `Assets/DLC/VT/Water/`, shadows Upgrade1Textures.fpk's) | 3601: res 13367, 256 x 4, 9 mips, SRGB view | **equal** in texel values [L]: the capture's 5,628 B = the VT file's payload with B and R swapped (8 ocean draws, both cameras). The server uploads A8R8G8B8 as B8G8R8A8 without the swap (`dds.h`), so its memory bytes differ in order, not in value [C] | capture 11f3cd76...; file 24f64f72... |
| Water t8 WaterFogRamp | `fogramp.dds` | res 2416 | **equal** (as terrain t2) [L] | f9ed79fe... |
| Water t11 TransFOW | TerrainFOW Generic volume (3609) | res 2434 | **equal** byte for byte [L] (this task, 10 draws) | 49245115... |
| Water t12 river ColorKey | `Rivers_Generic_ColorKey.tga` (rule) | river draws 0356, 0218: res d77ead2b... | **equal** [L] (3605, this task) | volume d77ead2b..., FNV-1a 64 73969e1d4303803d |
| River t5 / t6 | `river_alpha_new.dds` (Upgrade1Textures.fpk) / `river_diffuse_new.dds` (**VT**) | 0356, 0218 | **equal** [L] (3605; this task: the capture = payload with B and R swapped) | 5a77e3fc... / e7084d61... (capture) |
| Water t0 / t1 LEAN, t5 currents, `watersky.dds` fields | 3602's loader and currents map; WaterFrame from the fields | 3601 | the replay with the module's inputs: 99.78-100 % of each ocean draw's pixels equal (3609, `water-system.md` 10.7); WaterFrame 19-20 of 22 members bit-equal (10.6) | watersky payload 2f8f90e7... |

Command (A1, A2): `run_styles.sh` and `water_inputs_4303.py --fpkscan <fpkscan.exe> --install <pure_vp> <export> ...`;
result `F:/c5-64bit-work/0169/4303/{styles.json,water_inputs.txt}`. [L] here means the capture's bytes, taken live by 3601,
compared offline; the server's own upload of each file is the module's tested path (0188, 0189, 0190, 3605, 3609, 3953).

### 12.2 A2: which files differ by style, per install, and overrides

`styles_4303.py` over `pure_vp` and the instances c5h-a, -b, -c (`%USERPROFILE%/c5-instances`) and c5h-d to -i
(`F:/c5-instances`), each with its own `MODS` folder as `--mods` (the VP and CP mods, EUI compatibility, the Hormigas
map pack), resolving every name by the game's lookup rule (`asset-pipeline.md` 1.2) [L, 2026-11-15 12:36]:

- **All ten installs are equal** in every one of the 37 files (25 ColorKey names, 10 lighting cubes, fog ramp, spec map,
  waterdepthcolor, watersky, the FOW cloud tile). oldpc's and the laptop's test bases (`tb1`, `tb2`, `pure_vp`) are copies
  of the workstation's `pure_vp` with the same VT hash (`asset-pipeline.md` 1.7) [I: their file lists are not on hand; the
  laptop's Steam game has no VT and was not read].
- ColorKey files: 21 of the 25 names exist (`Assets/ColorKeys`, loose, base layer); **`Rivers_{America,Asia,Africa,
  Europe}_ColorKey.tga` are missing**, so those slots keep the identity volume in the EXE and in the server alike (the river
  draw uses Generic, `water-system.md` 11.2). The five Terrain, five TerrainFOW, five TerrainShadow and five Buildings files
  each give **one volume per type**; Terrain and Buildings give the same volume. *Note:* `Terrain_Africa` and
  `Terrain_Europe` differ from the other three as files (SHA-256 6fb58875... against baf5e23a...) but only after the first
  32,768 pixels the loader copies, so the volumes are equal.
- Lighting cubes: **Sunlight differs from Generic only for Africa**; **Shadowlight differs for America, Asia, Africa and
  Europe** (each its own file; the three `_2` files come from Upgrade1Textures.fpk). Mean colour of mip 0 over the six faces
  (`cubes_4303.py`, 8-bit): Sunlight Generic (98.3, 98.2, 98.3), Africa - Generic (-3.8, -3.8, +5.9); Shadowlight Generic
  (77.5, 95.5, 113.7), America (+13.3, -8.8, -14.5), Asia (+18.9, -4.5, -15.5), Africa (+9.9, -0.2, -4.7), Europe (-13.7,
  -15.4, -12.0).
- **Overrides:** no mod in any instance's `MODS` and nothing in `pure_vp`'s `MP_AUTOPLAY_VP_5_4_7` modpack carries any of
  these names. VT overrides exactly two of them: `waterdepthcolor_new.dds` (VT `Water/`, over Upgrade1Textures.fpk) and
  `river_diffuse_new.dds`; both are what stock binds (12.1). No ColorKey, cube, fog ramp or spec map is overridden.
- So on these installs the only colour input that depends on the tile style is the bake's pair of cubes; the terrain
  draw's t6 / t7 and the bake's ColorKey are style-free. An install or mod with different per-style ColorKeys would change
  that (0341's note); none exists on the machines.

### 12.3 A3: the tile style and what drawing a styled tile as Generic costs (sizes 0341)

Rule [L, 3406]: style = lowest set bit 1..4 of tile+0x138 = the lowest nonzero continent art type of the tile's plots
(`bake::tileStyle`; corrected 2026-11-16 by 0383: four plots plus a fifth plot of column tx - 1 for 1 <= tx <= (ty + 1)
/ 2, x not wrapped, exact for all 10,752 huge and 2,240 Tiny tiles, `terrain-bake-passes.md` 5.3; was "4-plot rule,
exact for 10,748 of 10,752 huge tiles"). Tiles per style, the EXE's tile+0x138 read live by 3406:

| Map (save) | Tiles | Generic | America | Asia | Africa | Europe |
|---|---|---|---|---|---|---|
| Tiny `c5h-9028-tiny-t0008` (b2ceb663...) = the library's Tiny (`offline-scene-harness.md` 18) and `C5H-3600-TINY_0008`, the save 3601's / 3019's 9V was made from [I: 9V = that map at turn 9, every plot revealed] | 2,240 | 1,551 | 353 | 336 | 0 | 0 |
| huge `C5H-VP8P-HUGE_0350` = the library's huge (c499d3c6...) | 10,752 | 7,531 | 151 | 111 | 1,509 | 1,450 |
| Tiny T0018 map (3951's captures; the old 4-plot rule with wrap over Lua art types, `fog-of-war.md` 8.2; not recounted with 0383's rule) | 2,128 | 1,425 | 388 | 37 | 210 | 68 |

The bundles carry no art types (0341: none in the feed), so these counts come from the live reads above, not the bundles.

**Terrain draw:** t6 and t7 are the same bytes for every style (12.2), so a styled tile drawn as Generic is **bit-equal**
[L by the bytes]. **Bake:** `bakecompare` on 3401's caches (arm B), every cached tile, once with the art types (the rule;
reproduces run 3 of `terrain-bake-comparison.md` 5 exactly: Tiny blocks 0.9185, huge 0.8494) and once with every tile
Generic (`run_bake_styles.sh`, 2026-11-15 12:37-12:39): Tiny blocks equal mip 0 0.9185 -> 0.9111, huge 0.8494 -> 0.7976;
fair set 0.9907 -> 0.9867 and 0.9522 -> 0.9106. To isolate the style from decals, the tiles S = styled tiles whose t0 and
t8 are bit-equal to the EXE under the rule, baked Generic (`run_style_aggregate.sh`; decoded mip 0, per texel largest
channel difference, server Generic vs EXE = vs the server's own styled bake):

| Save, style (S tiles) | t0 bit-equal as Generic | t0 texels equal | median / p95 / p99 / max | within 16 | signed R/G/B (server - EXE) |
|---|---|---|---|---|---|
| Tiny, America (89) | 52 / 89 | 98.15 % | 0 / 0 / 6 / 63 | 99.92 % | - |
| Tiny, Asia (112) | 78 / 112 | 97.45 % | 0 / 0 / 9 / 50 | 99.53 % | - |
| huge, America (3) | 2 / 3 | 99.75 % | 0 / 0 / 0 / 20 | 99.99 % | - |
| **huge, Africa (74)** | **0 / 74** | **17.6 %** | **4 / 12 / 20 / 72** | **96.97 %** | **+2.42 / +2.52 / -1.10** |
| huge, Europe (166) | 107 / 166 | 96.89 % | 0 / 0 / 9 / 71 | 99.58 % | - |
| Tiny Asia tiles drawn at 3019's camera 1 (37 of 116) | 27 / 37 | 99.09 % | 0 / 0 / 0 / 49 | 99.94 % | -0.20 / +0.04 / +0.08 (the 10 differing) |
| Tiny Asia tiles drawn at 3019's camera 2 (7 of 55) | 1 / 7 | 81.74 % | 0 / 13 / 19 / 50 | 96.60 % | -1.73 / +0.45 / +0.80 (the 6 differing) |

- **t8 never changes** with the style (every S tile's t8 stays bit-equal as Generic): CacheBake's spec target uses only
  HalfVector and the spec layer, not the cubes [L, matches the PS in `terrain-bake-passes.md` 3.3].
- Africa costs on every texel (its Sunlight cube differs: the lit colour of all ground); the other styles only where the
  Shadowlight term weighs (slopes in macro shadow), so most of their tiles stay bit-equal. Huge Asia has no cached tile
  whose t0 is bit-equal under the rule (22 cached tiles), so Tiny's Asia stands for it.
- The signed Africa offset has the direction of 3019's unexplained huge offset (3.2: joined brighter R/G by +4.3 to +5.7,
  stock bluer) and about half its size; the clean plots of `v1_city_default` (plot (100, 30)) lie in a block of 288 of 357
  Africa plots (art types of `huge_art.txt`, x 90-110, y 22-38). [I] So 0341 explains about half of that offset at cache
  level; the rest is the distance fog missing at 3608's time (fixed by 0188 since) and not separated. Frame level not
  measured (no replay with substituted caches; a capture of the huge save is not possible, `renderdoc-capture.md` 4).
- Numbers are cache level (decoded BC1 mip 0), not frame level. Outputs `F:/c5-64bit-work/0169/4303/bake/{rule,generic,agg,
  agg_cam,agg_diff}/`, `style_cost.json`.
- Pictures (DIAGNOSTIC, D112: offline bake caches, not game pictures), Visual Checks issue 4303:
  `vc-20261115-125157-workstation-6c40` (huge Africa tile (106, 24): Generic | EXE | |d| x 8) and
  `vc-20261115-125158-workstation-5d4c` (Tiny Asia tile (48, 14) at 3019's camera 2).

**For 0341:** the cost is real but small except on Africa ground (p95 12, 3 % of texels beyond 16, a warm shift of
2.5 levels in R/G); on the huge library save 1,509 of 10,752 tiles are Africa. 0341's estimate (2-4 h) stands; its
acceptance can use `bakecompare` with and without `--plot-art` on 3401's set (this section's runner), offline.
*Done 2026-11-15 by 4305 (12.6: offline acceptance per style, live styles equal to the EXE's on 2,237 of 2,240 Tiny
tiles; on huge 10,748 of 10,752, 13.7). At frame level bare Africa ground on huge is at stock's ceiling (13.6: pooled
median 0, 95.8 % within 16 against stock's 92.6 %).* *The last 3 Tiny and 4 huge tiles: done 2026-11-16 by 0383 (12.7).*

### 12.4 A4: lighting

- **The terrain draw has no per-frame light term [C].** The registers of TerrainFrame (cb1) read by the draw's programs
  (`shaderdump --va`, this task): VS 0x067D6490 cb1[16].z, [22].xy; HS 0x067F98F0 [14].x; DS 0x067FA080 [0..13], [16];
  PS pass 0 0x067E6E58 and pass 1 0x067E8D68 [19] (CameraPos: distance fog only, 6), [25] Arg6, [27] FlagB (pass 0),
  [28] FlagC, [31..34] ShadowMatrix0. None reads AuxDir [17], LightDir [18], CameraAxisZ [20] or HalfVector [21]. All
  lighting reaches the picture through CacheBake's t0 / t8 (stock bake constants, 5.3), the static screen-space spec map
  t9 (pass 0), the model shadow (t3 x Arg6 through t7) and the fog; the module sets each as the EXE (6, 8, 9).
- **The water's light constants are the EXE's** (WaterFrame from the stock `watersky.dds` fields): 19-20 of 22 members
  bit-equal at 3601's cameras; the two normalised watersky vectors (+0x9C, +0xB8) are 1 ulp off in two components (D3DX's
  normalise not reproduced) and FogParams.y 1.1e-6 relative (x87 exp) (`water-system.md` 10.6). Effect: 29 pixels by 1 of
  3.6 M in the ocean replay.
- **One colour constant differs from the EXE's [C]:** the water's fog colour default `water.h` `Fog::color` blue is
  `243.0f / 255.0f` = 0x3F73F3F4, the EXE's (and the terrain module's) `243 * (1 / 255)` = 0x3F73F3F5, 1 ulp lower (found
  by 0188, `water-system.md` 10.7 pointer (a); still on `main` fc8fc964). No 8-bit pixel moves in 3609's replays [I: below
  the PS's rounding]; a one-character fix for whoever next touches `water.h`. *Fixed 2026-11-15 by 4305 (12.6):
  `Fog::color` is `n * (1 / 255)`, blue 0x3F73F3F5, as the EXE's.*
- No other colour constant of TerrainFrame or WaterFrame is set differently at the same frame: DistanceFog members 0-6
  bit-equal (0188), LOD members byte-equal (0191), Arg6 / FlagB / FlagC / ShadowMatrix0 as captured (0189, 0190), FOWAnim
  bit-equal (3953). [L by those tickets' replays; not re-run here.]

### 12.5 A5 and the work left: what of 3019's colour reference is left on `main` (fc8fc964)

Sizes are 3019's at its two cameras (Tiny 9V, 3601's captures; replay level unless marked) or the owning ticket's.

| Cause (3019 section 4) | Size at 3019's cameras | State on main | Owner |
|---|---|---|---|
| Distance fog | was p95 75-89, blue -8.6 to -16.9 | **done**, replay 0 px (0188) | - |
| LodMip | was MAD 2.9 / 2.8 / 1.8, p95 12 at zoom 17 | **done**, replay 0 px (0191) | - |
| Spec pass 0, t9, FlagB | was p95 1-7, max 42-182 | **done** for the inputs, replay 0 px (0189); the server's **t8 set** still differs: p95 1-4, max 52-182 on the tiles it touches (section 8; under D33 tile+0x300 is set on 61 of 460 tiles) | 0367 (backlog) |
| Shadows of models on the ground | 0 on clean ground; on all land MAD 0.7-1.2, max 81-82, 97.6-98.3 % within 16 | module done (0190, replay 0 px); the tap is not in the product. *2026-11-15: **done**: tap 4304 (11.9), bound by 4308 (11.10), live on the product build (11.11, 13.7); shadowed land at stock's ceiling, tiny9v pooled 97.5 % within 16 against stock's 97.1 % (13.6)* | 0312 (backlog; 4302 checks its premises live) *(done in epic 0169)* |
| Overlay (borders, city area) | 0 on clean ground; off it p95 24, max 175 | **done**: the EXE's own target tapped (0167, 3704) | - |
| Fog of war | 0 on 9V | **done** (0168: mask 99.99 %, LUT and FOWAnim exact; live in 8) | - |
| Decals (forest floor, river bed, roads, farms) | 3019's worst tiles (median 9) | **done** (0166, 4105 wiring, 4107 live): same-session bakes Tiny 2,543 / 2,568, huge 5,330 / 6,136 textures bit-equal; the rest named in `terrain-decals.md` 13.4 (equal-priority order 15 / 725 textures, quarry ground, edge column) | 0166 follow-ups |
| **Tile styles (0341)** | 116 of 150 (camera 1) and 55 of 58 (camera 2) drawn tiles are Asia; on their decal-free tiles t0 p95 0 / 13, max 49 / 50 (12.3, cache level) | open: every tile Generic. *2026-11-15: **done** by 4305 (12.6; superseded as 12.6 names); styled bare ground at stock's ceiling: tiny9v bare_asia 98.7 % against 98.0 %, huge bare_africa 95.8 % against 92.6 %, medians 0 (13.6)* | **0341** (backlog) *(done: 4305)* |
| Near-camera RGBA8 caches | camera 1 MAD 2.0 / 1.8 / 2.3, p95 8, max 53, 99.8 % within 16; camera 2 p95 5, max 72 (0306 replay) | open | 0332 (backlog) |
| Polar ice | not in 3019's terrain replay; drawn by stock, not under D33 | open. *2026-11-15: **done** for the scene: ice tap 4307, `c5r::ice` 4306, scene 4308, live 4310 / 4311 (`polar-ice.md` 7-12); the floes equal stock; the ice class tiny9v 91.4 % within 16 against stock's 94.7 %, huge pooled 90.8 % against 93.6 % (13.6). Left: the ice in the server's water reflection (decision 3, a follow-up ticket)* *2026-11-16: done by issue 0382 (`polar-ice.md` 13: huge pooled ice class 0/28/104, 92.3 % against stock's 93.6 %)* | 4301 (this epic, `polar-ice.md`) *(done: 4306-4308; the reflected ice: follow-up)* |
| The huge offset (+4.3 to +5.7 R/G, 3.2) | MAD 6.1-6.5 on two huge cameras | [I] about half Africa-as-Generic (12.3), the rest the fog missing at 3608's time (0188 since); not re-measured. *2026-11-15: 0341 done (4305); at 4309's huge cameras bare Africa ground is at stock's ceiling (13.6: pooled 0 / 9 / 160, 95.8 % against 92.6 %); 3608's two huge cameras (v1 / v2) were not re-measured* | 0341; a new frame-level pair after it *(the pair: 13.6)* |
| Water fog colour 1 ulp | no pixel seen | open, trivial. *2026-11-15: **done** by 4305 (12.6)* | whoever next touches `water.h` *(4305)* |
| Water + overlay ±1 run to run | 1 px by 1 in some huge frames | open | 0336 (ready) |
| Texture filtering, gamma | not found (3019) | - | - |

**What is not answerable offline, and what would answer it:** a frame-level number for the current product at 3019's
cameras. The library's Tiny bundle is the same map (b2ceb663...) but at turn 8 without every plot revealed and with its
own camera; no bundle holds 3601's cameras on the 9V save, and no library stock shot exists for the Tiny or huge bundles
(`offline-scene-harness.md` 18). It needs one recording: a bundle of `C5H-3600-TINY_0009V` with 3601's two camera poses
and a stock shot of each (one game-instance place, about 20 minutes with 3904's driver and `tools/scenario` poses), then
`scenerun` gives the product's fidelity there offline for every later change.
*2026-11-15 (epic 0169): the frame-level number at 3019's cameras is now measured live (13.6, decision 5: `w1` all_kept
80.7 % within 16 against stock's two-session 80.4 %, `w2` 88.6 % against 88.1 %; bare and shadowed land medians 0 on
tiny9v). The offline bundle at 3601's two camera poses with stock shots is not among the epic's recordings (14.2
recorded the pole), so the standing offline check is still to make.*

**Work left in seam G's item 1 (the colour inputs), with sizes:**
1. 0341, the tile style in the feed and the bake: 2-4 h (its estimate); worth on Africa ground p95 12 and a +2.5 R/G
   shift, elsewhere p99 at most 9. Acceptance offline with this section's runner.
2. The water fog colour constant: 5 minutes inside any ticket that touches `water.h` (no pixel effect seen).
3. A bundle at 3019's cameras with stock shots (above): about 1 h including the hand-back, one game-instance place; it
   turns A5 into a frame-level standing check.
Everything else in 12.5's table belongs to other tickets (0367, 0312, 0332, 4301, 0336); item 1's own inputs are exact.
*State 2026-11-15 (epic 0169): items 1 and 2 done by 4305 (12.6); item 3 open (the paragraph above); 0312 and 4301's
ice done (4304 / 4308, 4306-4308); left of the table: 0367 (the t8 set), 0332 (the near-camera RGBA8 caches), 0336
(water + overlay +-1), the reflected ice (decision 3) and, found at frame level, the terrain's wrap copies 1 world unit
off (13.6; section 2's note).*

### 12.6 As built: tile styles (epic 0169, task 4305 = ticket 0341, 2026-11-15)

Written 2026-11-15 from 13:17 EST by worker 4305 (lead-0169), branch `task-4305-tile-styles` from `epic-0169-seam-g`
dbe83490. Scripts: `render/evidence/0169/4305/`; game-derived outputs `F:/c5-64bit-work/0169/4305/` (D83, D141).

**The live question first: does the EXE re-style a tile when an art type changes in play? No [L].** VP's
`CvPlot::setImprovementType` changes the plot's continent art type when a farm (improvement 3) is built: no resource and
art type < 3 -> art + 2 (America -> Africa, Asia -> Europe, 0 -> Asia), rice -> Asia, maize -> America (CP/VP fork
`CvPlot.cpp` 8164-8180) [C]; `CvPlot::SetContinentType` tells the engine nothing (14403-14408) [C]. Measured
2026-11-15 13:40:13-13:40:49 EST on c5h-h (Goldberg, F:), stock session (no proxy, no server), 32-bit pin
`i0ship-26234371-ia` (MODS CP DLL 3c2f47da...), CP/VP/EUI, save copy `C5H-4305-TINY_0008` of the library's Tiny
`c5h-9028-tiny-t0008` (b2ceb663...), VT present (D94): `farm_style_4305.py` (ReadProcessMemory and the Lua channel):
- before: the EXE's tile +0x138 by the 4-plot rule over the Lua art types: 2,237 of 2,240 tiles equal (3406's figure);
  the EXE's style table (`[0xD90FC4] + 0x164`, byte `(y + 2) * 255 + x`) equals the art type on all 2,016 plots;
- plot (17, 14), America, no resource: `SetImprovementType(3)` through Lua at 13:40:18 -> the DLL's art type 1 -> **3**
  (Africa); by the rule three tiles ((16, 15), (17, 15), (16, 16)) go America -> Africa;
- 10 s later and again 20 s after a `UI.LookAt` of the plot: **no tile's +0x138 changed anywhere** (the three keep 3 =
  Generic | America) and **the EXE's style table keeps 1** at the plot. The EXE keeps the style it took at the load.
- Not tried: a turn end, a save and reload in the same process (a reload reads the saved art type, so the farm's
  style shows after a reload: the server does the same through the new snapshot).

**What the server does (follows the EXE):**
- **Feed** (`server/src/feed/srv_feed.h`): `PlotRecord::artType` (the byte that was `reserved1`; size 20 and every
  offset kept) from the pure `ICvPlot1::GetContinentType` (D30 list, `proxy/model/pure-methods.tsv`; the value Lua's
  `GetContinentArtType` returns), read at the snapshot and every refresh with the other getters (`kFArtType` in
  `kFGetterAll`; `srv_remote_feed.cpp` ReadPlot). A value outside 0..4 is stored as 0 (`SrvFeed::ArtTypeFromDll`) and
  counted (`art_type_outside` in `feed-per-turn.tsv`, appended after the old columns with `rf_art_type`): the rule
  takes no style from art types outside 1..4 either.
- **Bake and scene** (`srv_residency_source.*`, `srv_render_scene.cpp`): the residency keeps the art types of the DLL's
  map (the field's plots x rows - 4) and hands the same array to its bake (`MapInputs::plotArtTypes`: each tile bakes
  with its style's Sunlight / Shadowlight cubes and Terrain LUT, `bake::tileStyle`) and to the scene, which calls
  `Renderer::setTileStyles` after every successful field and whenever the array changes (t6 and t7 per tile). The
  array is filled at the field from the feed's table of the same size; a table that comes after the field (or a new
  load's, when the field found only the previous load's table) fills it then, and the tiles whose style changed are
  re-baked (`raiseTile`). **In play an art type change is not taken** (`ResidencySourceConfig::followArtChanges`,
  default false, as the EXE; counted `art_ignored`); with it true the change is taken and the tiles re-baked (tests).
  Log: `residency: tile styles of load seq N: ... tiles Generic a America b Asia c Africa d Europe e, fnv64 X` (X =
  FNV-1a 64 over the tiles' style bytes, row-major, for the live comparison with +0x138).
- Order on the product path: the feed's snapshot comes about 4 s before the field (0368's and 0140's server.log on
  the huge save: `feed: snapshot` at 106.2 s, `residency: field` at 110.4 s), so the field normally finds the load's
  table [L, those logs].
- `render_tiles=tap` (the EXE's caches, a test reference) has no residency: its tiles keep style 0 (t6 / t7 only;
  every install measured has equal files per style, 12.2). Named, not done.
- **t7 per style** (`render/src/terrain/terrain.cpp`): init reads the five `TerrainShadow_<Style>_ColorKey.tga`
  (missing = identity, a file that does not parse fails init naming it), `setShadowLut`, `shadowLut(style)`,
  `shadowLutsFromFile`, `FrameStats::shadowStyleTiles`; the draw binds the tile style's volume as t7 while a shadow
  is bound (Generic's before 4305).
- **Water fog colour** (`render/include/c5r/water/water.h` `Fog::color`): `n * (1 / 255)` as the EXE, blue 0x3F73F3F5
  (was 0x3F73F3F4, 12.4); red and green bits unchanged.

**Offline acceptance: the bake with the art types, per style** (2026-11-15 13:49-13:53 EST, `bakecompare` of build
`render-build/6ed2c2ff1a` (this branch at 998682bd; the bake pipeline is untouched by 4305), pure_vp install / c5h-i's
for the decal run, VT present (D94); `accept_bake_4305.sh`, `accept_decals_4305.sh`, `style_share_4305.py`; outputs
`F:/c5-64bit-work/0169/4305/bake{,2}/`). The server passes the same art types to the same `PipelineBaker` as
`bakecompare --plot-art` (the stand-in tests below show the array reaching the bake's `setMap`).
- **Against one stock session with its decals** (0166's 4106 references, armS, `--decals`, t8 from +0x2FC: 0166's
  standard same-session comparison), share of textures (t0 + t8) bit-equal to the EXE's caches, with the art types /
  every tile Generic (the product before 4305):

| Save | Style | Tiles | Textures | Bit-equal with styles | Equal only with the style | Equal only as Generic | BC1 blocks equal mip 0 |
|---|---|---|---|---|---|---|---|
| Tiny `C5H-0166-TINY_0008` | Generic | 646 | 1,292 | 1,290 (99.85 %) | - | - | 1.0000 |
| | America | 335 | 647 | **641 (99.07 %)** | 268 | **0** | 0.9999 |
| | Asia | 337 | 629 | **612 (97.30 %)** | 210 | **0** | 0.9962 |
| | all | 1,318 | 2,568 | 2,543 (99.03 %; as Generic 0.9358 of blocks) | | | 0.9991 |
| huge `C5H-0166-HUGE_0350` | Generic | 1,100 | 2,200 | 2,199 (99.95 %) | - | - | 1.0000 |
| | America | 101 | 200 | 146 (73.0 %) | 24 | **0** | 0.9983 |
| | Asia | 22 | 44 | 31 (70.5 %) | 8 | **0** | 0.9971 |
| | **Africa** | 417 | 825 | **703 (85.2 %)** | **320** | **0** | 0.9917 |
| | Europe | 1,445 | 2,867 | 2,288 (79.8 %) | 603 | **0** | 0.9959 |
| | all | 3,085 | 6,136 | 5,367 (87.5 %; as Generic 0.9106 of blocks) | | | 0.9969 |

  The style makes 1,433 textures bit-equal that were not, and breaks none. On Tiny the styled tiles reach the Generic
  tiles' share within 0.8 / 2.6 points; on huge the styled tiles are the land and carry 0166's named decal differences
  (`terrain-decals.md` 13.4: ties, edge column, quarry ground; the Generic tiles are mostly ocean, without decals), so
  their share stays lower for causes that are not the style [I: by 0166's per-tile causes; not re-separated here].
- Against 3401's reference sets without decals (4303's method, `accept_bake_4305.sh`): the rule arm reproduces 4303's
  run exactly (Tiny blocks mip 0 0.9185, huge 0.8494); as Generic no texture is equal that the styles lose; with the
  styles 71 (Tiny) and 134 (huge) textures more are equal.

**Live, stock session, the server's rule against the EXE** (2026-11-15 13:56:11-13:56:30 EST, c5h-h, same pins and save
copy as above, `styles_fnv_4305.py` through `live_farm_4305.sh` with `DRIVER=styles_fnv_4305.py`): the EXE's styles
Generic 1,551 / America 353 / Asia 336 (FNV-1a 64 `fa4b79fa78260c58`); the rule over the Lua art types (what the
residency computes from its feed) 1,552 / 351 / 337, **FNV-1a 64 `84e560b88a97709a`**; they differ on 3 tiles, the
"fifth plot" tiles of 3406 ((0, 21) rule Asia / EXE Generic, (15, 30) and (11, 32) rule Generic / EXE America: +0x138
1 and 3) [L]. So the server equals the EXE on 2,237 of 2,240 Tiny tiles; the 3 others need the EXE's fifth-plot rule
(not traced; a follow-up). *Corrected 2026-11-16 (0383, 12.7): (15, 30) and (11, 32) are fifth-plot tiles; (0, 21) is
not: the EXE's lists do not wrap in x, and the old rule's wrap reached an Asia plot of column 55. With both fixed the
rule gives the EXE's FNV `fa4b79fa78260c58` on this map.*

**Live, the D33 product path** (2026-11-15 13:58:37-14:02:17 EST, c5h-h; `build_pp_4305.sh`, `live_pp_4305.sh`,
`run_pp_4305.py` = 3308's prep / run / restore with the save and the run step adapted): server.exe and proxy release of
this branch at d15bb9e7 (server fc2c4548..., proxy 41d1b5ca...) against the x64 pin's release deps (fork 04b1211b8,
9045's build_all: DLL 74588135..., the bytes of the replaced layout `x64-04b1211b-pa803776`; the current layout's DLL
29F58E17... is the same fork commit built by the release driver: a functional check, no game result compared), composite
of the branch (d0163dfd...), 32-bit `i0ship-26234371-ia`, D33 with Overlay kept on, ServerRenderer = 1, save copy
`C5H-4305P-TINY_0008` of the library Tiny (b2ceb663...), VT present (D94):
- server.log: `feed: snapshot 56 x 36 at turn 8` at 43.0 s, then **`residency: tile styles of load seq 3: art types
  56x36 from the feed's table (table generation 1), tiles Generic 1552 America 351 Asia 337 Africa 0 Europe 0, fnv64
  84e560b88a97709a`** at 44.6 s, the field at 44.6 s (the table before the field, as 0368 / 0140 on huge) [L];
- `styles_fnv_4305.py` on the same process at 14:01:32: the rule over the DLL's art types (Lua) has the server's FNV
  exactly, and the EXE's +0x138 under D33 gives the stock session's styles (FNV `fa4b79fa78260c58`): **the server's
  tile styles equal the EXE's on 2,237 of 2,240 tiles**, the 3 others being the fifth-plot tiles above [L] (two
  fifth-plot tiles and one x wrap, 12.7, fixed 2026-11-16);
- 0 LOUD lines in server.log and render.log; the instance restored by hash (backup verify all equal / absent),
  provision verify ok, claim released. Picture `vc-20261115-140134-workstation-72df` (DIAGNOSTIC, the run's end shot).
- Not checked live: the huge save (*checked since: 13.7 by 4311, and with the corrected rule 12.7 by 0383, 2026-11-16*); t6 / t7 of the drawn tiles (the files are equal per style on every install, 12.2).

**Tests** (2026-11-15, this branch): `server/tests/feed/run_feed_test.py` (`TestArtType`: offset 19, every art type
0..4 through the snapshot, the refresh's change listed, an outside value stored as 0, the field alone, layout patches
leave it, a new snapshot re-reads it; plain 119 checks and MSVC ASan with the canary caught, 13:48); the fog scene's
stand-in `server/tests/render/fow/test_fow_scene.cpp` cases `tile_styles` (the art types at the field, the bake's
`setMap` gets the same array, the drawn tiles' t6 styles are the rule's, a changed art type and a new table in play are
not taken), `tile_styles_follow` (followArtChanges: taken, tiles re-baked, the renderer's styles set again by the poll)
and `tile_styles_late_table` (no table at the field: all Generic; a table of another size refused; the table of the
map's size taken, tiles re-baked) - all 14 cases pass plain and under ASan (13:49, 13:54); `test_residency_source.py`
30 passed (13:53); `render/build.py --test -R "c5r_terrain|c5r_water"` 10 of 10 (13:31; under MSVC `--asan` 10 of 10, 0 skipped, 0 ASan reports, 14:07; `c5r_terrain_fow_test`'s new
cases: the five TerrainShadow volumes equal the TGA rule's bytes of the install's files (all five equal on pure_vp, FNV of
the test `1055fb8b5653e503`), a crafted different file per style bound to that style's tiles only (full shadow: style 2
colour 4,428 px, style 4 2,160 px), a bad style file fails init naming it, a lone crafted Asia file; the water test checks
the three fog colour bit patterns).

**Supersedes in this section's earlier text** (sections 12.1-12.5 are 4303's and stay as measured): 12.1's "product:
style 0 everywhere (0341)" and "loading Generic only is exact", 12.4's last bullet (the 1-ulp blue: fixed) and 12.5's rows
"Tile styles (0341)" and "Water fog colour 1 ulp" (done by 4305); "list 12.6" in the section's first screen means 12.5.

### 12.7 The fifth plot and the edge columns: every tile's style equals the EXE's (ticket 0383, 2026-11-16)

Branch `task-0383-fifth-plot-tile-style`. The rule was traced from 3406's read-only probe files (each tile's plot list
at tile+0x04 and its +0x138, by PID; `D:/c5-64bit-work/3400/w6/live/{tiny,huge}_plots.txt`), no decompile (D83); the
full statement is in `terrain-bake-passes.md` 5.3. In short [L: the EXE's own lists of two live sessions]:
- a tile with **1 <= tx <= (ty + 1) / 2** also lists the plot of column tx - 1 that the four leave out: (tx-1, ty-2)
  for even ty, (tx-1, ty-1) for odd ty (400 of 2,240 Tiny tiles, 1,764 of 10,752 huge: a triangle at the left edge);
- the lists **never wrap in x** (the first and the last tile column list 3 plots on both wrapping maps).

**What changed in the server:** `bake::tileStyle` (`render/src/bakeheight/heightpage.cpp`) lists the fifth plot
(`tileListsFifthPlot`) and no longer wraps x (its `wrapX` argument is kept for the callers and ignored). Every user takes
it from there: the residency's style array and `Renderer::setTileStyles` (t6 / t7), the bake (`MapInputs::plotArtTypes`,
`PipelineBaker`), `bakecompare --plot-art`. No other code changed.

**Result against the EXE** (2026-11-16, offline over the EXE's live reads):

| Save | Tiles | EXE: Generic / America / Asia / Africa / Europe, FNV-1a 64 | 0383 rule | old rule (4 plots, wrap) |
|---|---|---|---|---|
| Tiny `c5h-9028-tiny-t0008` (3406's probe; same map as 4305's live product run) | 2,240 | 1,551 / 353 / 336 / 0 / 0, `fa4b79fa78260c58` | **equal on all 2,240, same FNV** | 3 differ, `84e560b88a97709a` (4305's server line) |
| huge `C5H-VP8P-HUGE_0350` (3406's probe; same counts as 13.7's live product run) | 10,752 | 7,531 / 151 / 111 / 1,509 / 1,450, `52c0219ec8b791d1` | **equal on all 10,752, same FNV** | 4 differ, `7c3745f6013ee421` (13.7's server line) |

The FNV is the one the server logs (`residency: tile styles ... fnv64 X`, row-major style bytes); the old rule's FNVs here
are exactly the ones 4305 (12.6) and 4311 (13.7) read in server.log, so the art types and the order are the server's.

**Live, the D33 product path** [L] (2026-11-16 03:01:29-03:10:38 EST, c5h-f (Goldberg, F:), claimed and restored by
hash, provision verify ok; `render/evidence/0169/0383/build_pp_0383.sh`, `live_pp_0383.sh`, `run_pp_0383.py`,
`styles_fnv_0383.py` = 4305's method with the 0383 rule): server.exe 19a67f0d... and proxy release 7a15f015... of this
branch at 2e1786ed against the x64 pin's release deps (DLL 74588135..., fork 04b1211b8), composite 63e3ccc1..., 32-bit EXE
04cfdc71..., D33 with Overlay kept on, ServerRenderer = 1, VT present (D94); save copies `C5H-0383P-TINY_0008` (of
c5h-f's `C5H-0366-TINY_0008`, b2ceb663...) and `C5H-0383P-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, c499d3c6...), each loaded
once, 0 AI turns:

| Save | server.log | EXE +0x138 (read by PID) | 0383 rule over the Lua art types | old rule |
|---|---|---|---|---|
| Tiny 56 x 36 | `tiles Generic 1551 America 353 Asia 336 Africa 0 Europe 0, fnv64 fa4b79fa78260c58` at 53.1 s | same counts, `fa4b79fa78260c58` | **0 of 2,240 differ** | 3 differ ((0, 21), (15, 30), (11, 32)) |
| huge 128 x 80 | `tiles Generic 7531 America 151 Asia 111 Africa 1509 Europe 1450, fnv64 52c0219ec8b791d1` at 129.9 s | same counts, `52c0219ec8b791d1` | **0 of 10,752 differ** | 4 differ ((16, 49), (16, 50), (15, 52), (18, 53)) |

**The server's tile styles equal the EXE's on every tile of both saves** (the server's FNV = the EXE's FNV). 0 LOUD lines
in server.log and render.log of both runs. Outputs `F:/c5-64bit-work/0383/live-{tiny,huge}/styles_fnv_0383.json`.
Pictures (DIAGNOSTIC, the runs' end shots, Visual Checks issue 0383): `vc-20261116-030439-workstation-5ab1` (Tiny, the
map drawn) and `vc-20261116-030942-workstation-f1af` (huge: a uniform grey frame about 10 s after the style line; not
investigated here, the check is the data above).
Each half alone is not enough: the fifth plot with wrap leaves Tiny (0, 21); no wrap without the fifth plot leaves Tiny
(15, 30), (11, 32) and the four huge tiles (16, 49), (16, 50), (15, 52), (18, 53). The tile (11, 28) of `fog-of-war.md`
8.2 (Tiny T0018) is a fifth-plot tile too.

**Tests** (`render/tests/bakeheight/bakeheight_test.cpp`): `game_tiny_tile_style_from_art_types` and
`game_huge_tile_style_from_art_types` now require every tile equal to the EXE's +0x138 (were 8 allowed to differ);
`tile_style_fifth_plot_synthetic` (the triangle's edges and size on both maps, each parity's fifth plot, tiles just
outside the triangle, no wrap at either edge, an art type >= 32 ignored); `tile_style_from_plots_synthetic`'s wrap case
now expects no wrap.

## 13. Acceptance of epic 0169 at frame level (done C): the stock side and the method (task 4309, 2026-11-15)

Written 2026-11-15 from 15:08 EST by worker 4309 (lead-0169), branch `task-4309-acceptance-stock` from
`epic-0169-seam-g` 21063236. Scripts: `shell/evidence/0169/4309/` (driver, chain) and `render/evidence/0169/4309/`
(comparison); game data (pictures, census, camera blocks, heights, plans, comparison output) on
`F:/c5-64bit-work/0169/4309/` (D83, D141), never committed. Method copied from 0166's done C (`terrain-decals.md` 16,
`shell/evidence/0166/4107/`), 3019's census and clean mask (`terrain-colour-reference.md` 3) and 3408's huge script.

### 13.1 Saves, cameras and how they are driven

Stock arm: instances c5h-h and c5h-i (Goldberg, F:), 32-bit pin `i0ship-26234371-ia` (3c2f47da...) in the CP slot,
darkred stock config.ini, no proxy, no server, no composite; CP/VP/EUI; **VT present, folder hash `45b3b433...`
(55 files) on c5h-h, c5h-i and c5h-c** (D94: the joined arm must run on an instance with this hash, `vthash.py` of
`shell/evidence/0166/4107/`). Each arm loads its own copy `C5H-0169-4309-<MAP>` (removed at the arm's end).

| Map (`--map`) | Save (source, SHA-256) | Cameras (`plan_<map>.json`, made by stock1 from its census) |
|---|---|---|
| `tiny9v` | `C5H-3600-TINY_0009V` (c5h-c, 27420a41...; 3601's Tiny state, turn 9, every plot revealed and visible) | `p1_pole_44_3` (4301's south pole, ice); `w1_cam1_3601_33_14`, `w2_cam2_3601_51_14` (3601's two cameras, 3019's reference, decision 5); `x1_seam_*` (x = W-1 or 0 with the most unit, city and improvement plots within 4); `s1_<style>_*` (the most bare Africa plots within 3, else Asia); `g1_grid_*` (s1's plot with the hex grid on) |
| `tiny_0008` | `C5H-0166-TINY_0008` (c5h-i, b2ceb663...; 0166's Tiny decal save, turn 8) | 0166's five (4107's plan: the capital (51, 14), (53, 12), (53, 15), (49, 12), the capital 4 wheel ticks in). Only 28 plots are revealed, so the save has no pole or seam in view: those cameras are on tiny9v and huge |
| `huge` | `C5H-0166-HUGE_0350` (c5h-i, c499d3c6...; turn 350) | the 20 unique step plots of 3408's script (`shell/evidence/3408/script_huge.json`, as 4107) plus `p1_pole_*` (the most revealed ice plots within 4, rows 2-5 or H-6..H-3) and `x1_seam_*` |

Per arm (`run_4309.py`, 4107's driver imported with its census, plan, cameras and stock arm replaced): after the load
+20 s gt_reader heights and grid; the census of every plot as the active team sees it (Lua, read only: 4107's fields
plus `Plot:GetContinentArtType` and the FEATURE_ICE id); then, before the cameras (EFFECTIVENESS E9), the auto unit
cycle off (cached option, put back at the end) and every unit of the active player asleep (`PushMission(MISSION_SLEEP)`,
the save copy's state, the same in every arm); per camera `UI.LookAt(plot, 0)`, wheel ticks, the hex grid state (key G,
checked by `OptionsManager.GetGridOn`; Lua `UI.SetGridVisibleMode` as a named fallback), popups closed by Esc until the
top band is undimmed (D114), the cursor over the minimap, 8 s, the camera block, the picture, the camera read again: **a
camera that moved fails the arm** (exit 5). Stock pictures are PrintWindow harness shots recorded in Visual Checks as
stock references (`--result info`, issue 4309, the asset set from the instance, D94); the joined arm records the
composite's back buffer and a passthrough frame (the model mask) exactly as 4107.

### 13.2 Plot classes and masks

`compare_4309.py`: pixel to plot by a ray cast against the EXE's drawn height field, cut at the water plane (z 10.1167)
where the hit lies below it (ice and shadows on water lie on the surface), then the nearest plot centre. Classes
(revealed plots): **shadowed_land** (unit, city or improvement plots and their six neighbours, land, without farm
and route plots) and **shadowed_water** (the same on water); **farm_route** (0166's P6 plots, info); **ice** (FEATURE_ICE); **bare_<style>** (bare land outside `shadowed`, by the
plot's continent art type: africa, asia, america, europe) and **bare_generic** (art type 0, the control); **all_kept**.
Mask as 0166 C: minus 16 px around the EXE-drawn pixels of the joined arm's passthrough frame (without one: the census
model cover, ice and flood plains not counted as models, named per camera), minus 16 px around tree columns (trees standing 24 world units) and 32 px around tree plots, minus the
interface rectangles + 16 px, minus a 16 px frame; variant `inner` = plots at least 2 from any unrevealed plot.
Since 2026-11-16 (ticket 0397) the compare also runs two model checks that this mask cannot: a **models** class (model
plots' regions, unmasked, without RANDOM-row, fogged and P6 plots; reported) and the **drawn-whole** gate (the joined
presented frame against its own passthrough frame on opaque model pixels; exit 5 when it fails). Method, numbers on
4311's sets and limits: `landmarks-and-models.md` 7.5; code `tools/compare/model_checks.py`.

### 13.3 The command for the joined arm (wave 4)

On an instance whose VT folder hash is `45b3b433...` (D94), with the seam-G builds (server.exe and proxy release of the
team branch against the current x64 pin's deps, the composite of the branch; COMMON 18):

    export C5_4309_OWNER=0169-<task> C5_4309_ISSUE=<task> C5_4309_BUILD="<commit; server.exe, proxy, composite SHA-256>"
    sh shell/evidence/0169/4309/joined_chain.sh <instance> dry   <srv-dir> <shell-dir> tiny9v tiny_0008 huge   # DIAGNOSTIC
    sh shell/evidence/0169/4309/joined_chain.sh <instance> final <srv-dir> <shell-dir> tiny9v tiny_0008 huge   # ACCEPTANCE
    python -I render/evidence/0169/4309/compare_4309.py --arm final

`joined_chain.sh` runs `run_4309.py prep / run / restore` per map (4107's joined path: SHA-256 backup, ServerRenderer = 1,
D33 A'' form, the shipped composite ini; per camera the presented frame and a passthrough frame, recorded under
`C5_4309_ISSUE`); pictures land in `F:/c5-64bit-work/0169/4309/captures/<map>/<arm>/` beside the stock sessions. The
comparison takes every camera of the plans, the joined arm's passthrough frames as the model mask for **both** columns
(joined vs stock1, stock2 vs stock1), and writes `F:/c5-64bit-work/0169/4309/compare/<arm>/tables_<arm>.md`, per map a
`compare_<arm>.json` and difference images. Not run by 4309: the joined path itself is 4107's unchanged and was not
dry-run here (no seam-G product yet); the passthrough-mask branch of the comparison was checked on a fabricated arm
(stock2's pictures with an all-clear passthrough frame and a fake top band: the mask was taken, both columns equal).

### 13.4 Stock findings (2026-11-15, the sessions of 13.1) [L]

- **PrintWindow returns UI-less frames on the stock path too**: 3 of 66 first stock shots (huge `s02` in stock1, `s08`
  and `s21` in stock2) had no interface and no unit or city models (the terrain, water and improvements drawn). The
  top bar's bright lower edge (row 27 of 1411) stands 37-83 grey levels over the bar rows above it in the 63 good shots
  and 0-1 in the three: `run_4309.py ui_present()` now refuses such a shot, retakes it (up to three times) and records
  only a checked shot. The three feed entries are marked SUPERSEDED (amended notes); retaken by `stock_extra.sh`.
- **Stock's own differences between two sessions** (what the ceiling holds): the water's animation (shadowed_water median
  4-9 on every camera with water), farm-field layout and route curves (0166 P6; on huge `s01` the farm plots are the
  white areas of the difference image: hence `farm_route` apart and out of `shadowed_land`), tree placement and sway at
  forest edges (tiny9v cam1: the left-over differences of `shadowed_land` p95 57 lie on forest edges; the tree mask was
  widened to 24 world units and 32 px), particle beams over cities, the cloud edges (tiny_0008: every bare plot is within
  2 of an unrevealed plot, so its `inner` variant has no bare plot at all). Camera blocks of the two sessions differ by
  at most 0.13 world units (huge `s21`), most below 0.004 (the drawn height +-1 u16, as 4107).
- **Classes present; no Generic land** (census of stock1, Lua `GetContinentArtType`): **no revealed land plot of the
  three saves has art type 0** (tiny9v: America 266, Asia 256; tiny_0008: Asia 23; huge: Africa 1,345, Europe 1,263,
  Asia 43, America 38); art type 0 is on water only. So the class `bare_generic` is empty and the brief's Generic
  control does not exist on these saves; the control is bare ground of the styles whose inputs equal Generic's except
  Shadowlight (America, Asia, Europe: 12.2-12.3, t0 p95 0 as Generic), and `bare_africa` is the styled class that
  changes (Sunlight differs, 12.3). Bare plots outside `shadowed_land`: tiny9v America 84 / Asia 77, tiny_0008 Asia 6,
  huge Africa 22 / Europe 29 (huge is built over: 1,550 shadowed land plots, 1,001 farm or route plots); at the cameras
  huge shows Africa only. Ice: 272 plots on tiny9v (1.48 M px at `p1`), 923 on huge (`p1`, `x1`).

### 13.5 Stock's ceiling per save, camera and class (the self-test, 2026-11-15 16:00-16:10 EST) [L]

`python -I render/evidence/0169/4309/compare_4309.py --self-test` (joined = stock session 2; exit 0, "3 maps, 33
cameras compared, 0 problems": the joined column, computed by the same path, equals the ceiling column on every camera
and pooled). Sessions of 13.1 (stock1 / stock2 of each save; the three retaken huge cameras from `stock_extra.sh`),
VT `45b3b433...`, pin `i0ship-26234371-ia`. **Mask here: the census model cover** (no passthrough frame exists for a
stock pair); the joined arm's run recomputes this ceiling with its passthrough frames as the mask for both columns, so
its figures, not these, stand beside the joined ones. All revealed plots of the class; the `inner` variant equals these
on tiny9v and huge (all, or nearly all, plots revealed) and on tiny_0008 keeps no bare plot (13.4). Output:
`F:/c5-64bit-work/0169/4309/compare/stock2/` (`tables_stock2.md`, per map `compare_stock2.json`, difference images and
class maps per camera). Indications of what is left as stock's own: water animation (shadowed_water), P6 farm and route
layout (farm_route, huge only), tree placement at forest edges (tiny9v `w1`, tiny_0008), particle smoke and beams
(huge `s08`, `s21`), cloud edges (tiny_0008 bare).

**tiny9v** (6 cameras; cells: median / p95 / max of the largest channel difference, share within 16 in %; pooled pixels in the last row)

| Camera | shadowed_land | shadowed_water | ice | bare_asia | bare_america | all_kept |
|---|---|---|---|---|---|---|
| `p1_pole_44_3` | 0/19/77, 93.6 | 11/37/86, 65.1 | 0/18/223, 94.5 | 0/5/57, 99.2 | - | 3/35/223, 80.4 |
| `w1_cam1_3601_33_14` | 0/57/216, 89.6 | 7/23/70, 85.9 | - | 0/22/146, 93.8 | - | 5/36/216, 80.2 |
| `w2_cam2_3601_51_14` | 0/4/122, 99.1 | 4/21/88, 91.8 | - | 0/23/158, 94.0 | - | 0/40/217, 89.3 |
| `x1_seam_55_14` | 0/1/118, 99.2 | 6/20/46, 91.0 | - | 0/27/146, 93.5 | 0/9/158, 96.4 | 3/20/208, 93.0 |
| `s1_asia_42_12` | 0/1/167, 99.6 | - | - | 0/5/182, 99.5 | - | 0/5/182, 98.1 |
| `g1_grid_42_12` (grid on) | 0/1/157, 99.6 | - | - | 0/4/162, 99.5 | - | 0/5/207, 98.1 |
| **pooled** | **0/1/216, 98.8** (2.02 M px) | **6/29/88, 82.5** (0.29 M px) | **0/18/223, 94.5** (1.48 M px) | **0/5/182, 98.0** (2.62 M px) | **0/9/158, 96.4** (0.26 M px) | **1/29/223, 88.9** (10.96 M px) |

**tiny_0008** (5 cameras; cells: median / p95 / max of the largest channel difference, share within 16 in %; pooled pixels in the last row)

| Camera | shadowed_land | shadowed_water | bare_asia | all_kept |
|---|---|---|---|---|
| `c1_capital` | 0/14/139, 95.5 | 4/18/102, 94.0 | 0/56/168, 80.5 | 0/31/168, 88.9 |
| `c3_plot_53_12` | 0/24/142, 93.5 | 7/20/60, 90.2 | 0/40/161, 85.4 | 3/23/161, 89.3 |
| `c4_plot_53_15` | 0/3/136, 97.9 | 3/13/23, 98.8 | 1/59/148, 73.4 | 1/26/148, 91.8 |
| `c5_plot_49_12` | 0/13/91, 96.0 | 8/22/153, 87.8 | 2/31/86, 84.9 | 3/27/178, 85.2 |
| `c2_capital_zoomin` | 0/18/159, 94.5 | 4/21/68, 92.2 | 0/53/131, 80.6 | 0/30/167, 91.2 |
| **pooled** | **0/16/159, 95.1** (1.13 M px) | **5/20/153, 91.5** (0.25 M px) | **0/49/168, 81.4** (0.80 M px) | **1/28/178, 89.3** (3.65 M px) |

**huge** (22 cameras; cells: median / p95 / max of the largest channel difference, share within 16 in %; pooled pixels in the last row)

| Camera | shadowed_land | shadowed_water | farm_route | ice | bare_africa | all_kept |
|---|---|---|---|---|---|---|
| `p1_pole_74_76` | - | 2/19/100, 93.4 | - | 1/21/109, 93.1 | - | 1/20/243, 93.2 |
| `x1_seam_127_73` | - | 1/18/42, 92.9 | - | 0/17/106, 94.3 | - | 3/21/144, 91.5 |
| `s00_start_101_25` | 0/1/192, 98.0 | 9/32/161, 72.4 | 22/87/220, 42.1 | - | - | 0/57/220, 80.6 |
| `s01_pan_111_25` | 0/92/189, 88.9 | 9/47/216, 72.4 | 22/95/209, 42.0 | - | - | 9/63/216, 69.1 |
| `s02_pan_111_35` | 1/6/58, 97.7 | 4/16/102, 95.1 | 0/86/133, 80.3 | - | - | 4/24/133, 91.4 |
| `s03_pan_101_35` | 0/16/101, 95.0 | 8/106/184, 67.0 | 27/92/198, 35.4 | - | - | 8/88/198, 60.1 |
| `s05_jump_51_25` | 0/10/89, 96.6 | 3/43/186, 88.1 | 2/34/94, 77.5 | - | - | 2/34/186, 87.3 |
| `s06_jump_84_31` | 0/43/201, 89.3 | - | 1/77/223, 66.3 | - | 3/77/164, 85.1 | 0/55/223, 85.2 |
| `s08_jump_93_37` | 18/56/106, 44.9 | - | 31/97/213, 27.6 | - | - | 28/94/213, 30.0 |
| `s09_jump_54_52` | 1/9/57, 98.5 | 5/36/140, 88.3 | 12/41/108, 62.1 | - | - | 6/34/140, 81.0 |
| `s11_jump_51_43` | 0/5/97, 96.9 | - | 1/30/67, 82.9 | - | - | 0/19/97, 94.0 |
| `s12_jump_72_58` | 0/5/60, 97.6 | 6/27/137, 87.8 | 14/36/102, 59.6 | - | 0/1/27, 99.9 | 4/27/137, 84.5 |
| `s14_jump_78_43` | 0/9/80, 96.6 | - | 1/37/61, 74.0 | - | 0/24/84, 94.2 | 0/18/84, 94.7 |
| `s15_jump_96_34` | 0/12/186, 96.7 | - | 11/87/206, 54.4 | - | - | 1/77/206, 69.1 |
| `s17_jump_90_37` | 0/22/107, 93.8 | - | 6/55/183, 61.5 | - | - | 0/44/183, 81.5 |
| `s18_jump_111_46` | 0/37/91, 90.7 | 6/45/157, 83.8 | 0/62/119, 84.3 | - | - | 5/29/157, 88.0 |
| `s20_jump_78_31` | 0/24/193, 93.3 | 8/42/55, 70.2 | 0/32/81, 83.1 | - | 0/60/134, 88.2 | 0/27/193, 92.3 |
| `s21_jump_93_52` | 5/22/251, 91.0 | 5/20/250, 93.5 | 9/43/199, 69.4 | - | - | 6/35/251, 82.5 |
| `s23_jump_84_28` | 0/41/171, 86.8 | 8/95/243, 75.9 | 1/77/180, 62.0 | - | 0/51/100, 89.1 | 5/80/243, 77.4 |
| `s24_jump_90_16` | - | 9/36/187, 75.8 | 18/57/191, 47.6 | - | - | 7/34/191, 79.2 |
| `s26_jump_39_37` | 0/7/91, 96.9 | 1/12/59, 96.8 | 15/41/96, 54.7 | - | - | 4/35/96, 75.7 |
| `s27_jump_99_46` | 0/2/84, 98.2 | - | 7/41/105, 67.3 | - | - | 0/32/105, 85.2 |
| **pooled** | **0/20/251, 94.1** (4.57 M px) | **6/38/250, 82.8** (4.68 M px) | **12/64/223, 58.9** (3.37 M px) | **1/19/109, 93.5** (2.96 M px) | **0/42/164, 91.8** (0.13 M px) | **3/38/251, 84.3** (21.78 M px) |

### 13.6 The joined arm on the product build (done C, task 4311, 2026-11-15 19:45-20:41 EST) [L]

Worker 4311 (lead-0169), branch `task-4311-acceptance` from `epic-0169-seam-g` a8a8dd8e. **ACCEPTANCE** in the sense of
D112 / D114 / D94: the full shipped system as a player runs it. **Build**: the one product build of `polar-ice.md` 11.4,
`C:/c5-64bit-work/0169-build/x64-04b1211b-p0b0dc14df-0169` (commit 0b0dc14df; server.exe 171545D8..., proxy 1393AE42...,
composite 4F69BC1A..., x64 DLL 29F58E17...; all 20 files equal to `shell/evidence/0169/4310/product_build_manifest.json`
and the repository's `shell_composite.ini` = the build's AFB0491B..., checked by `shell/evidence/0169/4311/check_build_4311.py`
before every run). **Settings**: 3308's prep as 4107's acceptance: 32-bit EXE 04CFDC71... in the CP slot, the proxy and
server.exe of the build, the composite and its manifest, the shipped `shell_composite.ini` unchanged (`shadow_tap=1`,
`ice_tap=1`, `overlay_tap=1`, `decal_tap=1`; no key set by the driver), VP64Options.ini `[Renderer] ServerRenderer = 1`,
D33 in config.ini in the shipped A'' form (Overlay, River, Arrows on), no viewmask marker, server.cfg defaults
(`render_shadow=1`, `render_ice=1`), no server test key; vpproxy.ini keys of 3308's prep (`mode=on`, `local_answers=1`,
`local_verify=0`, `lua_mirror=0`, as 4107's acceptance). **Instance** c5h-h (F:, Goldberg; VT folder hash 45b3b433...,
55 files, the stock sessions' asset set, D94), ShadowLevel 3. Saves: 4309's copies `C5H-0169-4309-<MAP>` of 13.1.
**Pictures**: the composite's back buffer (the presented frame) per camera and a passthrough frame (the model and
interface mask), the 33 cameras of 4309's plans, units asleep and the unit cycle off, popups closed (none was open),
camera blocks read before and after each picture (0 moved; joined against stock1's block at most 0.16 world units, huge
`s02` and `s26`, as stock2's 0.13; every other camera below 0.005).

Runs (`sh shell/evidence/0169/4311/joined_4311.sh c5h-h final tiny9v tiny_0008 huge`, 4309's `joined_chain.sh` and
`run_4309.py` unchanged): a **dry arm first** (DIAGNOSTIC, tiny9v, 2 cameras, 19:48-19:53: the joined path of 4309 works as
written, restore by hash ok; its `g1` camera differed by 0.71 world units because `C5_4309_ONLY` skipped `s1`, which `g1`
follows in the plan: run the plan in order), then **final** tiny9v 19:55-20:01, tiny_0008 20:01-20:07, huge 20:07-20:21,
each rc 0, restore by hash ok. In each run: guard checks 0, start-up watch ok; the shadow tap INSTALLED (20 patterns),
the ice tap READY (21 patterns; Tiny 1 chunk, huge 8 chunks, 427,774 triangles); the server's `shadow 1s:` lines end
ACTIVE with `frames_none 0`, `frames_q0 0`, refused / stale / unmatched / bad / failed 0, q 3, fmt 56, and ice ACTIVE,
mesh_refused 0, draw_err 0, drawn in every frame (Tiny 3 draws / 871,065 indices per frame, huge 24 draws / 3,849,966);
**0 LOUD lines** in render.log, server.log and vpproxy.log of all three; the composite log of huge has one,
`overlay_tap: LOUD: missed copy: no free slot (missed 3, faults 0, copies 10728)` (a counted lag of the overlay tap, D132,
not seam G). Memory after the load (indications, COMMON 23): EXE private 1,285 / 1,289 / 1,445 MB, largest free block
1,951 / 1,955 / 1,871 MB; server private 744 / 816 / 1,645 MB. Then `python -I render/evidence/0169/4309/compare_4309.py
--arm final` (exit 0) and `python -I render/evidence/0169/4311/tables_4311.py --arm final`. Output (game data, D83):
`F:/c5-64bit-work/0169/4309/compare/final/` (`tables_final.md`, `tables_4311_final.md`, per map `compare_final.json`,
difference images and class maps); pictures `F:/c5-64bit-work/0169/4309/captures/<map>/final/`; run logs
`C:/c5-64bit-work/0169-4311/work/<map>-final/out/` copied to `F:/c5-64bit-work/0169/4311/final_runs/`.

**Tables** (mask: the joined arm's passthrough frame + 16 px for both columns, trees, interface, 16 px frame; all revealed
plots of the class). Cells `J | C`: J = joined vs stock session 1, C = stock session 2 vs stock session 1 (stock's
two-session ceiling, the same mask); each `median/p95/max share-within-16 %` of the largest channel difference; `*` = J
further from stock1 than C (share more than 3 points lower or median more than 2 higher).

**tiny9v** (final; cells `J m/p95/max s% | C m/p95/max s%`, J = joined vs stock1, C = stock2 vs stock1, the same mask; * = J further from stock1 than C)

| Camera | mask | cam delta | shadowed_land | shadowed_water | farm_route | ice | bare_asia | bare_america | all_kept |
|---|---|---|---|---|---|---|---|---|---|
| `p1_pole_44_3` | pt | 0.00061 | 0/13/53 96.6 \| 0/23/78 92.5 | 11/36/88 67.0 \| 11/36/86 65.6 | - | *0/24/222 91.4 \| 0/17/223 94.7 | 2/5/43 99.7 \| 0/5/57 99.2 | - | 4/36/222 78.9 \| 3/35/223 80.3 |
| `w1_cam1_3601_33_14` | pt | 0.00055 | 0/51/164 92.4 \| 0/56/216 89.1 | 8/24/191 84.6 \| 7/23/195 86.0 | *3/40/58 74.7 \| 0/35/58 79.1 | - | 0/10/143 96.0 \| 0/22/146 93.8 | - | 5/35/191 80.7 \| 6/36/216 80.4 |
| `w2_cam2_3601_51_14` | pt | 0.00037 | 0/28/157 93.0 \| 0/39/179 92.0 | 4/25/146 90.2 \| 4/25/150 90.2 | 0/43/74 65.9 \| 0/42/74 65.9 | - | 0/19/158 94.6 \| 0/24/158 93.9 | - | 0/41/208 88.6 \| 0/44/208 88.1 |
| `x1_seam_55_14` | pt | 0.0021 | 0/1/131 99.7 \| 0/1/131 99.1 | *2/20/117 92.8 \| 2/14/46 96.7 | 0/1/20 99.9 \| 0/1/1 100.0 | - | 1/18/145 94.7 \| 0/31/146 92.9 | *11/36/164 68.9 \| 0/9/158 96.4 | *5/26/211 88.1 \| 3/19/208 93.2 |
| `s1_asia_42_12` | pt | 0.002 | 0/3/234 98.6 \| 0/2/234 98.7 | 2/5/7 100.0 \| 1/3.7/5 100.0 | - | - | 0/1/101 99.9 \| 0/4/182 99.5 | - | 0/3/234 98.1 \| 0/5/234 97.9 |
| `g1_grid_42_12` | pt | 0.002 | 0/3/234 98.7 \| 0/2/234 98.7 | 2/4/7 100.0 \| 1/4/5 100.0 | - | - | 0/1/101 99.9 \| 0/4/162 99.5 | - | 0/3/234 98.2 \| 0/5/234 97.9 |
| **pooled** | | | **0/5/234 97.5 \| 0/5/234 97.1** | **6/29/191 83.5 \| 6/28/195 83.9** | **0/40/74 78.9 \| 0/39/74 79.5** | ***0/24/222 91.4 \| 0/17/223 94.7** | **0/3/158 98.7 \| 0/5/182 98.0** | ***11/36/164 68.9 \| 0/9/158 96.4** | **1/30/234 88.3 \| 1/29/234 89.1** |

**tiny_0008** (final; cells `J m/p95/max s% | C m/p95/max s%`, J = joined vs stock1, C = stock2 vs stock1, the same mask; * = J further from stock1 than C)

| Camera | mask | cam delta | shadowed_land | shadowed_water | farm_route | bare_asia | all_kept |
|---|---|---|---|---|---|---|---|
| `c1_capital` | pt | 0 | *3/29/205 92.7 \| 0/13/139 95.8 | 4/20/194 92.8 \| 3/19/192 93.4 | *4/91/173 89.6 \| 0/52/147 88.2 | *5/110/199 66.9 \| 0/61/168 77.4 | *4/54/205 82.8 \| 0/34/192 88.3 |
| `c3_plot_53_12` | pt | 0.002 | *3/22/146 93.7 \| 0/18/142 94.7 | 6/21/201 89.7 \| 6/20/206 91.0 | *4/23/146 93.5 \| 0/21/128 94.1 | *4/67/134 80.0 \| 0/46/161 81.0 | *6/41/201 77.3 \| 3/24/206 89.2 |
| `c4_plot_53_15` | pt | 0.00031 | *4/13/143 96.5 \| 0/2/136 97.9 | *4/21.5/131 92.6 \| 1/26/116 91.2 | *4/29/175 92.2 \| 1/41/158 90.1 | *5/122/178 66.6 \| 1/62/148 69.4 | *5/55/178 83.8 \| 1/29/158 91.0 |
| `c5_plot_49_12` | pt | 0.0016 | *4/19/151 94.0 \| 0/12/150 96.3 | 7/22/162 88.1 \| 7/24/153 87.6 | *4/117/152 83.5 \| 0/67/138 81.2 | *10/78/181 57.4 \| 3/36/125 81.9 | *7/61/181 69.8 \| 3/28/178 85.2 |
| `c2_capital_zoomin` | pt | 0.0013 | *5/56/182 88.8 \| 0/17/193 94.8 | 5/25/182 90.1 \| 3/23/220 91.3 | *5/124/187 82.8 \| 0/65/181 82.2 | *6/83/179 76.9 \| 0/56/151 78.8 | *5/59/187 85.0 \| 0/31/220 90.8 |
| **pooled** | | | ***4/31/205 91.5 \| 0/15/193 95.5** | **5/22/201 90.5 \| 4/22/220 91.2** | ***5/110/187 86.5 \| 0/58/181 85.5** | ***5/92/199 70.6 \| 0/54/168 78.5** | ***5/56/205 80.8 \| 1/29/220 89.2** |

**huge** (final; cells `J m/p95/max s% | C m/p95/max s%`, J = joined vs stock1, C = stock2 vs stock1, the same mask; * = J further from stock1 than C)

| Camera | mask | cam delta | shadowed_land | shadowed_water | farm_route | ice | bare_africa | all_kept |
|---|---|---|---|---|---|---|---|---|
| `p1_pole_74_76` | pt | 0.00012 | - | 3/37/98 91.1 \| 3/19/100 93.7 | - | *0/47/231 89.0 \| 1/21/109 93.3 | - | *1/40/239 89.6 \| 1/19/109 93.6 |
| `x1_seam_127_73` | pt | 0.00098 | - | 3/19/45 92.1 \| 3/19/42 92.2 | - | 0/18/224 94.1 \| 0/17/106 94.3 | - | 4/23/238 90.7 \| 3/21/109 91.9 |
| `s00_start_101_25` | pt | 0.00049 | *0/67/226 75.9 \| 0/1/202 97.8 | 10/34/118 69.7 \| 10/33/136 70.8 | *25/105/226 37.9 \| 12/89/220 54.6 | - | - | *2/80/226 67.0 \| 0/52/220 86.5 |
| `s01_pan_111_25` | pt | 0 | *0/64/208 84.3 \| 0/67/204 90.4 | 8/47/234 73.3 \| 8/43/232 74.3 | *34/149/238 25.3 \| 21/96/200 43.6 | - | - | *9/64/238 69.6 \| 8/55/232 72.8 |
| `s02_pan_111_35` | pt | 0.16 | *3/89/236 79.6 \| 2/28/168 89.7 | 4/20/234 93.3 \| 4/16/141 95.3 | 4/40/168 88.5 \| 4/43/150 88.8 | - | - | 4/27/236 90.9 \| 4/23/168 91.9 |
| `s03_pan_101_35` | pt | 0.00073 | 0/39/228 90.0 \| 0/46/155 88.1 | 9/108/194 63.8 \| 9/105/182 65.2 | 17/85/231 49.0 \| 16/85/230 50.8 | - | - | 4/78/231 65.6 \| 3/78/230 66.0 |
| `s05_jump_51_25` | pt | 0.0035 | *0/72/220 88.4 \| 0/7/65 97.4 | 3/40/98 88.6 \| 3/41/91 88.5 | 1/34/216 79.3 \| 1/34/102 78.2 | - | - | 2/38/220 86.1 \| 2/33/103 87.0 |
| `s06_jump_84_31` | pt | 0.0029 | 0/37/152 90.8 \| 0/40/188 90.2 | - | 2/58/194 71.1 \| 5/64/197 69.0 | - | 0/47/160 91.6 \| 3/77/164 85.1 | 0/46/194 86.2 \| 0/51/197 85.2 |
| `s08_jump_93_37` | pt | 0.0012 | 2/85/230 78.3 \| 11/90/226 61.6 | - | 25/88/212 42.0 \| 31/91/211 27.0 | - | - | 23/88/230 44.0 \| 30/91/226 29.0 |
| `s09_jump_54_52` | pt | 0.003 | 1/11/85 96.8 \| 1/11/76 96.9 | 5/30/82 89.8 \| 5/28/81 90.2 | *11/47/237 63.0 \| 10/40/103 66.3 | - | - | 5/37/237 81.0 \| 5/33/103 82.2 |
| `s11_jump_51_43` | pt | 0.00079 | *0/41/246 90.1 \| 0/15/115 95.2 | - | *4/33/98 79.8 \| 3/30/87 83.4 | - | - | *0/39/246 87.6 \| 0/24/115 92.3 |
| `s12_jump_72_58` | pt | 0.00061 | 0/5/235 97.8 \| 0/5/103 97.8 | 6/22/125 90.7 \| 6/22/103 91.0 | 11/34/96 69.3 \| 12/37/102 64.2 | - | 0/1/8 100.0 \| 0/1/8 100.0 | 2/23/235 89.8 \| 2/24/103 89.0 |
| `s14_jump_78_43` | pt | 0.00072 | 0/2/79 97.8 \| 0/13/103 95.9 | - | 0/36/50 80.8 \| 1/37/61 74.2 | - | 0/1/50 97.7 \| 0/18/84 94.9 | 0/5/79 96.9 \| 0/18/103 94.5 |
| `s15_jump_96_34` | pt | 0.00098 | *3/23/105 93.0 \| 0/21/152 94.4 | - | 14/78/222 52.3 \| 14/80/233 52.4 | - | - | *7/71/222 63.8 \| 3/73/233 64.2 |
| `s17_jump_90_37` | pt | 0.0024 | *0/62/244 84.6 \| 0/2/96 97.7 | - | *1/73/236 67.9 \| 0/51/183 73.4 | - | - | *0/70/244 76.3 \| 0/40/183 85.5 |
| `s18_jump_111_46` | pt | 0.0012 | 0/20/231 94.7 \| 0/17/127 94.9 | 5/42/191 84.2 \| 5/42/96 83.7 | 3/44/218 78.0 \| 1/43/119 78.8 | - | - | 3/33/231 88.2 \| 2/32/127 88.1 |
| `s20_jump_78_31` | pt | 0.0042 | 0/25/232 93.5 \| 0/20/193 94.2 | - | 5/57/188 72.7 \| 5/51/178 74.8 | - | 0/47/129 91.6 \| 0/60/134 88.2 | 0/38/232 90.0 \| 0/32/193 90.8 |
| `s21_jump_93_52` | pt | 0.0026 | 0/3/88 97.6 \| 5/21/85 90.9 | 4/13/50 97.9 \| 4/15/54 96.7 | 2/48/224 73.5 \| 9/46/211 71.2 | - | - | 0/38/224 85.3 \| 7/37/211 81.1 |
| `s23_jump_84_28` | pt | 0.0043 | 1/43/218 88.1 \| 0/37/215 89.1 | 7/79/216 79.5 \| 7/78/214 79.0 | *2/118/224 69.0 \| 1/61/218 77.3 | - | 2/7/78 97.5 \| 0/16/80 95.1 | 3/78/233 80.4 \| 3/65/231 81.5 |
| `s24_jump_90_16` | pt | 0.0016 | 2/39/224 86.5 \| 1/39/209 86.1 | 9/37/187 75.7 \| 9/37/190 75.6 | 15/56/194 52.7 \| 15/49/191 53.6 | - | - | 7/37/238 77.9 \| 7/34/209 79.4 |
| `s26_jump_39_37` | pt | 0.16 | 2/15/243 95.4 \| 0/7/91 97.0 | *3/41/241 87.4 \| 1/13/59 96.6 | 13/44/240 60.5 \| 12/41/106 62.5 | - | - | *5/40/243 76.6 \| 2/34/106 79.4 |
| `s27_jump_99_46` | pt | 0.0042 | 0/3/84 98.0 \| 0/3/84 97.9 | - | 6/40/217 69.8 \| 4/39/115 72.4 | - | - | 0/32/217 84.5 \| 0/31/115 85.7 |
| **pooled** | | | ***0/34/246 91.4 \| 0/18/226 94.5** | **6/41/241 82.4 \| 6/36/232 83.3** | **9/67/240 62.6 \| 9/62/233 64.0** | **0/32/231 90.8 \| 1/19/109 93.6** | **0/9/160 95.8 \| 0/38/164 92.6** | **3/45/246 82.7 \| 3/38/233 84.5** |

**Reading: every class of remaining difference, named** (`*` cells; the rest is at or inside stock's ceiling):

- **Seam G's own items are at the ceiling.** Shadowed ground: tiny9v pooled shadowed_land 0/5/234 97.5 % against stock's
  97.1 % (the models' shadows from the tap darken the server's ground as stock's: `w2` 93.0 / 92.0 %, `s1` 98.6 / 98.7 %);
  styled bare ground (0341): tiny9v bare_asia 98.7 / 98.0 %, huge bare_africa 95.8 / 92.6 % pooled, medians 0; the grid
  picture `g1` (hex grid on, key G) equals `s1` within 0.1 point in every class; ice floes are black (equal) in every
  pole difference image. **3019's two cameras (decision 5), live**: `w1` all_kept 5/35/191 80.7 % against 80.4 %, `w2` 0/41/208
  88.6 % against 88.1 %: at stock's ceiling (the visible-land offset of median 4-5 that 0168's acceptance still saw,
  fog-of-war.md 8.4, is gone on tiny9v: bare and shadowed land medians 0).
- **Intended: the reflected ice (decision 3)**: the ice class at the poles, tiny9v `p1` 0/24/222 91.4 % against 94.7 %, huge
  `p1` 0/47/231 89.0 % against 93.3 %. The floes are equal; what differs is the water between them (stock's own animation is
  in the ceiling) where stock reflects the floes and the server's reflection target is transparent black [I: the
  reflection not separated per pixel]; on huge `p1` also the cloud band along the map's top edge over ice plots (next item).
  `polar-ice.md` 12.
- **Other seams' named differences**: (1) the farm field layout and route curves (0166 P6): farm_route on every camera with
  farms (tiny9v `w1`, huge pooled 62.6 / 64.0 %), and route curves that spill into neighbouring shadowed_land plots (huge
  `s05`, `s11`); (2) the `IMPROVEMENT_OBELISK` plazas around Thebes and Pi-Ramesses that the server bakes and stock's stale
  caches do not show (`terrain-decals.md` 16.4 / 17.3, the MC's call): huge `s00` (shadowed_land 75.9 % against 97.8 %),
  `s01`, `s02`, `s15`, `s17` (84.6 % against 97.7 %); (3) tiny_0008 (every land plot within 2 of an unexplored plot,
  13.4): every land class median 3-10 against 0-3, the joined land darker and less blue than stock (bare_asia signed mean
  RGB -7 / -7 / -14 at `c5`, -10 / -11 / -16 at `c1`; stock2 against stock1 within 0.9), the large differences on the
  cloud edges: the fog's cloud-plane phase and the FOWLevel-3 particles that the server does not draw (0168's
  acceptance, fog-of-war.md 8.4; ticket 0340) [I: the haze not separated from the near-camera pair's BC1 rounding, 0332 /
  0371, which 0306 measured at median 2-3 on near tiles]; (4) the cloud band at the map's edge on huge `p1` (0168's phase).
- **Not world rendering**: an EXE interface panel (the unit icon list over the unit panel) is taller in stock than in the
  joined pictures at huge `s05`, `s11`, `s26` and lies outside the interface mask (shadowed_water p95 41 against 13 at
  `s26`); both arms ran the same Lua (units asleep); the EXE draws it [L picture, cause I].
- **Finding (not intended): the terrain's wrap copies are 1 world unit off.** tiny9v `x1_seam_55_14`: bare_america (the
  ground across the seam, x = 0..2, drawn as wrap copy +1) median 11, 68.9 % within 16 against stock's 0 / 96.4 %, with
  equal mean colour (signed mean RGB +0.1 / +0.3 / +0.3) and equal detail: the picture there is the same ground **4 px to
  the right** (mean |difference| over a 400 x 200 crop 10.16 unshifted, 2.15 at +4 px, 2.82 at +5 px; stock2 1.17
  unshifted) [L]; one world unit along x is about 4.8-5.0 px there (the camera block's matrices) [I], so the shift is one
  world unit. Cause
  [C]: `render/src/terrain/terrain.cpp` line 1527 (since 4bb8ca72, 2026-11-11) places the wrap copies at `samplesX * sx` =
  3584 world units on Tiny, while the EXE's view 1 is view 0 moved by the wrap period 3583 (0xD90FE4, section 11 S3,
  `overlay-system.md` 4) and the water module uses `(samplesX - 1) * sx` = 3583 (`water.cpp` worldFromGrid); the same
  `wrapW` feeds `overlayProjForCopy` and `shadowMatrixForCopy`. On huge (8,192 against 8,191) the seam camera `x1` shows
  ice and water there, both at the ceiling. A fix (outside this task: no product code) is the wrap period of the camera
  block or `(samplesX - 1) * sx` for `wrapW`, with the terrain test of the wrap copies against an EXE view pair; the
  lead names the ticket. Picture `vc-20261115-195946-workstation-6fab` (result fail).

**Pictures** (Visual Checks, `--issue 4311`): 33 ACCEPTANCE entries (`--what` starting `ACCEPTANCE: epic 0169 ...`, the
build and settings named, stock1's picture as the reference, `same` where the camera blocks agree within 0.01), each
amended by `render/evidence/0169/4311/amend_4311.py` with its difference image, the per-class J | C figures and the
naming above: 32 pass, 1 fail (tiny9v `x1_seam_55_14`); 33 passthrough mask frames and the dry arm's 4 pictures,
DIAGNOSTIC; 0 undescribed. Examples: pole `vc-20261115-195834-workstation-4bb7`, grid on `vc-20261115-200036-workstation-8cb7`,
tiny_0008 capital `vc-20261115-200429-workstation-b9c5`, huge pole `vc-20261115-201137-workstation-3d35`, huge Thebes
`vc-20261115-201228-workstation-4bf1`.

### 13.7 Done B on the huge save, product level (task 4311, 2026-11-15 20:21-20:40 EST) [L]

`sh shell/evidence/0169/4311/b_4311.sh c5h-h` (`run_4311b.py`: 4304's prep / run / restore with this build): the huge
save `C5H-0169-4311-HUGE_0350` (copy of c5h-i's `C5H-0166-HUGE_0350`, C499D3C6..., turn 350, 128 x 80, wrapping), the
product build of 13.6, D33 (darkred list, Overlay on, D93), ShadowLevel 3, VT `45b3b433...`; **one test switch of the
composite, `shadow_verify=10`** (a check, not an acceptance picture: no picture taken), every other composite key as
shipped, server.cfg defaults. Two runs (20:21-20:28 and 20:33-20:40; the first found the ice fields' per-process
pointers below and had no maximum-zoom camera); figures of the second unless named. Data: `F:/c5-64bit-work/0169/4311/b_huge/`
(run 2) and `b_huge_run1/`. Tiny results of B are 4304's (11.9), 4305's (12.6) and 4307's (`polar-ice.md` 9): not redone.

- **(1) Shadow.** Texels: **564 VERIFY reads, each 8,388,608 bytes (2048 x 2048 R16_UNORM), 0 bytes differing** (the
  composite's byte compare of the published slot against the EXE's ShadowBlur in the same Present; run 1: 361 of 361);
  the tap's counters at the end of the stimuli: 4,890 copies, verifyCount 489, verifyDiffer 0, faults 0, descChanges 0,
  missed 13 (lags, D132; run 1: 37). ShadowMatrix0 per view, numViews, quality and strength at **6 still cameras**
  (`shadow_probe.py` and at once the tap's newest slot): the load camera (101, 25) 2 views, the seam (0, 40) 2 views, the
  south (74, 3) 1, the north (74, 76) 1, the map's middle 6 wheel ticks out 1, **the seam at the maximum zoom (20.5)
  2 views**: every matrix **bitwise equal** (max relative difference 0.0, within the 1e-5 asked), quality 3 = 3,
  strength 1.0 = factor 1.0, numViews equal. **The huge map never has 3 or 4 views** [L]: g_NumViews is 2 within reach of
  the seam at every zoom up to the maximum and 1 elsewhere (as Tiny, S3); "3-4 views" of the brief cannot occur on this
  save (a third view would need both neighbouring copies on screen at once, wider than 8,191 units). In the server: 187
  ACTIVE `shadow 1s:` lines, in every one `frames_bound` = the frames drawn and `frames_none` 0; views 1 and 2 seen;
  refused / stale / unmatched / bad / failed 0; 0 LOUD lines. The server's own verify (verifyOk / verifyBad of the reader)
  is counted but not printed by the scene's lines: the texel equality here is the composite's compare.
- **(2) Ice**: polar-ice.md 12.2 (the real tap's 8 chunks ACTIVE and drawn, 24 draws per frame; the image equal to 4307's
  reference set).
- **(3) Styles** (`render/evidence/0169/4305/styles_fnv_4305.py --server-log`, 4305's method, on the running game):
  the server's line `residency: tile styles of load seq 3: art types 128x80 from the feed's table (table generation 1),
  tiles Generic 7535 America 151 Asia 111 Africa 1509 Europe 1446, fnv64 7c3745f6013ee421` equals the 4-plot rule over
  the DLL's art types (fnv64 7c3745f6013ee421) **bit for bit**; against the EXE's tile+0x138 on all 10,752 tiles (128 x 84)
  **10,748 equal, 4 differ**: tiles (16, 49), (16, 50), (15, 52), (18, 53), the rule Generic and the EXE Europe (+0x138 =
  0x11), the "fifth plot" tiles of 3406 as 4305's 3 of 2,240 on Tiny (12.6); EXE counts Generic 7531 / America 151 / Asia
  111 / Africa 1509 / Europe 1450. Both runs identical. *2026-11-16 (0383, 12.7): the four are fifth-plot tiles; the
  corrected rule gives exactly these EXE counts (all 10,752 tiles equal on 3406's probe of the same map).*

### 13.8 The wrap period (task 4314, 2026-11-15 20:55-, worker 4314, branch `task-4314-wrap-period`)

Written by worker 4314 (lead-0169) from 667d029f; resolves the finding at the end of 13.6 (the terrain's wrap copies one
world unit off stock). Static reading only for the rule (the llvm-objdump listing of the DX11 EXE of
`reverse-engineering-setup.md` 2, read only; described in my own words, D83), 4302's probe reads for the numbers.

**The rule [C, listing; L, values].** MapSetup 0x535BF0 (called at load stage 1 from 0x591270 with (mapW, mapH,
wrap), wrap = 1 when the map wraps in x, else 0) stores the wrap flag at 0xD90FBC and then, by wrap type: types 0 and 1
(every game load) **[0xD90FE4] = float(64 * mapW - 1)** (`shl 6`, `dec`, `cvtsi2ss`, at 0x535E67 / 0x535F03); type 2
(only from a second caller, 0x6E83E9, which passes 2; not a game load [I]) float(64 * mapW - 65) (0x535DC5). Then the
world box: min x = -0.5 * [0xD90FE4] (0xD90FCC), max x = +0.5 * [0xD90FE4] (0xD90FD8), height [0xD90FE8] =
(64 * mapH - 1) * 0.866 (0xD90FD0 / 0xD90FDC its halves). So **the wrap period, the world box's width
[0xD90FD8] - [0xD90FCC] and `water::worldFromGrid(g).width` = (samplesX - 1) * sx are one value**: 3583 on Tiny (56),
8191 on Huge (128), bit for bit (sx = 1.0 exactly; `camera-and-picking.md` 9 [L], 4302's probe `grid.wrap_period` 3583.0
and box -+1791.5 [L]). UpdateViews makes the wrap views from it (0x51E052, `camera-and-picking.md`): 4302 read view 1 =
view 0 moved by 3582.99975 (k = -1 west of the centre, +1 east, 11 S3).
**Why the field gives one unit more:** the height field has 64 * mapW samples per row at x = i * sx - ox, ox = 32 * mapW
(sample 0 at -1792, the last at +1791 on Tiny): the samples span 64 * mapW - 1 units, and the EXE takes that span as the
period, not the sample count. The terrain's last tile reaches sample 64 * mapW (= sample 0, the height page wraps) at
x = +1792, and copy +1 starts at -1792 + 3583 = +1791: **the copies overlap by one sample interval in stock**, the ground
repeats every 3583 units while the height texture repeats every 3584 samples (the column between the last sample and
sample 0 is drawn by both copies; depth decides). The module's `samplesX * sx` (3584, since 4bb8ca72, 2026-11-11) was
the sample count.

**The fix (commit a2f2f518).** `c5r::terrain::wrapPeriod(samplesX, sx)` = float(samplesX - 1) * sx (0 for no samples;
`render/include/c5r/terrain/terrain.h`), used by `Renderer::draw` as the copies' shift k * wrapPeriod (TileWorld and the
cull box) and as the `shift` of `overlayProjForCopy` / `shadowMatrixForCopy` for OverlayProj and ShadowMatrix0 of copies
-1 and +1. Nothing else of the draw changed: copy 0 has shift 0 and binds the EXE's bytes, a map that does not wrap draws
copy 0 only (`copies = 0`), and the water placeholder quad keeps its old extent (`samplesX * sx` either side). The scene
(`srv_render_scene.cpp`) needed no change: it takes each copy's matrix from the EXE's views as written (matched with the
camera block's period, 3583) and the module now shifts its ground by the same period. Not changed, outside this task:
`render/src/bakeresidency/residency.cpp` (line 720) culls the wrap copies with `samplesX * sx`; its box has one sample of
margin, so the one-unit offset can at most keep or drop a tile at the very edge of the view [I, not measured].

**Tests** (`render/tests/terrain/terrain_test.cpp`, all of `c5r_terrain_test` passing, 52 tests, 2026-11-15 21:12 plain):
- `wrap_period_is_the_exes`: float(64 * mapW - 1) bit for bit for mapW 40, 56, 66, 80, 104, 128; 3583 / 8191; box
  -+1791.5; never samplesX * sx.
- `wrap_copy_matrix_equals_the_exe_view_pair`: 4302's probe reads `03_seam` (camera x = -1760.7, k = -1) and
  `05_seam_east` (x = +1760.7, k = +1), Tiny `C5H-0169-4302-TINY_0009V`, zoom 14.5, two views each (numbers as IEEE bit
  patterns in the test, D83): Translation(-k * wrapPeriod) * view 1's matrix against view 0's, relative to the largest
  element: **ShadowMatrix0 1.19e-7 (both), OverlayProj 2.47e-6 / 3.71e-6** (view 1's row 3 z is 0.50001) at 3583,
  within the 1e-5 asked; at 3584 5.58e-4 to 5.79e-4 (the defect, shown in the test's output).
- `draw_without_wrap_ignores_the_copies`: a map that does not wrap, overlay and shadow bound: the picture and world
  positions bit-equal under other copy matrices ([0] and [2]), no ground east of samplesX * sx - ox.
- Updated for the period: `draw_wraps_in_x` (copy +1's surface at c - 255 on the 4 x 4-plot field; the shared column
  [255, 256) accepted on either copy's surface: 19,200 px covered, 9,840 beyond x = +128, 0 off the surface; at the old
  shift every copy +1 pixel would be one sample off, about 0.06 height units on this sine against 0.005 allowed [I]),
  `draw_overlay_wrap_copies_take_their_view_matrix` (W = wrapPeriod: copy 0 7,800 px, copy +1 7,800 px, 0 off).
- Whole render suite (`render/build.py --test`, commit 8656dc8e, 21:26-21:33): 32 of 33; `c5r_bake_test`'s
  `game_gpu_timing_modes` (deferred mode: 0 of 24 bakes reported a GPU time) failed while the seam game ran on the same
  GPU and passed alone at 21:55 (bake module, untouched here). Under ASan (`--asan --test`, commit 6faa1d23, 21:56-22:08):
  **33 of 33, 0 ASan reports**. Logs `F:/c5-64bit-work/0169/4314/build_logs/`.

**The seam retaken, ACCEPTANCE [L, 2026-11-15 21:32-21:39 EST, workstation, instance c5h-f (F:, Goldberg; VT folder
hash 45b3b433..., 55 files, the stock sessions' asset set, D94), ShadowLevel 3].** Build: the product build
`C:/c5-64bit-work/0169-build/x64-04b1211b-p26a2fe588-0169` (commit 26a2fe588 of this branch, clean; server.exe
0C322425..., proxy CAACB6DA..., composite A80E8949..., shipped shell_composite.ini AFB0491B..., x64 DLL 29F58E17...;
laid out by `shell/evidence/0169/4314/layout_4314.py` as 4310's, manifest `shell/evidence/0169/4314/product_build_manifest.json`,
checked by `check_build_4314.py`: ALL EQUAL). Settings as 13.6 (4311's), no test switch. Driver `sh
shell/evidence/0169/4314/joined_4314.sh c5h-f tiny9v` (4309's `run_4309.py` through `run_4314.py`: arm `fix4314`, its
own picture folder beside stock1 / stock2, the ACCEPTANCE text of 4314; the whole tiny9v plan in order): rc 0, 6 of 6
pictures and passthrough frames, 0 cameras moved (joined vs stock1 camera block at most 0.0038 world units), restore by
hash ok (15 files equal or absent as before), `provision.py verify c5h-f` exit 0. In the run: shadow tap INSTALLED, ice tap
READY, the server's last `shadow 1s:` line ACTIVE with `unmatched=0`, refused / stale / bad / failed 0, `frames_none 0`;
0 LOUD lines in render.log, server.log, vpproxy.log and the composite log. Then `python -I
render/evidence/0169/4309/compare_4309.py --arm fix4314 --maps tiny9v` and `tables_4311.py --arm fix4314`; output (game
data, D83) `F:/c5-64bit-work/0169/4309/compare/fix4314/` (`tables_fix4314.md`, `tables_4311_fix4314.md`, difference
images), pictures `F:/c5-64bit-work/0169/4309/captures/tiny9v/fix4314/`, run logs copied to `F:/c5-64bit-work/0169/4314/`.

tiny9v, cells `J | C` as 13.6 (J = this build vs stock1, C = stock2 vs stock1, the same mask; * = J further than C):

| Camera | shadowed_land | shadowed_water | farm_route | ice | bare_asia | bare_america | all_kept |
|---|---|---|---|---|---|---|---|
| `p1_pole_44_3` | 1/20/97 93.5 \| 0/25/94 91.5 | 11/37/82 66.3 \| 11/36/86 65.6 | - | *0/28/231 88.1 \| 0/17/223 94.7 | 2/5/12 100.0 \| 0/5/57 99.2 | - | *4/37/231 76.9 \| 3/35/223 80.3 |
| `w1_cam1_3601_33_14` | 0/51/164 92.1 \| 0/56/216 89.1 | 8/24/198 85.2 \| 7/23/195 86.0 | *3/40/58 74.8 \| 0/35/58 79.2 | - | 0/13/143 95.6 \| 0/22/146 93.8 | - | 6/36/198 80.4 \| 6/36/216 80.4 |
| `w2_cam2_3601_51_14` | 0/6/157 98.2 \| 0/16/157 95.1 | 3/21/156 92.4 \| 4/25/150 90.4 | 0/1/1 100.0 \| 0/1/1 100.0 | - | 0/2/158 98.9 \| 0/3/158 98.2 | - | 0/31/211 91.3 \| 0/37/208 90.0 |
| `x1_seam_55_14` | 0/1/131 99.7 \| 0/1/131 99.1 | *2/22/182 91.9 \| 1/15/46 96.4 | 0/1/18 99.9 \| 0/1/1 100.0 | - | 1/15/118 95.3 \| 0/31/146 92.9 | **0/4/148 97.4 \| 0/9/158 96.4** | 3/19/188 93.5 \| 3/19/208 93.2 |
| `s1_asia_42_12` | 0/3/234 98.4 \| 0/3/234 98.4 | 4/10.3/11 100.0 \| 3/3.9/4 100.0 | - | - | 0/1/101 99.9 \| 0/4/182 99.5 | - | 0/3/234 98.0 \| 0/5/234 97.8 |
| `g1_grid_42_12` | 0/3/234 98.5 \| 0/3/234 98.5 | 3/3.9/4 100.0 \| 4/4/4 100.0 | - | - | 0/1/101 99.9 \| 0/4/162 99.5 | - | 0/3/234 98.1 \| 0/5/234 97.8 |
| **pooled** | **0/4/234 98.1 \| 0/5/234 97.5** | **6/28/198 84.6 \| 6/28/195 84.2** | **0/14/58 95.4 \| 0/10/58 96.2** | ***0/28/231 88.1 \| 0/17/223 94.7** | **0/2/158 99.4 \| 0/5/182 98.8** | **0/4/148 97.4 \| 0/9/158 96.4** | **0/29/234 88.8 \| 0/28/234 89.3** |

- **The seam is at stock's ceiling.** `x1_seam_55_14` bare_america (the ground across the seam, copy +1) 0/4/148 97.4 %
  against stock's 0/9/158 96.4 % (4311's run on the old build: 11/36/164 68.9 %); all_kept 93.5 % against 93.2 % (was
  88.1 %). `render/evidence/0169/4314/shift_4314.py fix4314 tiny9v x1_seam_55_14 1850 450 2250 650` (4311's 400 x 200 seam
  crop): best shift **0 px**, mean |difference| **0.53** unshifted (4311's build: 10.16 unshifted, 2.15 at +4 px; stock2
  1.17) [L]. The `x1` difference image is black over the whole bare_america area.
- **What stays outside the ceiling, named (none on the seam):** `x1` shadowed_water 91.9 % against 96.4 % lies on copy 0's
  Honolulu bay, left of the screen, not across the seam: animated seagulls and the shore wave foam, which move between the
  presented frame and the passthrough mask frame (crop of joined / stock1 / stock2 / difference looked at, 2026-11-15) [L
  picture, cause I]; 4311's old build had the same cell (92.8 % against 96.7 %). `p1` ice and all_kept: the reflected ice
  (decision 3, 13.6). `w1` farm_route: 0166 P6.
- Pictures (Visual Checks, `--issue 4314`): 6 ACCEPTANCE, all amended **pass** with their difference image and J | C
  figures by `render/evidence/0169/4314/amend_4314.py`; the seam `vc-20261115-213719-workstation-c32b` (supersedes 4311's
  fail `vc-20261115-195946-workstation-6fab` for the wrap copies), pole `vc-20261115-213549-workstation-d63a`; 6 passthrough
  frames DIAGNOSTIC; 0 undescribed. Not retaken: tiny_0008 (no seam in view, 13.1) and huge (its `x1` shows ice and water
  only, at the ceiling in 13.6; the copies of huge move by 8191 now, one unit less, as the EXE's).

## 14. Done D, E, F, I (epic 0169)

Sections 14.1-14.3 written 2026-11-15 20:50 EST by worker 4312 (lead-0169), branch `task-4312-parity-offline` from
`epic-0169-seam-g` a8a8dd8e; 14.4-14.6 are worker 4313's. All runs on the workstation (indications only for any time,
COMMON 23) with **the one product build of 4310**, `C:/c5-64bit-work/0169-build/x64-04b1211b-p0b0dc14df-0169` (checked
first against `shell/evidence/0169/4310/product_build_manifest.json` by `shell/evidence/0169/4312/check_build.py`: 20 of
20 files equal): server.exe 171545D8..., proxy 1393AE42..., pinned x64 DLL 29F58E17... (04b1211b8), composite 4F69BC1A...
with the shipped `shell_composite.ini` AFB0491B... (shadow_tap=1, ice_tap=1), 32-bit EXE 04CFDC71... of the instance.
Instance c5h-e (F:, Goldberg, VT present, folder hash 45b3b433..., D94), ShadowLevel 3; product path by 3308's prep
(ServerRenderer = 1, D33 with Overlay on, D93; no viewmask marker). Save: C5H-0169-4312-TINY_0009V, a copy of c5h-c's
C5H-3600-TINY_0009V (27420A41..., Tiny 56 x 36, turn 9: ice, units, the wrap). Drivers in `shell/evidence/0169/4312/`
(`live_4312.py` imports 0166's `live_4112.py` and 3308's prep / restore), comparison in `render/evidence/0169/4312/`;
data on `F:/c5-64bit-work/0169/4312/` (D83, D141; nothing of the game committed).

### 14.1 Done D: the parity pair over 30 AI turns [L]

`sh shell/evidence/0169/4312/parity_chain.sh c5h-e p1` (2026-11-15 20:16-20:35 EST), three arms one after the other on the
prepared instance, each a fresh launch of the save copy, eq_stats.py's dump 25 s after the load and after the 30th AI turn
(turn 9 -> 39), the player's end-of-turn choices by 4112's `UNBLOCK_LUA` (the same in every arm: 5 per arm, a worker into an
empty queue at turn 9, then blocking type 3 reported), no player orders (seam G has no player action to drive). Server keys
off through vpproxy.ini `server_cfg_extra=` (TEST ONLY, 0307), composite keys by the driver, the shipped ini put back byte
for byte after each arm (`composite_ini_restored` true in all three).

| Arm | Server | Composite | rc / turns | Keys after the load / at turn 39 | LOUD | The server's 1 s lines at the end |
|---|---|---|---|---|---|---|
| `on` | render_shadow=1, render_ice=1 (defaults) | shadow_tap=1, ice_tap=1 | 0 / 30 | 362 / 439 | 0 | shadow ACTIVE q 3, frames_bound = derived = 23,379, frames_none 0, refused / stale / unmatched / bad / failed 0; ice ACTIVE, 3 copies, 871,065 indices per frame, failed 0 |
| `off` | render_shadow=0, render_ice=0 | shadow_tap=0, ice_tap=0 | 0 / 30 | 362 / 439 | 0 + the proxy's 3 TEST ONLY lines | shadow `NONE(render_shadow=0)`, frames_none 24,185; ice `NONE(render_ice=0)`; composite `shadow_tap: OFF (shadow_tap=0)`, `ice_tap: OFF (ice_tap=0)` |
| `tapsonly` | render_shadow=0, render_ice=0 | shadow_tap=1, ice_tap=1 | 0 / 30 | 362 / 439 | 0 + 3 TEST ONLY | as `off` on the server (frames_none 34,894); the composite's taps run unread: ice READY generation 1, shadow INSTALLED and copying every frame (32,000 copies, CPU mean 0.0009 ms, faults 0) |

**`eq_stats.py` on against off and on against tapsonly: 362 keys after the load and 439 at turn 39, identical, 0
differing, in both pairs** (`F:/c5-64bit-work/0169/4312/parity/p1-eq-on-*-{load,end}.txt`; the arms' logs and `arm.json`
beside them; one row per arm by `d_table.py`). The shadow and the ice change nothing the DLL's game state holds over 30
AI turns, and the taps neither. Finding [L]: with the server's options off, a composite whose taps are on still copies the
ShadowBlur map every map frame (8 MiB at q 3; `missed` counts 7.7 % of frames in `tapsonly` against 6.7 % in `on`, so
`missed` is not about a reader); the taps have no "server not reading" stop. A player cannot reach that state (the
options are test switches, the ini ships both taps on), so it costs nothing in the product.

### 14.2 Done I: offline equals live for the shadow and ice members [L]

`sh shell/evidence/0169/4312/offline_chain.sh c5h-e` (20:35-20:45 EST). Two live recordings on the product path, each a
fresh launch with the scene recorder's test keys (`render_test_record=DIR`, `_after=1500`, `_frames=8`, `_dump=all`), at
once after the load the auto unit cycle off and the player's units asleep (4309's rule), `UI.LookAt(44,3)` at the load zoom
(the south pole, the ice of the real tap in view), held until render.log said the bundle finished (the span started 50 s
after the LookAt in both); one harness picture each after the span (Visual Checks `vc-20261115-203812-workstation-37ed`,
`vc-20261115-204141-workstation-c528`, DIAGNOSTIC, 4309's stock pole picture as the approximate reference):
- **r1, the product** (decals on; recorder keys only): frames 1593..1622, 8 dumped, 0 record errors, 8 `shadow/*.shtx`
  (0 shared: the texels change every frame), `ice/gen0000000001.ice` (1,070,310 B, the real tap's generation 1).
- **r2, the control** (the same plus `render_decals=0`, TEST ONLY): frames 1625..1653, 8 dumped, 0 record errors.
Bundles on `F:/c5-64bit-work/0169/4312/bundles/r{1,2}` (334 MB each). `sceneshot.exe` built from this branch (=
0b0dc14df's `server/` and `render/`; `build_sceneshot.py`, release), `--dumped --compare --diff`, the bundle's adapter
(RTX 4080), the bundle's install (c5h-e), tolerance 0:0:

| Replay | Colour against the live dump (per frame, of 3,603,694 px) | Depth | MISMATCH lines | Exit |
|---|---|---|---|---|
| r2 | **0 px differ in all 8 frames** | byte-equal, 8 of 8 | 0 | 0 |
| r1 | 34,725-34,820 px (0.97 %) differ, largest channel difference 62, p95 over the differing 28; 0 in alpha | byte-equal, 8 of 8 | 0 | 10 |
| r1 `--set render_shadow=0` (teeth) | 38,778-38,871 px | byte-equal | 17 (`a shadow triple bound ... live 1, replay 0`, `a shadow map bound ...`) | 8 |
| r1 `--set render_ice=0` (teeth) | 1,683,517-1,683,720 px | differs | 9 (`the ice drawn ... live 1, replay 0`) | 8 |
| r2 `--set render_water=0` (for the mask) | about 1.80 M px | differs | 17 | 8 |

So the shadow and ice members read back and play exactly: **without decals offline equals live byte for byte (colour and
depth, 8 of 8 frames)**, the recorded flags (shadow triple bound, map bound, ice drawn) are reproduced in every frame,
and dropping either member is caught by the flags and by the picture.

**Inside and outside the decal tiles (r1)** (`render/evidence/0169/4312/compare_4312.py`, output
`F:/c5-64bit-work/0169/4312/offline/compare_4312.json`). The decal-tile mask K is taken independently of the replay: the
pixels whose colour differs between the two live runs (r1 with decals, r2 without; frame k against frame k, same camera:
their depth differs by at most 0.003, scattered: [I] the water's animated surface), minus the water (where r2's replay with
`render_water=0` differs from r2's, +2 px), whose own animation differs between runs. K holds 9.3 % of the pixels
(about 333,000 per frame), the water 51.4 %, the rest 39.3 %. Over the 8 frames (28.8 M px):

| Class | Pixels | Of r1's differing pixels | Equal share (live = offline) |
|---|---|---|---|
| K (land pixels the decals change) | 2,667,639 | 187,139 (67.3 %) | 93.0 % |
| water | 14,817,683 | 90,646 (32.6 %) | 99.39 % |
| rest (neither) | 11,344,230 | 403 (0.14 %; 50-52 per frame, largest difference 4, all in rows 0-153: the far, most minified band) | 99.996 % |
| all outside K | 26,161,913 | 91,049 | 99.65 % |

The differences in the water are decal pixels seen through the water [I: r2, recorded without decals, replays byte-equal
in the water too, so nothing but the missing decal input separates r1's replay from its live frame there; river beds and
coast decals under shallow water]. The 50 px a frame outside both lie in the far band (rows 0-153), differ by at most 4
and were not traced. The share differs from 4308's 17.8 % because 4308 recorded at
the load camera over the capital (roads, farms, city grounds), this at the pole (few decals). The gap is 0364's (a decal
member for the bundle), not the shadow or the ice.

Not verified: the shadow member over a frame with many unit shadows (the pole has few casters; 4308's load-camera bundle3
showed 18,788 px that differ only without the shadow members); the bundles' `save.name` is the bundle id (the recorder's
stamp was not run), `turn` 0.

### 14.3 Done G, the rest: the shell composite under ASan [L]

`sh shell/evidence/0169/4312/suites_4312.sh build run` (2026-11-15 19:50-20:16 EST, through quietlock.py run, nothing else
of this worker running): `shell\build.cmd asan` of this branch (= 0b0dc14df's `shell/`; composite, stand-in, test hosts,
`/fsanitize=address`), the readers the files need in their ASan variants (`test_reader_build.py --asan`,
`build_water_tests.py --asan`), `ASAN_OPTIONS=log_path="<dir>/asan":log_exe_name=1:halt_on_error=1`:
`test_overlay_tap.py`, `test_decal_tap.py`, `test_water_tap.py`, `test_bake_tap.py`, `test_build_cmd.py`,
`test_errors.py`: **186 passed, 0 failed, 0 skipped, 0 ASan report files**, 24 min 55 s (an indication, 3-5 other heavy
jobs of other teams registered). No timing test failed, so no rerun was needed (0166's 4112 saw 0194's
`test_late_query_is_counted_stale` fail under load). With 4310's ASan pass (`test_shadow_tap`, `test_ice_tap`,
`test_standin`: 83/83, 0 reports) every shell test file the epic names has run once under ASan. Logs:
`F:/c5-64bit-work/0169/4312/suites/`.

Instance after the runs: c5h-e restored by 3308's restore (backup restored by hash, `backup_restore_ok` true, 9 entries
equal and 6 created files absent again, vpproxy.ini back to its recorded hash, set-proxy `--original` rc 0, front end
restored rc 0, save copies removed, `provision.py verify` rc 0), claim released 20:48 EST.



### 14.4 Nothing regressed (done E): 30 AI turns on the huge save, shadow and ice on against off (task 4313, 2026-11-15)

**Run [L, 2026-11-15, workstation, instance c5h-d (F:, Goldberg), claim 0169-4313].** Driver
`shell/evidence/0169/4313/live_4313.py` (the turns / esum part of 0166's `live_4112.py` over `shell/evidence/3308/run_3308.py`'s
prep, run and restore), chained by `turns_chain.sh c5h-d e1 30`; table by `e_table.py` (`e_table_e1.jsonl`); raw files
`F:/c5-64bit-work/0169/4313/huge/<arm>/out`. The one product build `x64-04b1211b-p0b0dc14df-0169` (manifest checked: 20 of
20 files equal): server.exe 171545D8..., proxy 1393AE42..., composite 4F69BC1A... with the shipped ini (AFB0491B...:
`shadow_tap=1`, `ice_tap=1`, `decal_tap=1`, `overlay_tap=1`), pinned x64 DLL 29F58E17...; 32-bit EXE 04CFDC71...;
VP64Options.ini ServerRenderer = 1, D33 + Overlay (D93), no viewmask marker, the composite keys of run_3308 (ring + fence,
lockstep, depth) with `shadow_verify=0`; save `C5H-0169-4313-HUGE_0350` (a copy of the instance's `C5H-VP8P-HUGE_0350`,
C499D3C6..., 128 x 84, turn 350), ShadowLevel 3 (the instances' setting), Vibrant Textures present (D94). No picture.
- **on** (19:50-20:08 EST): the shipped ini and the server.cfg defaults (`render_shadow=1`, `render_ice=1`), no test key.
- **off** (20:08-20:28 EST): composite `shadow_tap=0`, `ice_tap=0`; `render_shadow=0`, `render_ice=0` through vpproxy.ini
  `server_cfg_extra=` (TEST ONLY, 0307).
Each 30 AI turns (350 -> 379, `turns.py end` / `wait`), memory after the load (25 s settle) and after each turn, lockstep
slices per turn, the harness's stop with the turn hook (rc 0 both). The 30 turns took 13.3 / 15.6 min: a 30-turn huge run
fits a place easily.

**The machine was not quiet** (`quietlock.py check` before each arm: NOT QUIET, 4 reasons: account B's 4203 games on c5h-a
and c5h-b, 4311's game on c5h-h, 4312's ASan shell build). The on/off differences of time, lockstep, frame rate and GPU below
are therefore indications only (COMMON 23), and even the direction of a difference is not established by one pair.

| | on (taps and render options on) | off (both off) |
|---|---|---|
| EXE crashes / server crashes / stop rc | 0 / 0 / 0 | 0 / 0 / 0 |
| LOUD lines (render.log, server.log, vpproxy.log, this process's shell_composite.log) | **0** | 3, all the counted TEST ONLY lines of `server_cfg_extra` (vpproxy.log) |
| gray frames: before the bake-ahead / after it (0139's count) | 26 / **0** | 23 / **0** |
| bake-ahead (radius -1) | 4,922 tiles, 20.3 s after the field, 5,090 bakes, 0 failed | 4,922 tiles, 19.9 s, 4,964 bakes, 0 failed |
| terrain frames drawn, stale, draw errors | 109,893, 0, 0 | 95,887, 0, 0 |
| shadow (render.log `shadow 1s:`, last line) | NONE 21 s -> WAITING 78 s -> ACTIVE 859 s; q 3, 2048^2 fmt 56, 2 views (derived 109,893); **frames_bound 109,893 = every frame drawn**, frames_none 0, frames_q0 0; triples 94,317; refused 0, stale 0, unmatched 0, bad 0, failed 0 | `NONE(render_shadow=0)` in all 1,086 lines, frames_none 95,887 |
| shadow tap (composite) | 20 patterns match; INSTALLED 19:53:27 (load seq 3), 3 slots 24.0 MB; **96,000 copies by 20:07:33, missed 4,116 (lags, D132: 3 logged, then counted), faults 0, description changes 0, 0 LOUD** | `OFF (shadow_tap=0)` |
| ice (render.log) | ACTIVE from the load: generation 1, load 3, **8 chunks**, meshes 1, drawn in all 109,893 frames (3 copies, 24 draws, 3,849,966 indices per frame), mesh_refused 0, draw_err 0, failed 0 | `NONE(render_ice=0)` |
| ice tap (composite) | 21 patterns match; one generation (one load): VB 2,198,304 + IB 2,566,644 bytes, 427,774 triangles, image 4,765,732 bytes, acked by server.exe; 0 LOUD | `OFF (ice_tap=0)` |
| lockstep exact / older / none (30 turns), giveups = recoveries | 88,298 / 9,950 / 0 (89.9 % exact), 204 | 81,268 / 6,714 / 0 (92.4 %), 169 |
| turn wall time median / max | 26.7 / 31.1 s | 31.5 / 40.2 s |
| EXE private bytes after the load / at the end / max | 1,443.4 / 1,624.5 / 1,624.5 MB | 1,451.3 / 1,608.6 / 1,625.0 MB |
| EXE largest free block after the load / at the end / min | 1,855.3 / 1,629.4 / 1,629.4 MB | 1,865.7 / 1,649.3 / 1,641.6 MB |
| server private after the load / at the end | 1,625.3 / 1,708.6 MB | 1,625.3 / 1,726.8 MB |
| server video (render.log `video_local`) after the load / at the end | 1,204.9 / 862.6 MB | 1,081.5 / 817.0 MB |

**Read [L unless marked].**
- Done E's counts hold: 0 crashes, 0 LOUD lines in the on arm, 0 gray frames after the bake-ahead in both.
- **The shadow tap's publications per map frame:** of the composite's map frames after the install, 96,000 were copied and
  4,116 (4.1 %) were lags, each counted and never a fault (the previous copy's query had not signalled; the slot of the
  frame before stays, D132). The server bound a triple in every frame it drew (frames_bound = frames, frames_none 0) and
  took 94,317 new triples; 0 stale episodes, 0 refused. 4308's Tiny runs had 0-4 lags (11.10); the share here is an
  indication of a loaded GPU (the same run's terrain drew about 128 frames a second uncapped).
- **The ice tap's generations: exactly one per load** (generation 1 of load 3, the huge mesh of 8 chunks as 4307 found),
  drawn in every frame, nothing refused.
- **Faults in play that happened by themselves:** none of the shadow tap (faults 0, refused 0, description changes 0) or
  the ice tap (mesh_refused 0, draw_err 0); the 4,116 lags are D132's counted case and the only in-play event.
- **Memory** [L, one pair, indication]: the EXE's private bytes and largest free block are within about 10-20 MB of each
  other between the arms after the load and at the end (on: largest free 1,629 MB at the end against 1,649 MB): the shadow
  slots are 24.0 MB of D3D video memory on the EXE's device (NT-shared), the ice copy's staging buffers live only during
  the copy and its image is written through views of at most 4 MiB that are unmapped at once (`polar-ice.md` 9), so neither
  holds EXE address space in play [C]. The server's video memory after the load is 123 MB higher with the shadow and ice on
  (the opened shadow slots and the ice module's mesh and five textures) and 46 MB at the end [I: not split by resource].
- *Corrected 2026-11-16 (ticket 0385):* the composite's default log now starts per EXE process (an earlier process's
  log is renamed to `shell_composite.<yyyymmdd-hhmmss>.log`; `shell-composite-taps.md` section 1); the note below holds
  for builds before 0385.
- *Driver note* [L]: the composite appends to `Logs/shell_composite.log` across EXE processes (the second arm's copy began
  with the first arm's lines); a summary of one run takes the part after the last `==== shell_composite.dll ... loaded`
  line (`live_4313.py` `last_process`). run_3308's per-turn lockstep slices are not affected (they read from the byte
  position at each turn).


### 14.5 Failure route (done F): an install failure of the shadow tap and of the ice tap, live on oldpc on the shipped pack layout (task 4313, 2026-11-15)

**What forces it without a code change [C].** The shipped composite (`shell_composite.dll` 4F69BC1A... of the product build
`x64-04b1211b-p0b0dc14df-0169`, `polar-ice.md` 11.4) keeps the taps' own test keys: `shadow_test=<mode>` (24 modes of a
synthetic shadow state in the DLL, 11.9) and `ice_test=<mode>` / `ice_test_pattern=<k>` (`polar-ice.md` 9). Each fault is
one such key written into the instance's `shell_composite.ini`; no EXE byte and no other file changes. Chosen: the shadow
`shadow_test=wrongformat3` (quality 3 with a ShadowBlur of format 54 R16_FLOAT: the class "the EXE's texture is not the
one the tap knows", FAILED DESC after `shadow_fail_frames` = 30 map frames) and the ice `ice_test=patterns
ice_test_pattern=0` (the real 21-pattern list checked against a synthetic image with pattern 0 broken: the class "an EXE
the tap does not know", FAILED PATTERN at the tap's start). Both are the stand-in tests' own modes
(`test_install_failure_is_loud_and_final`, `test_each_pattern_refuses_when_broken`).

**Driver.** `shell/evidence/0169/4313/neg_tap_job.py --fault shadow|ice` = `tools/testbed/jobs/release_d125.py` unchanged
with the fault's keys added after its `lay` step (0166's `neg_decal_job.py` pattern), submitted by
`shell/evidence/0169/4313/submit_d125.sh` with `--headed` to oldpc's testbed (instance tb2, the machine's own EXE). Inputs:
the product build's server.exe 171545D8..., proxy 1393AE42..., vp64helper 16B73447..., composite 4F69BC1A... and pinned x64
DLL 29F58E17... (unchanged; `check_build.py`: 20 of 20 files equal the manifest); the pack `4313-d125` by
`tools/release/pack.py --layout <product build>` (x86 = Release-5.4.8's committed DLL 262aee88..., purity 7b1a91fb...),
`pack=1`, `fallback_dll` = that DLL, config.ini in the A'' form by `vp64helper on`; save `TB-TINY_0008` (b2ceb663..., a
copy). Results `F:/c5-64bit-work/0169/4313/d125-shadow` and `d125-ice` (release_d125.json, the fault logs, the report folder).

| Job (oldpc, tb2) | Composite (shell_composite.log) | Server | EXE ended after launch | Checks |
|---|---|---|---|---|
| shadow `20261115-195217-5365` (19:52-20:00 EST) | `TEST MODE shadow_test=wrongformat3`; 19:58:01.227 `shadow_tap: LOUD: FAILED DESC at the install: quality 3: handle 0x22 is 2048x2048, 1 mips, array 1, 1 samples, format 54 (expected 2048 x 2048 R16_UNORM, 1 mip) (for 30 map frames)`; the ice tap beside it READY generation 1 and acked | +38.718 s `render: LOUD: the models' shadow cannot install: the shadow tap could not install: VPSHD_FAILED DESC (reason 3: ...)`; +38.729 `shadow: the 64-bit path cannot go on ... (exit code 5)`; +38.767 `remote: exit code 5 (...)` | 92.0 s | 16 / 16 |
| ice `20261115-200046-3488` (20:00-20:06 EST) | `TEST MODE ice_test=patterns`; 20:04:54.215 `ice_tap: REFUSED: pattern 0 'CreateTerrain: the Ice object TerrainSystem+0x529BC passed to the Ice step 0x5A0BC0' at VA 0x70AF16 does not match - the tap never reads this EXE` and `ice_tap: FAILED PATTERN (load 0, copy 0): ... - DISABLED for the life of the process`; the shadow tap beside it: 20 patterns match | +20.551 s `render: LOUD: the polar ice cannot install: the ice tap failed: pattern 0 '...' at VA 0x70AF16 does not match; ...`; +20.560 `ice: the 64-bit path cannot go on ... (exit code 5)`; +20.590 `remote: exit code 5` | 75.7 s | 16 / 16 |

The 16 checks of each job [L]: the EXE ended by itself (no "Game Crash" box); one report folder with `report.txt` and
no `.dmp`; `report.txt` says `server pid ...: exit code 5` and quotes the server log's last FAILED or LOUD line, which
names the input's reason (shadow: the `remote: exit code 5 (the models' shadow cannot install ...)` line; ice: `result:
FAILED: the polar ice cannot install ...`), so `report.txt` alone now carries the reason (0167's 12.5 (b) found it only
in the tails; the proxy quotes it since); `vp64helper failed` restored config.ini (SHA-256 2e30c008... = the player's
before the run, after the failure and at the end), wrote `failed=1`, left no side file and showed the popup (Visual
Checks `vc-20261115-195807-oldpc-b451` and `vc-20261115-200503-oldpc-0141`, DIAGNOSTIC: the fault is a test key; pulled);
the composite's test-mode line and FAILED line; the server's LOUD "cannot install" line and exit code 5. After each job:
the instance restored by hash (`backup_restore_ok`, `provision.py verify` rc 0), the save copy removed, the host's My
Games unchanged (21,874 files compared), the report folder removed.

Finding [L]: the ice's failure reaches the server before the map (+20.6 s of the server, the tap fails at its start in the
front end; the reader turns it into an install failure as soon as the scene polls it), the shadow's only after 30 map
frames of the loaded game (+38.7 s). Both end the game the same way.

**A fault in play (D132): the stand-in tests are the evidence** (11.9: the texture with a wrong description and the
texture gone in play, a NaN matrix and a strength of 1.5 refused by the reader, q 3 -> 0 in play, a slot in use never
written over; 11.10: `shadow_refused_keeps_last_good`; `polar-ice.md` 9-10: a refused image keeps the last good mesh, a
mesh refused by `setMesh` LOUD once and counted, never fatal). The test modes cannot show it live: the composite's test
worlds count Presents from the process start (0167's 12.5, `overlay_test=lost`), and the real EXE gave no such fault by
itself in the huge run (14.4): what happened by itself there is in 14.4.


### 14.6 Costs (indications from the workstation, COMMON 23; task 4313, 2026-11-15)

All from the runs of 14.4 on a machine that was not quiet (14.4); no timing claim.
- **Shadow tap, per Present (a map frame with a copy)** [L]: CPU in CopyResource + End per copy, mean 0.0015 ms over the
  whole on arm (the 96 windows of 1,000 copies: means 0.0009-0.0024 ms, the largest single copy 0.225 ms); 8 MiB of GPU copy
  per map frame at q 3 (11, S7), whose GPU time is not measured here. No readback (`shadow_verify=0`, as shipped).
- **Ice tap, per load** [L]: one copy per load, nothing per Present in play: huge 1,996.4 ms of wall time over 3 Presents
  (the staging buffers' maps with DO_NOT_WAIT) and 27.1 ms of composite CPU in total (Tiny: 181.5 ms and 6.7 ms in 4310,
  38.1 ms and 5.3 ms on oldpc in 14.5's shadow job).
- **Server** [L]: terrain GPU time per drawn second mean 0.20 ms (max 0.8) on against 0.25 ms (max 1.3) off, about 128
  against 98 frames drawn a second (uncapped server, loaded GPU): no cost of the shadow binding or the ice draw shows above
  the machine's noise. Lockstep exact 89.9 % on against 92.4 % off; turn wall time median 26.7 s on against 31.5 s off (the
  off arm was the slower one: machine load).
- **Memory** (14.4's table): EXE about 0-16 MB of private bytes and 10-20 MB of the largest free block between the arms,
  within one pair's noise [I]; server video +123 MB after the load with the shadow and ice on.
- A timing claim (the shadow copy's GPU cost, the frame rate with and without the taps) needs the testbed's quiet place
  (COMMON 23), not run.

## 15. The terrain draw's CPU: the coarse cull and the per-tile submission (ticket 0707, 2026-11-23)

Ticket 0707 (account B, implementer aaff05fb; branches `task-0707-terrain-stage-cost` and `task-0707-units-begin-2`).
The split that named the parts is pan-coherence.md 5.12 (worker-0673w); the units' part is pan-coherence.md 5.12b.

### 15.1 What `Renderer::drawPass` spent its CPU on [C: code; L: 5.12's diagnostic runs]

`render/src/terrain/terrain.cpp` `drawPass` visits, for each of the 3 wrap copies, every renderer tile of the map (one per
plot: 128 x 84 = 10,752 on the huge save, 32,256 box tests a frame) with an 8-corner box against the 6 clip planes, then
submits each tile not culled: 5 TerrainTile members written by name (`put`, a `strcmp` walk of the layout each), one
constant-buffer map, the VS height page, `PSSetShader` (every tile), 11 PS views, one `DrawIndexed`. The fixed part (the
cull of every tile copy, two thirds of it in the two copies that lie almost wholly off screen) dominated: 0.6 of 0.9 ms
offline, 1.2-1.5 ms live on the laptop; submission 1.2-1.7 us per drawn tile. `drawScreenOverlay` (the Screen Overlay
pass, under the pass's `forest` mark) runs the same loop a second time per frame.

### 15.2 The change [C: code and tests]

- **Coarse cull.** Tiles are grouped in blocks of 8 x 8 (`kCullBlock`; partial blocks at the map's edge). Per draw the
  block's z range is taken from its tiles as the tile test forms it (`min(t.zmin - 1)`, `max(t.zmax + 1)`, recomputed
  every draw from the tiles, about 10,752 min/max: no state to keep in step with `setHeightField` / `updateHeightRegion`).
  Per wrap copy each block's box (the first tile's low corner to the last tile's high corner, the tile test's own
  formulas) is tested against the same six planes; a block is culled only when all 8 corners lie outside ONE plane by
  more than `kCullMargin` = 1e-4 x the scale of the clip values over the box (sum of |coordinate| x |matrix entry|). The
  plane tests are linear and every tile box lies inside its block box, so every tile of such a block is outside that
  plane in exact arithmetic; the margin (about 1,000 x float rounding of a 4-term dot product) keeps the per-tile float
  test from disagreeing. A NaN or infinite matrix makes every comparison false: no block is culled, the tile test runs as
  before. The tile loop keeps its order (rows, then columns); a culled block's run of a row is skipped in one step and
  counted (`tilesCulled` at copy 0, as the tile test counted it). So the tiles drawn, their order, the counters and the
  picture are those of the per-tile test alone.
- **Submission.** The five TerrainTile members are found once per draw (`findMember`, put's checks and messages; a
  failure is returned at put's place, the first drawn tile); the tile's values are `memcpy`'d at those offsets.
  `PSSetShader` is called only when the tile's shader differs from the one the loop bound last (the first tile always
  binds; nothing else in the loop binds a pixel shader), as the hull shader already was.
- **Options / stats** (`render/include/c5r/terrain/terrain.h`): `Options::coarseCull` (default true; false = the tile test
  alone, for tests), `FrameStats::coarseSkipped` (tile visits skipped, every copy) and `FrameStats::drawnOrderHash` (FNV-1a
  32 over (copy + 1, tile index) of each tile drawn, in draw order).
- **Test** `draw_coarse_cull_equals_the_tile_cull` (`render/tests/terrain/terrain_test.cpp`): a wrapped 40 x 26 field
  (a partial block row), 48 cameras (straight down and tilted to 1.1 rad, a third near the wrap seam, some off the map),
  each drawn with the coarse cull on and off: equal `tilesDrawn`, `tilesCulled`, `drawnOrderHash`, `tilesWithoutCache`,
  colour and world-position pictures bit for bit. 48 of 48 cameras culled blocks, 36 of them beside drawn tiles, 33,896
  tile visits skipped [L, 2026-11-23]. Mutation (not committed): a block culled at 6 corners of 8 outside one plane fails
  the test (tiles, counters, order hash and both pictures differ) [L].

### 15.3 Numbers

- **Workstation, offline, an indication (COMMON 23)** [L, 2026-11-23 16:54-16:57 EST, machine busy]: sceneshot with
  `server/evidence/0707/bench0707b.diff` (never merged: each shot's frame drawn 10 more times under a pass timer) on
  HUGE_0350 (8 frames, city view, 76 tiles), one round: the terrain draw's CPU (`FrameStats::cpuMs`) p50 1.80 -> 1.05 ms;
  every shot byte-equal to the server's dump in both arms.
- **Laptop, live, one run per arm in one set** [L, 2026-11-23, laptop clock; jobs `20261123-171019-18fc` (base: main
  364413ddd) and `20261123-171658-0e61` (the terrain cut + the instrument of pan-coherence.md 5.12b), TB-HUGE_0350 t350,
  tb1, 30 fps cap, the `wide` case of `capture_job.py` (30 wheel ticks out, 60 s still, 352 tiles drawn); the keys and
  drags could not run: Desktop Duplication is refused while the laptop's display is idle-off; `split0707.py`]: the
  terrain draw's CPU (`terrain 1s` cpu_ms) **1.22 -> 0.65 ms** per drawn frame; the `forest` stage (the Screen Overlay pass
  runs the same loop) 1.46 -> 0.81 ms; the server's busy time per drawn frame 17.55 -> 16.34 ms (-1.2 ms, -6.9 %).
  Grade C (one run per arm).
- **oldpc, offline, quiet: the PERFORMANCE.md series (grade A)** [L, job `20261123-173648-4fc8`, 2026-11-23
  17:38:28-17:46:38 EST, quiet proof: alone, undisturbed, foreign cores 0.041, CPU mean 17.2 %]: base = main 364413ddd,
  branch = ed1c9ef23 (the terrain cut and the instrument), each with `bench0707b.diff`; HUGE_0350 bundle, 8 frames (city
  view, 76 tiles drawn) x 30 redraws, the first redraw dropped; 6 rounds interleaved, order turned each round; 1,392 draws
  per arm; oldpc's base install, `--adapter hw`, the asset root's MODS (`server/evidence/0707/oldpc_bench0707.py`,
  statistics `bench0707_stats.py`, output `server/evidence/0707/oldpc-4fc8-stats.txt`). Paired branch - base of the
  per-round medians, bootstrap 95 %:

  | part (ms per draw) | base p50 | branch p50 | branch - base | sd of the round differences |
  |---|---|---|---|---|
  | the terrain draw's CPU (`drawPass`) | 0.603 | 0.322 | **-0.281 [-0.287, -0.277] (-47 %)** | 0.007 |
  | `forest` stage (the Screen Overlay pass's `drawPass` + the trees) | 0.531 | 0.244 | **-0.287 [-0.293, -0.282] (-54 %)** | 0.008 |
  | `units` (unchanged code offline) | 1.244 | 1.216 | -0.028 [-0.057, -0.005] | 0.035 |
  | `draw`, `predraw`, `water`, `models` | | | -0.008, -0.006, -0.006, -0.001 | |
  | the whole scene draw (busy) | 4.249 | 3.626 | **-0.619 [-0.689, -0.565] (-14.6 %)** | 0.086 |

  Why it moved: the 32,256 box tests per `drawPass` became 3 x 176 block tests plus the tile tests of the few blocks the
  view touches, twice a frame (the terrain and the Screen Overlay pass); the 76 submitted tiles lost their 5 by-name
  member walks and 75 redundant `PSSetShader` calls. Every shot of both arms drew (exit 0); the comparison with the dumps
  runs on the workstation (not P1 on oldpc's adapter).

### 15.4 Not verified

- The zoomed-out view offline (the bundle's frames are the city view); its live figure is the laptop pair above (grade
  C). The first oldpc submit (job `20261123-170046-56e7`) failed at once: sceneshot exit 9, the bundle's
  `render_decal_mods` is a workstation path; `oldpc_bench0707.py` ships the asset root's `Documents` and passes `--mods`.
- Keys and drags on the laptop (display idle-off). The GPU time is unchanged by construction (the same draws).
- Rule A on the landing branch [L, 2026-11-24 00:58-01:47]: offline-equals-live 17/17 over the C: library (every bundle
  byte-equal or within the library tolerance), render-host 199/199, scenebundle 4/4 + 28/28; builders: render ctest 109/109,
  ASan terrain/units/unitanim 0 reports, U6 and every render test builder linked (server/evidence/0707/acceptance.txt).
