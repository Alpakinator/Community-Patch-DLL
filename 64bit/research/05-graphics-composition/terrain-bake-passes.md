# The terrain bake after the blend: normals, macro shadow, CacheBake, DXT compression, mips (issue 3403)

Written 2026-11-13 from 03:05 EST by worker W3 of team-3400 (epic 3400, bake stage 1). Marks: `[C]` from a static reading
of the DX11 EXE (Ghidra 12.1.4 headless on a read-only copy of `D:/c5-64bit-work/2900/2903/ghidra`, decompiles of
0x694A60, 0x627810, 0x6CF830, 0x515630, 0x63CCD0 and helpers) or from the shader blobs' own token stream (`c5r::shaders`
reflection and the D3DDisassemble text of task 2902, `D:/c5-64bit-work/2900/2902/dump/asm/`); `[L]` read from the
RenderDoc 1.47 capture `D:/c5-64bit-work/3023/captures/c5h-c_20261113-000224_frame380.rdc` (3023 run 3: Goldberg
instance c5h-c, stock, DLL `i0ship-bbfacca1-ia`, save copy `C5H-3023-TINY`, load screen, RTX 4080, shader level 10 = set
5_0); `[I]` inferred. Addresses are VAs at ImageBase 0x400000. Following D83 the behaviour is given in words and tables;
no decompiler output, EXE bytes or game data are committed. Shader blobs are named by their VA in the EXE's `.shader`
section and by the SHA-256 of the blob (the capture's `CreatePixelShader`/`CreateVertexShader` bytes equal the EXE's blob
byte for byte for all 79 shaders the frame uses [L]).

Method: the capture was exported once by 3023 with `renderdoccmd convert -c zip.xml` (`tools/harness/rdc.py`); this
issue parsed that export (`D:/c5-64bit-work/3400/w3/scripts/dumpxml.py`: one line per call per context, every created
object, every shader's bytes) and read constant-buffer and vertex data from the export's buffer archive. The RenderDoc
replay API was not used: RenderDoc 1.47's release folder ships no `renderdoc` Python module (only the one embedded in
`qrenderdoc`, which the harness never starts), see section 6.

## 1. Answer first: the premise check (GPU or CPU, deterministic?)

- **BC1 compression runs on the GPU** [L + C]. The bake renders the lit colour into an R8G8B8A8_UNORM scratch target of
  the tile's size class, generates its mips with `GenerateMips`, then a pixel shader (`CompressBC`, material 41) writes one
  BC1 block per pixel into an **R32G32_UINT** target of a quarter of the size (64 x 64 for a 256 cache), one draw per mip,
  and a `CopyResource` copies that R32G32_UINT texture (6 mips) into the BC1_TYPELESS cache texture tile+0x198 (t0); the
  same again for t8 (tile+0x19C). No `Map` for reading, no staging texture, no `UpdateSubresource` and no CPU encoder
  take part [L: none of these calls touch the caches or the scratch targets in the frame].
- **Deterministic on one GPU and driver** [C for the shader, I for the GPU]: the encoder reads its 16 texels with
  integer `ld` (no filtering, no derivatives), has no UAV, no atomics and no data-dependent loop count, and every
  pixel writes only its own block. Two runs on the same GPU and driver therefore give the same blocks, given the same
  input texels. What can change across GPUs or drivers: the float arithmetic of the lighting pass (`log`/`exp`/`rsq`
  are not bit-exact across vendors) and the box filter of `GenerateMips` (driver-defined), which makes mips 1-5 of the
  scratch target, and from them mips 1-5 of t0/t8. **Measured on this machine (section 7): the server reproduces the
  EXE's t0 and t8 of one tile bit for bit, all mips, twice** [L].

## 2. The ordered pass list of one bake (0x694A60, array mode, size class 256, tile with a spec layer)

Order as the capture executes it (one tile, frame 380; each pass is one deferred-context command list, run on the
immediate context in this order) [L], matched to the code of 0x694A60 [C]:

| # | Pass (the EXE's marker string) | Program, SM5 blobs (set 5_0) | Target(s), format, viewport | States | Draws |
|---|---|---|---|---|---|
| 1 | "HeightMap": the blend (issue 3402) | TerrainShaderScreen pass 2: VS `0x067d9550`, PS `0x067f1e40` | 3 x 512 R16G16B16A16_FLOAT, 10 mips (TR8+0x68c..), cleared 0 | blend ONE+ONE add; depth off; cull none | 1 x 26,136 indices, then `GenerateMips` on all three |
| 1a | "Decal Layer" (roads, farms, river bed, model grounds; only when TR8+0x538 != 0) | TerrainDecalShader **pass 1** (VS `0x067e6360`, PS `0x067f8f30`, three targets) via 0x742EF0 | **the blend's three 512 R16G16B16A16_FLOAT targets above, mip 0, not cleared**, before their `GenerateMips` | colour SRC_ALPHA / INV_SRC_ALPHA, alpha ONE / ONE; cull none | one draw per decal, ascending priority; none in this capture. *Corrected 2026-11-15, epic 0166 (terrain-decals.md 4.1, 11.3, [L] on 7 decal bakes; replaced: "TerrainDecalShader pass 0 via 0x742EF0 into the size class's decal layer target (TR8+0x6c4[i])" [C, not seen live]): no separate decal layer; TR8+0x6C4 is a second binding of the same three textures [I]; pass 0 (two targets) was never drawn* |
| 2 | "Normal Calculation" | NormalCalculate pass 0: VS `0x067d8760`, PS `0x067eab30` | 512 R8G8B8A8_UNORM, 10 mips (TR8+0x694), not cleared | blend off; depth off; cull none | 1 quad (6 indices), then `GenerateMips` |
| 3 | "Macro Shadow" | TerrainShaderScreen pass 3: VS `0x067e4590`, PS `0x067f6698` | 512 R8G8B8A8_UNORM, 10 mips, cleared 0 | blend ONE+ONE add | 3 x 26,136 indices (count = 0xD90F6C = 3), then `GenerateMips` |
| 3a | "Decal Macro Shadow" (when TR8+0x538 != 0) | TerrainDecalShader pass 1 (the same PS) via 0x742EF0, the decals with record pass bit 1 (tree shadows: Tree_Shadow from the Forest, one natural-wonder key) | the macro-shadow target alone | ONE / ONE add | none in this capture; *[L] 2026-11-15, epic 0166 (terrain-decals.md 4.4): one draw of the tree-shadow blobs after the three macro-shadow draws* |
| 4 | "Terrain Cache" (CacheBake) | CacheBake pass 1 (two targets): VS `0x067d8cb8`, PS `0x067ed018`; pass 0 (`0x067ebe78`, one target) when tile+0x2FC = 0 | 2 x 256 R8G8B8A8_UNORM, 6 mips (size class), not cleared | blend off; depth off; cull none | 1 x 26,136 indices, then `GenerateMips` on both |
| 5 | "DXT Compress" x mips, colour | CompressBC pass 0: VS `0x06743180`, PS `0x067434f0` | R32G32_UINT 64 x 64, 6 mips, mip m as target, viewport 64 >> m | blend off; depth off; cull none | 1 quad per mip, m = 0..5 |
| 6 | "Copy Resource" | - | R32G32_UINT (6 mips) -> t0, BC1_TYPELESS 256, 6 mips | | `CopyResource` |
| 7 | "DXT Compress" x mips, second (only when tile+0x2FC) | as 5, source = the second CacheBake target | second R32G32_UINT 64 x 64, 6 mips | | 6 quads |
| 8 | "Copy Resource" | - | -> t8, BC1_TYPELESS 256, 6 mips | | `CopyResource` |

Blob identities (SHA-256 of the blob, equal in the capture and the EXE) [L]:

| Blob | Stage | Size | SHA-256 |
|---|---|---|---|
| `0x067d8760` NormalCalculate VS (also TerrainShaderScreen pass 4) | VS | 456 | `b58b7859cb1b6d87a4592acee4865cfc34bd03eaa020d43495e53fe4e0f5fe9d` |
| `0x067eab30` NormalCalculate PS | PS | 1,688 | `85cfb62354fe5bf505aa01914d7d7763033f22d13d104b4c11026e80c438b8ab` |
| `0x067e4590` macro shadow VS | VS | 876 | `b002c39805daf0ab8b29d463760c108ae8ab76d9919368e6a8b4381004f309c5` |
| `0x067f6698` macro shadow PS | PS | 1,512 | `3b0e7364efa16a0df1f9a515a361253f3f404fda9f37d847a88302b21548307e` |
| `0x067d8cb8` CacheBake VS | VS | 732 | `268904fbcd3f8a06f3cd98151a778c56203266c7efa2b4e1bf92fb0d6f6eacee` |
| `0x067ed018` CacheBake PS, two targets | PS | 2,076 | `3184290dbd9d73dd5c67ad2a82961190e18e00fcba16d376eaa4f6a8d13d78ed` |
| `0x06743180` CompressBC VS | VS | 292 | `a7ee14599502673ff4a30bbcfba1e596e179180d21a63f50da3084d413f18f39` |
| `0x067434f0` CompressBC PS (pass 0) | PS | 7,408 | `d8f24708cf4b38691cec02978a9f716389063cbcac730a413f16a1bb9fad7670` |
| `0x067d9550` blend VS (for reference) | VS | 4,000 | `deb732edce6a0767185f8b522bae53a92c3c5e7296091c365b6f5dbb0eadf40a` |
| `0x067f1e40` blend PS (for reference) | PS | 6,300 | `9b6410c808c30199badc546aeed70574c8624504ae9a6bfdafaaf80c55eed259` |

States (the capture's objects) [L]: "blend off" = BlendEnable false, write mask all; "add" = ONE/ONE/ADD for colour and
alpha, IndependentBlend off. Depth-stencil: DepthEnable false, StencilEnable false. Rasterizer: solid, CULL_NONE,
FrontCounterClockwise true, DepthClip true, no scissor, no MSAA. Topology triangle list. Viewport origin 0, depth 0..1.
Samplers: **A** = MIN_MAG_MIP_LINEAR wrap (the EXE's sampler id 0), **B** = MIN_MAG_MIP_LINEAR clamp (id 5), **C** =
MIN_MAG_LINEAR_MIP_POINT wrap (id 1); all MinLOD 0, MaxLOD FLT_MAX, bias 0, aniso 0, comparison NEVER.

## 3. Each pass in detail

### 3.1 Normal Calculation (pass 2)

- Input t0 and t1: the blend's **Normal temp** (blend target 1, SRV over all 10 mips), sampler B in s0/s1 [L]. The PS reads
  only t0's red channel [C]: a 3 x 3 Sobel filter over the red channel (the blended height of the layers) at offsets
  of one texel step, times half the strength, gives a slope (dx, dy); the output is n = (-2 dx, 2 dy, 4) / |(-2 dx, 2
  dy, 4)|, packed as `n * 0.5 + 0.5`, alpha 0 [C].
- Constant buffer slot 1 = a 32-byte shared buffer (0x597520 maps id 0x20): register 0 = texel step (1/512, 1/512, 0, 0),
  register 1.x = strength **16.0** (`0x9EFBA8`) [L values, C source]. Slot 0 is unbound (null) [L].
- Geometry: a full-target quad, an immutable vertex buffer of 4 vertices x 32 bytes (POSITION f32x3 in clip space,
  NORMAL f32x3 = the position again, unused; TEXCOORD0 f32x2): (1, 1) uv (1, 0); (-1, 1) uv (0, 0); (1, -1) uv (1, 1);
  (-1, -1) uv (0, 1); index buffer 6 x R32_UINT = 2, 1, 0, 3, 1, 2. The VS passes POSITION (w = 1) and TEXCOORD0
  through [L: buffer contents, C: VS]. The DXT Compress pass uses an identical quad (its own buffers, same bytes).
- Draw object 0x515630: t0 = t1 = the normal temp, samplers id 5 [C].

### 3.2 Macro Shadow (pass 3)

- Input t0 = **the tile's height texture** tile+0xEC (here A8_UNORM 1024 x 1024, 11 mips, the height page shared by
  several tiles), sampler A [L + C: draw object field +0x18 = tile+0xEC].
- The PS ray-marches 40 steps from each texel toward the light over the height texture and outputs (1 - occlusion) *
  weight [C]. Reads: own cb0[73].xy = the tile's offset into the height page (floats of tile+0xE4, tile+0xE8; here
  (64, -1728)), cb0[75].z = weight = 1 / 0xD90F6C (here 1/3); TerrainFrame (slot 1) register 16.z (height unit 0.2),
  register 17 (the "AuxDir" = **shadow direction**, here (0.4330, -0.25, 0.8660)), register 22 (height texel scale
  (1/1024, -1/1024)) [C reads, L values].
- The VS adds own cb0[0].xy to POSITION.xy; both are 0 in all three draws [L].
- **Three draws, ONE+ONE additive into an R8G8B8A8_UNORM target** (count 0xD90F6C = 3 here). Each draw also gets its own
  pair TR8+0x420 + 8i into own register 75.xy ((0,0), (-0.4991, 0.2658), (0.2322, -0.3926) here), but the SM5 PS does
  not read 75.xy, so the three draws are identical [C + L]. The sum is not one draw at weight 1: each draw is rounded to
  8 bits before the add, so a port must also draw three times (or emulate the rounding) to match bit for bit [I, checked
  by the comparison in section 7].
- Geometry: the tile's blend grid (67 x 67 vertices x 48 bytes, the same buffer and the same 26,136 R16 indices as the
  blend), VS 0x067e4590 reads POSITION and TEXCOORD0 only [C].
- Own buffer: TerrainShaderScreen's 0x4C0-byte block (the 1,280-byte slot-0 buffer); other registers carry stale data
  that the shaders do not read [L].

### 3.3 CacheBake, "Terrain Cache" (pass 4)

- Targets: two R8G8B8A8_UNORM textures of the size class (here 256 x 256, 6 mips; the array-mode scratch of the size
  class), viewport = the size class. Not cleared (every texel is drawn) [L].
- Inputs (draw object 0x6CF830 [C], bound views [L]):

  | t | Content | View | Sampler |
  |---|---|---|---|
  | 0 | Normal Calculation output (3.1) | R8G8B8A8 512, 10 mips | B |
  | 1 | blend target 0, **Albedo temp** | R16G16B16A16_FLOAT 512, 10 mips | B |
  | 2 | Macro Shadow output (3.2) | R8G8B8A8 512, 10 mips | B |
  | 3 | blend target 2, **Spec temp** | R16G16B16A16_FLOAT 512, 10 mips | B |
  | 4 | Sunlight cube of the tile's style (TR8+0x3D0 + 4 style) | BC1_UNORM_SRGB cube 128, 8 mips | A |
  | 5 | Shadowlight cube of the style (TR8+0x3E4 + 4 style) | BC1_UNORM_SRGB cube 128, 8 mips | A |
  | 6, 7 | none (an 8 x 8 dummy is left bound) | | A |
  | 8 | ColorKey **Terrain** LUT of the style (`0xD90E4C + 4 style`), 32^3 R8G8B8A8 | 3D | B |
  | 9 | FOW Mask (`0xDA12AC`) | bound, **not read** by pass 1 | B |
  | 10 | ColorKey TerrainFOW LUT (`0xD90E60 + 4 style`) | bound, not read | B |
  | 11 | TR8+0x564 (SpecScreenMap, BC3 1024) | bound, not read | A |
  | VS 0 | tile+0xEC height texture | bound, not read by the VS | C |

  Style = the lowest set bit 1..4 of tile+0x138, 0 (Generic) when none [C, as in asset-pipeline.md 3.4].
  *Corrected 2026-11-14 by issue 3957 (epic 0168) from `fog-of-war.md` 3.10 [C, task 3950]: row 10's table stride is 4
  (`0xD90E60 + 4 style`: the terrain draw object `0x5AD770`, CacheBake `0x6CF830`, `0x520340` and the instanced-model
  draw `0x75A220` all index it with stride 4); was "`0xD90E60 + 8 style`". `asset-pipeline.md` 3.5's formula was right.*
- Constant buffer: slot 1 = TerrainFrame (shared id 11, 0x250 bytes); the PS reads **only register 21 = HalfVector =
  normalize(LightDir + CameraAxisZ)** [C]; slot 0 unbound. In the capture (load screen, identity view): LightDir (reg 18) =
  (0, -0.4226, 0.9063), CameraAxisZ (reg 20) = (0, 0, 1), HalfVector = (0, -0.2164, 0.9763) [L]. **The half vector holds
  the camera axis, so the specular content of t8 depends on the camera at the time of the bake** [C for the read; I that
  in play the TerrainFrame of the last view is in the buffer when a tile bakes]. *Corrected 2026-11-13 by W6 (3406,
  section 5.3): before every bake the EXE writes TerrainFrame with an identity view, so CameraAxisZ is always (0, 0, 1)
  and **t8 does not depend on the camera**; the [I] above is superseded.*
- Geometry: the tile's **CacheBake grid**, 67 x 67 vertices x 24 bytes: POSITION f32x3 (clip space with a half-texel
  margin, -1.0039 .. 0.9961, z = 0.5; not read by the VS), NORMAL f16x4 (the terrain's unit normal at the vertex, w =
  1), TEXCOORD0 f16x2 = (i/66, j/66). Same 26,136 R16 indices as the blend [L]. The VS places the vertex at clip (2u - 1,
  1 - 2v) and passes the normal and uv [C]. The CPU prepares this array per tile beforehand (tile+0x308, 24 bytes per
  vertex, (w+2)(h+2) vertices) and 0x63CCD0 copies it into the bake's dynamic buffer at the start of the bake [C]; the
  writer of tile+0x308 is in section 5. In the capture the last column has normal (0, 0, 1) [L].
- What the PS computes [C]: a tangent frame from the vertex normal; the normal map (t0, unpacked `* 2 - 1`) perturbs it;
  lighting = lerp(Sunlight(n), Shadowlight(n), occlusion) * 1.7, where occlusion = the macro shadow's red channel (t2);
  colour = albedo (t1.rgb) * lighting + 0.001, gamma `^(1/2.2)`, then the Terrain ColorKey LUT -> **target 0 = t0
  colour, alpha 1**. Specular: e = spec t3.a * 64 + 1; term = saturate(dot(HalfVector, n))^e * e * saturate(1 -
  occlusion); target 1 rgb = LUT((t3.rgb * term)^(1/2.2)), alpha = t3.a * term -> **t8 = baked specular colour** (BC1
  drops the alpha).
- Pass 0 (one target, PS `0x067ebe78`) is chosen when tile+0x2FC = 0 (no layer has a spec map); then no t8 is made [C].
- After the draw: `GenerateMips` on both targets [L]: the mips of t0/t8 come from the driver's box filter of the
  scratch target, not from a separate pass.

### 3.4 DXT Compress (passes 5 and 7) and the copy (6, 8)

- Target: an R32G32_UINT texture of (size / 4)^2 with the cache's mip count (here 64 x 64, 6 mips), created with
  RENDER_TARGET | SHADER_RESOURCE | GENERATE_MIPS; one RTV per mip; viewport (size/4) >> m [L].
- Source t0 = the CacheBake target (all its mips), sampler slot bound but unused (`ld`) [C].
- Own constant buffer (0x77A940 maps 0x10 bytes): register 0.x = the mip level m as a float; the 256-byte slot-0 buffer's
  other bytes are stale [L]. Draws m = 0 .. mips-1 [C + L].
- Geometry: the 4-vertex quad of 3.1's layout (POSITION xy = +-1); the VS outputs clip xy = POSITION.xy * 2 - 1, so
  the quad spans -3..1 and covers the viewport [C].
- The PS per pixel (x, y) [C], a bounding-box encoder in the style of the well-known real-time DXT1 method: loads the 4 x
  4 texels (4x + i, 4y + j) of mip m with `ld`, takes per-channel min and max of rgb, picks the box diagonal by the signs
  of the red-blue and green-blue covariances around the box centre, insets both ends by (range / 16 - 1/510), quantises
  them to 5:6:5 with round-to-nearest-even, swaps them so that colour0 > colour1 (4-colour mode, never the 3-colour
  mode), projects each texel onto the end-colour line for its 2-bit index, and writes **(colour0 | colour1 << 16,
  indices)** as the uint2. Pixel (x, y) of mip m is block
  (x, y) of BC1 mip m after the copy (`CopyResource` between R32G32_UINT and BC1_TYPELESS of the same block layout) [L].
- **Mip counts by size class** (0x694A60 [C]): 64 -> 4, 128 -> 5, 256 -> 6, 512 -> 7 (the chain stops at 8 x 8 texels
  = 2 x 2 blocks). The size class is tile+0x17C; the compress target and the RGBA8 scratch are per size class (TR8
  arrays indexed 0..3), the float blend / normal / macro-shadow targets are always 512.

### 3.5 Where the bake's other variants go (not in the capture)

- **Near-camera caches** (tile+0x190/+0x194, RGBA8, 9-10 mips): when the bake is called in state 5, CacheBake also
  renders straight into these (one or two targets, the same program), before the BC1 path [C]; there is no compression
  for them. When the EXE makes them, which tiles get them and what the draw binds: 3.5.1 (issue 0306).
- **Non-array mode** (0xD90F4C = 0): CacheBake renders straight into tile+0x198 (an RGBA8 cache); no DXT pass [C].
- Decal passes: see 4.

#### 3.5.1 The near-camera caches: when, which tiles, which size (issue 0306, 2026-11-15, impl-B-0306)

Static reading of the objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm` and 2903's decompile of 0x694A60 (read only,
nothing committed, D83), the two jump tables read from the DX11 EXE's bytes (0x62796C: 5 entries, 0x7BAF50: 6 entries;
file offset = VA - 0x400C00); live counts in 3.5.2. States as terrain-bake-records.md 3.3 (+0x310 now, +0x314 requested).

- **Who asks [C].** The in-play cull 0x7BA820, per visible tile (visible lists TR+0x1AF0.., count TR+0x2334..), first sets
  tile+0x180 = the wanted size class (0x4B2B40, 5.3), then by the current state (table 0x7BAF50): 0 -> try a BC1 slot
  (0x716010, with eviction rounds) and request 3; 1 -> request 3; 2 and 3 -> nothing; 4 -> request 5; **5 -> if the content
  generation +0x1CC is ahead of +0x1D0, rebuild the layer list and request 2; then, if the near size +0x178 differs from
  the wanted +0x180 and the free list of the wanted size holds more than one entry, request 2.**
  *Server deviation (ticket 0667, 2026-11-22, `live-terrain.md` 11.11) [C, L]: with this rule a state-5 tile whose
  content changed keeps drawing its old pair while the wanted pool is empty (array mode; the 512 pool is full at a close
  camera), which showed live land/water changes with their old colours. The server's `NearCaches::frame` bakes such a
  pair again into its own textures (or gives it back to the BC1 pair when it lacks a needed t8); everything else is the
  EXE's rule.*
- **The up-res pass [C]** (same function, after the sort of each visible list by 0x5E7010: requested state, then current
  state, then +0x1BC, then +0x1A4): it collects visible tiles whose state is not 0 and not already 5 (or 4 -> 5), at most
  `[0xD90F64]` = 2 per frame while the camera is settled (TS+0x75FE4 = 0), `[0xD90F60]` = 1 while it moves; for each,
  within the frame's bake budget (16 settled, 1 moving, 3.3 of terrain-bake-records.md), it calls **0x5D78A0**, and on
  success sets +0x314 = 5 and puts the tile on the dirty list. So **every visible baked tile is up-ressed to the near pair
  in turn, two per frame when the camera rests, until the near pools run dry**; the near caches are not limited to the
  closest tiles. (Then the bake-ahead step for tiles in state 0.)
- **0x5D78A0, the allocation [C].** In array mode (0xD90F4C = 1, stock high): new BC1 slots for t0 and (when +0x2FC) t8
  from the array pool (0x5C60B0) at **`[0xD90F30]` = 256**, and a near cache for +0x190 and (when +0x2FC) +0x194 from
  **0x6AD280(wanted size +0x180)**. It fails (and gives everything back) unless every one of them was had and both near
  caches have the same size; on success it frees the old caches, stores the new handles, sets **+0x178 = the near size**
  and **+0x17C = 256**. So a tile in state 5 holds a BC1 pair at 256 **and** an RGBA8 pair at its wanted size.
- **0x6AD280, the near pools [C].** Pops the free list of the asked size (64: TR+0x244C/+0x2450, 128: +0xA2DC/+0xA2E0,
  256: +0x1216C/+0x12170, 512: +0x19FFC/+0x1A000; array, count). **In array mode it does not fall back**: an empty list
  sets the size to the next smaller class and returns 0, so the up-res fails and the tile keeps drawing its BC1 pair; only
  in non-array mode does it loop down to the next smaller list. (The cull's up-res pass, its sort keys and budget in
  detail: 3.5.3.) *This corrects terrain-bake-records.md 3.2, which said "An
  empty size falls back to the next smaller one" without the mode.* The pools are filled once by 0x63BCB0 from
  TerrainSystem counts TS+0x629F4 (64), +0x629F8 (128), +0x629FC (256), +0x62A00 (512), +0x62A04 (array pool) at
  TerrainSystem create (0x5BCEF0); their values are read live in 3.5.2.
- **The bake [C].** The wrapper 0x627810 by requested state (table 0x62796C): 5 -> (unless the current state is 4) bake
  0x694A60 with state 5: CacheBake into the near pair (two targets when +0x2FC, else one; the same grid draw object as the
  BC1 path) and then the whole BC1 path at 256 into the new slots; 3 -> drops the near pair (back to its pools) unless the
  current state is 1, and bakes BC1 only; 1 -> bakes BC1 only and drops the near pair; 2 and 4 -> no bake. ~~Whether the
  near pair gets mips by `GenerateMips` after CacheBake (its 9 / 10 mips) is [I]~~ *Settled 2026-11-15 by issue 0332
  (3.5.3) [L]: every mip of all 347 near textures of 3601's two captures equals `GenerateMips` of their own mip 0 on
  this GPU and driver.*
- **The draw [C].** 0x5C37E0 skips tiles in states 0, 1, 4; it draws a tile in state 3 with the draw objects at
  tile+0x154 / +0x15C and a tile in state 2 or 5 with those at +0x158 / +0x160 (the first of each pair without, the second
  with `[0xD90F1C]`). That the +0x158 / +0x160 objects are the ones that bind the near pair is [L by counts]: at 3601's
  cameras the live state-5 / state-3 counts equal the captured RGBA8 / BC1 t0 counts tile for tile in number (3.5.2). So **a tile with a
  near pair is drawn from it, not from its BC1 pair** [L, census + C].
- **Size [C].** The near pair's size is the wanted class (0x4B2B40: 512 when the tile's projected size p > 256, else 256
  when p > 128, ...; at least `[0xD90F30]` = 256 at TerrainDetailLevel 3). The draw samples with derivatives, so a 512
  cache's mip 0 is read only where the tile covers more than about 512 px; below that the 512 pair draws the same texels
  as a 256 one would from its mips 1.. (replay below: at camera 1 the 512 -> 256 variant is bit-equal to the 512 one).

**The census of 3601's two stock captures [L]** (`render/evidence/0306/census_0306.py`, results
`render/evidence/0306/results/census_cam{1,2}.txt`; Tiny `C5H-3600-TINY_0009V`, 32-bit pin `i0ship-26234371-ia`, c5h-c,
RTX 4080, 2554 x 1411, VT present, D94):

| Camera | scene terrain draws | t0 RGBA8 256 (9 mips) | t0 RGBA8 512 (10 mips) | t0 BC1 256 (6 mips) | t8 RGBA8 256 / 512 | t8 BC1 256 | t8 8 x 8 placeholder |
|---|---|---|---|---|---|---|---|
| 1, LookAt (33, 14), zoom 17 | 150 | 112 | 32 | 6 | 107 / 32 | 6 | 5 |
| 2, Honolulu (51, 14), zoom 11 | 58 | 0 | 32 | 26 | 0 / 32 | 25 | 1 |

Each RGBA8 resource is bound by one draw only. 32 tiles with a 512 pair at both cameras (64 RGBA8 512 textures): the 512
pool is 64 textures [L, 3.5.2], full at both cameras. At camera 2 the near pairs lie on the far half of the view, and the 26 closest
tiles (the bay and Honolulu, which want 512) draw BC1: the 512 pool was taken before the zoom-in, and in array mode an empty
512 pool gives no near cache at all (above) [L for the picture, C for the rule]
(`F:/c5-64bit-work/0306/replay/cam2/diff/bc1_vs_exe_diff_x16.png`, Visual Checks vc-20261115-010926/-010927). So **which
tiles carry a near pair depends on the camera's history and the pool sizes**, not only on the current view.

#### 3.5.2 Live counts at 3601's cameras [L, 2026-11-15 01:26-01:32 EST]

Goldberg instance c5h-a (claimed, standard set backed up by SHA-256 and restored by hash, provision verify --quick 0),
stock: 32-bit pin `i0ship-26234371-ia` (3c2f47da...), `darkred.py --arm stock`, no proxy, no server; Tiny save copy
`C5H-0306-TINY_0009V` of c5h-c's `C5H-3600-TINY_0009V` (27420a41..., removed after); 2554 x 1411, RTX 4080, VT present.
Driver `render/evidence/0306/live_0306.py` (cdb `-pv` attach, read, detach, 4-9 s per read; the game went on; ended by
`UI.ExitGame()` over the Lua channel, stop and leftovers 0). Each camera read about 1 s and 12 s after it settled: equal
counts both times (the up-res had finished while the wheel ticks settled). Summary `render/evidence/0306/results/live_summary.json`;
raw cdb texts in `F:/c5-64bit-work/0306/live/out/`. Sequence as 3601's (load view, then camera 2, then camera 1):

| Step (zoom) | visible tiles (main view list) | state 5 (near pair, drawn from it) | near 256 / 512 | state 3 (BC1 256) | wanted 512 / 256 | free near 256 / 512 |
|---|---|---|---|---|---|---|
| load view (14.256) | 110 | 42 | 10 / 32 | 68 | 100 / 10 | 236 / 0 |
| cam2, LookAt (51, 14), 2 ticks in (11.0) | 57 | 32 | 0 / 32 | 25 | 57 / 0 | 242 / 0 |
| cam1, LookAt (33, 14), 3 ticks out (17.0) | 150 | 144 | 112 / 32 | 6 | 38 / 112 | 23 / 0 |
| cam1o, one tick further (19.0) | 251 | 144 (130 5/5, 14 5/2) | 130 / 14 | 107 | 0 / 251 | 1 / 36 |

- **The capture's counts are reproduced exactly**: camera 1 (zoom 17) 112 RGBA8 256 + 32 RGBA8 512 + 6 BC1 against 3601's
  112 + 32 + 6 t0 bindings in 150 draws; camera 2 32 RGBA8 512 + 25 BC1 (3601: 32 + 26 in 58 draws: the 58th draw is [I] a
  wrap copy of a listed tile).
- **Pool sizes [L]** (TS+0x629F4..+0x62A04): 16 textures of 64, **0 of 128, 256 of 256, 64 of 512**, array pool 6,144.
  Every near texture is R8G8B8A8_UNORM (format 28), 256 with 9 mips, 512 with 10 (texture records). Globals 0xD90F2C 512,
  0xD90F30 256, 0xD90F4C 1 (array mode), 0xD90F60 1, 0xD90F64 2, 0xD90F74 3.
- **No fallback in array mode [L]**: at the load view 100 tiles want 512, the 512 pool is empty, 236 256-textures are free,
  and those tiles stay in state 3 (BC1) instead of taking a 256 pair (3.5.1).
- **History [L]**: at camera 2 the 25 BC1 tiles are the closest rows (plot rows 13-16; the bay and Honolulu), the 32 near
  pairs the rows behind them (17-20): the 512 pool was full before the zoom-in. At camera 1 the 512 pairs stayed on tiles
  that now want 256 (38 want 512, 32 hold it); one tick further out (cam1o) 14 of them asked for 256 again (state 5/2) and
  18 had already given their 512 pair back (512 free 0 -> 36), while the 256 pool was down to 1 free texture and 107
  visible tiles drew BC1. So the share of BC1 tiles in the EXE's own picture depends on zoom and history: 6 of 150 at
  camera 1, 25 of 57 at camera 2, 68 of 110 at the load view, 107 of 251 one tick out of camera 1.
- One harness shot per camera (DIAGNOSTIC, D112; popups: none to close, 0 Esc rounds) in the Visual Checks feed under 0306
  (01:29-01:31); a first run of the same driver at 01:20-01:25 stopped at camera 1 because its zoom target 16.2556 (3609's,
  reached from the load zoom) is not reachable from camera 2's 11 (ticks step 2.0: 13, 15, 17): its load and camera 2 reads
  equal the second run's.

#### 3.5.3 The server's near pair: bake, pools and state machine (issue 0332, 2026-11-15 12:48-, impl-B-0332, branch `task-0332-near-rgba8-caches`)

**The up-res pass and its order, read again [C]** (objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm`, 0x7BA820
0x7BA9D3..0x7BAD5A, 0x5E7010, 0x4AD810; scratch excerpt `C:/c5-64bit-work/0332/cull.asm`, not kept; nothing committed,
D83). This completes 3.5.1's "who asks" and "up-res pass":

- Order inside one cull call: (1) per view list, per tile: tile+0x180 = 0x4B2B40, then the switch on +0x310 (3.5.1);
  (2) each view list sorted with `qsort` (import slot 0x9D68A4) by **0x5E7010: +0x314 (requested) ascending, then +0x310
  (current) ascending, then +0x1BC descending (unsigned), then +0x1A4 descending (float)**; (3) the up-res pass; (4) the
  bake-ahead step (one tile from TR+0x1AE8 at the cursor TR+0x2328), **skipped when any up-res succeeded this frame**;
  (5) the visible bakes: per list, a tile with request 3 and current state not 3, or current state 4, goes to the dirty
  list; one with request 3 from a state other than 1 counts one bake against the budget (over budget: a state-0 tile
  gives its slot back and its request returns to 0); request 3 from state 1 costs nothing (the wrapper only copies the
  state).
- **+0x1A4 is the second of the six plane distances that the visibility test 0x4AD810 stores at tile+0x1A0..+0x1B4**
  (dot(plane.xyz, tile+0x124 centre) + plane.w, for the planes at 0xDA1510 + 0x60 * view, written only when the tile is
  visible). In the order of terrain-surface.md 7 (far, near, left, right, bottom, top) it is **the distance from the
  near plane: the up-res takes the farthest tiles first**. +0x1BC: zeroed at tile creation (0x679BE7); no other writer
  found by a search of the listing for stores to +0x1BC (the hits belong to other objects) [C, negative: a write through
  another base is not excluded]; the server takes 0.
- The up-res pass: budget = 16 bakes when settled (TS+0x75FE4 = 0), else `[0xD90F60]` = 1; candidate cap = `[0xD90F64]`
  = 2 settled, `[0xD90F60]` = 1 moving. The collection walks each sorted list from the start and stops at the cap; it
  skips state 0, state 5 with request 5 and state 4 with request 5 (so a state-5 tile with request 2 is a candidate). Each
  candidate, while bakes < budget, **counts one bake before** 0x5D78A0 is called: a failed allocation uses budget too, and
  because the list is sorted, **the first candidates that fail (an empty pool) block every tile behind them in every
  frame** until the camera or the pools change. At the load view of 3.5.2 (100 tiles want 512, 512 pool empty) this is
  why 68 tiles stay BC1 although 236 textures of 256 are free.
- The near pair is a new allocation before the old one is returned (3.5.1): a tile that holds a 256 pair and asks again
  (content changed) needs two more free textures of 256.

**Bake (step 1) [L, 2026-11-15 13:00 EST, RTX 4080, Release of `de3b088b`]** (`render/src/bakepost/post.cpp`,
`c5r/bake/post.h`): `PostInputs::nearCache[2]` (named so because `near` is a Windows macro): CacheBake, the same program,
grid, inputs, states and samplers, drawn first into the given near pair with a viewport of its size, then `GenerateMips`
on it; the BC1 path at the size class follows unchanged. `nearCacheMips` (64 -> 7, 128 -> 8, 256 -> 9, 512 -> 10),
`nearCacheBytes` (256: 349,524; 512: 1,398,100), `createNearCache` (R8G8B8A8_UNORM, full chain, RT | SR, GENERATE_MIPS).
Checks: a near t8 missing or given against `needSecond`, two sizes, one texture twice, a format other than R8G8B8A8,
not the full chain, no GENERATE_MIPS: BadLayout before anything is drawn (each a negative test). Test
`game_tile_p_near_pair_is_the_state_5_bake` on 3403's tile P (`C5R_BAKE_REF`, section 7):

| Compared | Equal |
|---|---|
| near t0 / t8 at 256, mips 0-5, against the EXE's CacheBake targets of tile P (CacheBake at 256 + the EXE's `GenerateMips`) | 100 % of bytes (349,440 each) |
| near t0 / t8 at 256, all 9 mips, against the EXE's mip 0 + `GenerateMips` here | 100 % (mips 6-8: 4 x 4, 2 x 2, 1 x 1 included) |
| BC1 t0 / t8 blocks with a 256 or a 512 near pair, against the bake without one and against the EXE's | identical; 100 % |
| near pair at 512: every mip against `GenerateMips` of its own mip 0 | 100 % (mip 0 of a 512 pair has no EXE reference here) |

**The EXE's captured near pairs [L, 2026-11-15 13:06 EST]** (`render/evidence/0332/chain_0332.py` with
`render/evidence/0332/nearchain.cpp`: a `createNearCache` texture, the captured mip 0 written, `GenerateMips`, read back;
3601's two captures exported again by `render/tools/terrainreplay/export.py` into `C:/c5-64bit-work/0332/exports`, game data,
removed at the hand-back; results `render/evidence/0332/results/chain_cam{1,2}.json`):

| Camera | near textures (bindings) | whole chain equal to `GenerateMips` of their mip 0 | replay with every near cache replaced by [its mip 0 + the server's chain] |
|---|---|---|---|
| 1 (zoom 17) | 219 of 256 + 64 of 512 (283 SRVs in 144 draws) | 283 of 283, every mip 100 % | 0 of 3,603,694 pixels differ |
| 2 (zoom 11) | 64 of 512 (64 SRVs in 32 draws) | 64 of 64 | 0 of 3,603,694 |

So the EXE makes the near chain with `GenerateMips` (the [I] of 3.5.1 is now [L]), and the server's chain from the same mip 0
is the EXE's bit for bit on this GPU and driver. **Not verified: mip 0 of a 512 pair against the EXE** (CacheBake at a 512
viewport; no captured bake has its inputs). A full check needs a stock capture of a frame that runs an up-res bake: its
initial contents hold the previous up-res tile's blend, normal and macro-shadow targets, its CacheBake grid and its near
pair (3403's method, section 7), so the server's state-5 bake of that tile can be compared in every mip. That capture is a
required step of the follow-up with the draw (MC, 2026-11-15, queue entry #1795). *Settled 2026-11-17 by issue 0371
(3.5.4) [L]: mip 0 of a 512 pair equals the EXE's (every mip, t0 and t8). The capture's grid is not in it (the dynamic
vertex buffer is rewritten by the frame's own bake): the normals came from the EXE rule over the save's height field.*

**Pools and state machine (step 2)** (`render/src/bakeresidency/nearcache.cpp`, `c5r/bake/residency.h`: `NearCaches`,
`NearPoolCounts`, `NearCacheOptions`, `nearPoolBytes`): the pools with the EXE's stock counts (16 x 64, 0 x 128, 256 x 256,
64 x 512 textures), the switch, the sort (ties in list order [I]: the EXE's `qsort` is not stable), the up-res pass with
the cap, budget-per-attempt and no fallback in array mode (the per-texture fallback without it), a new pair taken before
the old one goes back, release on eviction and on a BC1-only bake, re-request on a size change only when the wanted pool
has more than one free texture and on a content change. Tests (`c5r_bakeresidency_test`, 2026-11-15 13:11, 31 run, 0
failed, 0 skipped): pools and memory, option and range negatives, 2 per settled / 1 per moving frame (farthest first),
the sort keys in order, no fallback and the blocking of failed candidates (array mode) against the fallback without it,
the size-change rule with 3 and with 1 free texture, release and failed bakes, and **0306's camera 2**: after a load view
that fills the 512 pool, the zoomed view has 32 tiles with a near pair and the 25 closest BC1, as live (3.5.2); a second
case shows the history: the same final view keeps the pairs on tiles that took them earlier, while an empty start gives
them to the farther tiles.

**The Residency's switch** (`ResidencyOptions::nearCaches`, **off by default** until the draw uses the pairs;
`nearOptions`, `nearBaker`; `ResidencyView::moving`): `beforeDraw` runs the up-res pass first over the visible land tiles
(projected size by `c5r::terrain::exeTileProjectedSize` at the view's width, distance from the view's near plane) and gives
its own visible bakes `burst` minus the frame's attempts; each pair taken is baked by the host's `NearBaker` (the pipeline's
state-5 bake: the follow-up) into pool textures made on first use; eviction returns the pair; `nearPair(x, y)` gives the
draw its textures; stats `nearHeld256/512`, `nearFree256/512`, `nearTextures`, `nearBytes`, `nearUpres`, `nearShort`,
`nearBakeFailures`. Tests: off by default, on without a baker BadLayout, 2 per settled / 1 per moving frame, every visible
land tile held after enough frames, pool accounting, release on eviction, a raised tile baked again, a failing near bake
leaves the BC1 pair. **Not done here (follow-up):** the draw (`render/src/terrain/`: t0 / t8 from the pair in states 2 and 5),
the pipeline's `NearBaker`, the server's camera-moving signal, the live count against `live_0306.py`. *Done 2026-11-17 by
issue 0371: 3.5.4.*

**Memory [C from the counts, L for the pool counts]:** a pair of 256 699,048 B, of 512 2,796,200 B; the stock pools full
256 x 349,524 + 64 x 1,398,100 + 16 x 21,844 = **179,306,048 B (171.0 MiB, 179.3 MB)** of video memory beside the BC1 caches
(test `near_pool_sizes_and_memory`); the server makes a pool texture on first use, so it holds only what the camera's history
has taken (`ResidencyStats::nearBytes`). The 128 pool is empty at stock high and the 64 pool unused there (min class 256).

#### 3.5.4 Part 2: the draw from the pair, the server's state-5 bake, the camera signal, mip 0 of a 512 pair, the live count (issue 0371, 2026-11-17 02:46-, impl-B-0371, branch `task-0371-near-rgba8-caches-part2`)

*This completes 3.5.3's "Not done here" list, and settles its "not verified: mip 0 of a 512 pair" (below, [L]).*

**The draw [C, tests].** `Residency::beforeDraw` hands the renderer, for a tile in state 2 or 5 that holds a pair, the views
of its pool textures (R8G8B8A8_UNORM, the full chain, made with the texture) as t0 / t8 (`TileCache`), and the BC1 pair
otherwise; a tile's handed pair is remembered (`nearShown`), so an up-res, a release, an eviction or a failed near bake
sets the tile again at the next draw. `ResidencyStats::nearDrawn256/512` count the visible tiles handed a pair. The renderer
counts what each drawn tile's t0 is (`c5r::terrain::CacheKind` from the view: BC1, RGBA8 256 / 512; `FrameStats::drawnKind`,
and `listKind` over the EXE's visible list, the census of 3.5.1 in the server's own frame). Tests
(`render/tests/bakeresidency`, 39 run, 0 failed, 2026-11-17 03:02 and 04:05): `near_draw_binds_the_pair_in_state_5_and_the_bc1_pair_otherwise`
(arrays on and off: 0 near draws before the first up-res, 2 after it, every visible tile drawn from its pair after enough
frames and `drawnKind[2]` = `nearDrawn256`, a move with radius 0 back to BC1, a failing near bake leaves no stale view) and
`near_off_draws_bc1_only_and_near_stats_stay_off_the_stats_line`.

**The up-res pass walks the EXE's visible list [L, C].** The first live run (below, run 1) fed `NearCaches::frame` the
residency's own visible set (its 8 x 8 block cull with z bounds and wrap copies), which is wider than the EXE's list: 4 of
the 32 pairs of 512 went to tiles the EXE does not list (load view: held 32, listed 28). Since `18c34af7` the pass takes
`c5r::terrain::exeFrameLod`'s list (0x7BA820 / 0x4AD810: height class 3 by the EXE's window, `exeTileClass3`, and the
bounding sphere in the six planes), the residency's set stays for its own BC1 bakes. With it the load view matches the EXE
exactly (table below).

**The server's state-5 bake [C].** `srvrender::ResidencySource::NearBaker` (`server/src/render/srv_residency_source.cpp`):
the pipeline's whole bake of the tile (`PipelineBaker::bakeWith`) with `BakeRequest::nearCache` (new, forwarded to
`PostInputs::nearCache`; null = the bake unchanged bit for bit), the tile's decals as `DecalBaker` finds them,
`areaChanged` = content generation > 1 (the residency's rule), and t8 forced to what the pair holds (`Overrides::needSecond`:
tile+0x2FC is the tile's, the pair was taken with it). The BC1 pair the same bake makes equals the tile's own and is dropped.
With the stand-in baker (tests) the pair is cleared to a colour of the tile. Host key `render_bake_near` (server.cfg, beside
`render_bake_warm`; recorded in the scene bundle's config): **1 by default since `ce2e535d`** (this section's checks), 0 =
every tile from its BC1 pair (the picture before 0371). A per-second line `near 1s:` in render.log (held / drawn / free per
size, textures and MB, up-res, short pools, failed bakes, camera frames moving / settled, and the t0 census of the drawn
tiles and of the EXE's visible list). *Negative result, 2026-11-17 05:45-06:20 [L]: the same figures appended to
`Residency::statsLine`, and three camera / near-bake fields appended to `ResidencySource::statsLine`, cut the end of the
server's `terrain 1s:` line: the render log writes a line through a 1,536-byte body (`Emit`, srv_render_common.cpp), the
terrain line was already about 1,500 characters, and the cut line lacked its `| tot_drawn ...` tail, so the tests of
`server/tests/render/test_residency_source.py` that read the residency part failed (12, then 4 after the first fix; a
build of `main` passed the same 4 at 06:15, one of this branch with `render_bake_near` = 0 failed them: the fields, not the
pairs). None of 0371's figures are on that line since; they are on `near 1s:` only. The live runs of this section
(server.exe of `18c34af7`) had cut terrain lines; their `near 1s:` lines, which every figure here comes from, were whole.*

**The camera-moving signal [I].** The EXE switches the up-res cap and the bake budget on TS+0x75FE4 (1 while the camera moves,
terrain-bake-records.md 3.3). The shell does not publish it (`VPShellCamera`, shell/common/shell_shared.h, has no such
field; publishing it is a shell change, not this issue's), so `ResidencySource::beforeDraw` sets `ResidencyView::moving`
when the frame's view or projection differs byte for byte from the last draw's (the first draw of a source is settled). What
it misses: (a) a camera that moves and comes back to the same matrices within one frame (counted settled); (b) the frames
at the ends of a move where the EXE's flag and the matrices disagree: the flag was 1 from 0.09 to 0.66 s after a
`UI.LookAt` [L, 3.3 of terrain-bake-records.md] while the camera's own animation may start later or end earlier, so on
those frames the server allows 2 up-res where the EXE allows 1 (and 16 bakes where the EXE allows 1); (c) the server's
frames are its own draws of the EXE's frames (the composite's lockstep), so a frame the server does not draw is not
counted at all. Counted per second in `near 1s:` (`cam_moving_frames`, `cam_settled_frames`).

**Mip 0 of a 512 pair against the EXE [L, 2026-11-17 03:17-03:47 EST].** Stock capture (RenderDoc 1.47 preloaded, 0302's
`capture --frames 3`), Goldberg c5h-c, 32-bit pin `i0ship-26234371-ia`, `darkred.py --arm stock`, Tiny `C5H-0371-TINY_0009V`
(copy of `C5H-3600-TINY_0009V`, 27420a41...), RTX 4080 driver 32.0.16.1088, 2554 x 1411, VT present (D94); driver
`render/evidence/0371/capture_0371.py`: the load view, two wheel ticks out (zoom 18.26: the tiles that held a 512 pair ask
for 256 and give it back), 15 s, then the capture request and four wheel ticks in at once (zoom 11: the tiles want 512 with
512 textures free). Two earlier runs wheeled first and captured 2 s later: none of their frames baked (the up-res had ended);
their exit by `UI.ExitGame()` with the preload ended with 0xC0000005 (ticket 0483), so the driver now stops by the harness.
Frames 1777 and 1779 each hold one state-5 bake at 512 with t8 (CacheBake into a 512 pair, then the BC1 path). In 1779 the
scratch targets' initial contents are the bake of the frame before (1778) of tile **(35, 16)** (origin 2240, 1024, page
offset -1728, -832 from its terrain draw's TileOffset), whose near pair (512, 10 mips, t0 and t8) the frame draws, so its
initial contents are that bake's result. Its CacheBake grid normals are not in the capture (the bake's dynamic vertex
buffer is written again by the frame's own bake): the test computes them by the EXE rule (5.1, `makeExeGridNormals`) from a
height-field tap of the same save (`F:/c5-64bit-work/0169/4312/bundles/r1/heightfield/load0003.c5hf`; confirmed the same
map by the results below: the normal target and both CacheBake targets at 256 come out equal). Reference folder
`F:/c5-64bit-work/0371/refupres` (game data, not committed) by `render/tests/bakepost/mkref.py --ids
render/evidence/0371/ids_frame1779.json` and `render/evidence/0371/mkref_0371.py`; test
`game_upres_tile_near_pair_is_the_exes_in_every_mip` (C5R_BAKE_REF_UPRES, C5R_UPRES_HF), output
`render/evidence/0371/results/bakepost_upres_frame1779.txt`:

| Compared with the EXE's own target of tile (35, 16) | without the tile's decals | with the EXE's macro shadow given |
|---|---|---|
| Normal Calculation, all 10 mips | 100 % | 100 % |
| Macro Shadow, all 10 mips | 33.9 % (median 60, max 255) | 100 % (given) |
| CacheBake at 256, target 0 / target 1, all 6 mips | 54.3 % / 99.76 % | **100 % / 100 %** |
| BC1 t0 / t8 blocks, all mips | 33.5 % / 99.80 % | **100 % / 100 %** |
| **near t0 at 512, mip 0** | 54.7 % | **100 % (1,048,576 bytes, max difference 0)** |
| near t0 / t8 at 512, all 10 mips | 54.6 % / 99.79 % | **100 % / 100 %** |

So **the server's state-5 bake at 512 is the EXE's bit for bit in every mip, mip 0 included** [L], given the tile's
macro shadow. The macro shadow differs without the tile's decals: the tile's pass-3a decals (tree shadows, 4) are not in
the capture (the decal set's buffers of frame 1778) [I: the difference has the shape of added shadow, max 255; not
checked decal by decal]; that pass is checked bit for bit on its own by epic 0166 (terrain-decals.md 11). The test hands
the EXE's macro shadow through `PostInputs::macroShadowGiven` (new; tests and replays only, the product never sets it;
negative tests for its shape).

**The live count against `live_0306.py` [L, 2026-11-17 04:10-04:16 EST].** The product path as shipped on c5h-c (proxy,
server.exe and composite of this branch at `18c34af7`, x64 pin 04b1211b / 29f58e17 DLL, D33 with Overlay on, VP64Options
ServerRenderer = 1, the repository's shell_composite.ini, no test key; `render_bake_near` = 1 by default), Tiny
`C5H-0371-PP-TINY_0009V` (copy of `C5H-3600-TINY_0009V`), 0306's sequence (driver `render/evidence/0371/live_0371.py`,
results `render/evidence/0371/results/live_0371.json`); the server's census of the EXE's visible list from `near 1s:` 12 s
after each camera settled, against 3.5.2's live cdb read of the stock EXE (12 s reads):

| Step (zoom) | listed tiles EXE / server | EXE near 256 / 512 / BC1 | server near 256 / 512 / BC1 | free 256 / 512 EXE / server |
|---|---|---|---|---|
| load view (14.26) | 110 / 110 | 10 / 32 / 68 | **10 / 32 / 68** | 236 / 0 ; 236 / 0 |
| cam2 (11.0) | 57 / 57 | 0 / 32 / 25 | 0 / 23 / 34 | 242 / 0 ; 216 / 0 |
| cam1 (17.0) | 150 / 150 | 112 / 32 / 6 | 108 / 0 / 42 | 23 / 0 ; 0 / 0 |
| cam1o (19.0) | 251 / 251 | 130 / 14 / 107 | 108 / 0 / 143 | 1 / 36 ; 0 / 0 |

- The visible list is the EXE's at every step (same count), and **the load view is the EXE's exactly** (both machines start
  there with the same history: the load).
- After the first camera move the two histories part: at cam2 the EXE's 32 pairs of 512 all lie on cam2's list, the
  server's 23 (9 stayed on load-view tiles that cam2 does not list, and 20 tiles not listed at cam2 took pairs of 256 on
  the way); from then on the server's 512 pairs sit off the list and its 256 pool runs dry at cam1 (the EXE keeps 23 free).
  So the per-tile assignment after a move depends on frames the two count differently. Candidates [I, not separated]: the
  camera signal's misses (b) and (c) above (2 up-res per frame where the EXE allows 1, and the LookAt's travel frames), and
  the EXE's per-frame list during the travel. Not fixed here: a per-frame trace of both machines through one LookAt (the
  EXE's by a non-breaking cdb read per frame is too slow; a capture of the travel frames is the way, 0302's
  `capture --frames`) is the follow-up proposed in the hand-back.
- What it means for the picture: the share of tiles drawn from a pair is close at rest (cam1: 108 of 150 against 144 of
  150), but which tiles are BC1 differs; each such tile carries BC1's rounding (3.5.1: median 2-3, p95 7-8 on near tiles).

  The counts are reproducible: a second run of the same build (run 4, 04:26-04:32, the acceptance pictures' run) gave the
  same numbers at every step (`results/live_0371_a.json`).

**Memory [L, workstation outside a timing window: an indication, D106].** Arm a (run 4, `render_bake_near` = 1) against arm
n (04:17-04:22, the same layout with `render_bake_near=0` through the TEST ONLY `server_cfg_extra`, the picture before 0371),
same instance, save and steps; `results/live_0371_a.json`, `results/live_0371_n.json`:

| Step | near textures made / their MiB (a) | server.exe private MB a / n | EXE private MB a / n | EXE largest free block MB a / n |
|---|---|---|---|---|
| load view | 84 / 92.0 | 877.9 / 777.8 | 1,288.4 / 1,289.3 | 1,957 / 1,994 |
| cam2 | 104 / 98.7 | 887.1 / 779.2 | 1,287.6 / 1,282.1 | 1,957 / 1,994 |
| cam1 | 320 / 170.7 | 977.2 / 801.6 | 1,295.4 / 1,282.2 | 1,957 / 1,994 |
| cam1o | 320 / 170.7 | 997.1 / 819.1 | 1,289.0 / 1,283.3 | 1,957 / 1,978 |

- The pools of 256 and 512 are made in full by cam1 (256 + 64 textures, 178,956,800 B = 170.7 MiB; the 16 textures of 64
  are never used at the stock minimum class 256, so 3.5.3's "179.3 MB when full" is 0.35 MB less in practice). server.exe's
  private bytes grow by about the textures made (+100 MB at the load view, +178 MB from cam1 on): the driver's copy of the
  pool textures is counted there [I]. The render log's `video_local` rose by 71-95 MB in the same steps (its periodic sample
  lags the step).
- The EXE does not change: private bytes within 13 MB between the arms at every step, in both directions; the largest free
  block differs by 21-37 MB between the two sessions at the load already, before any near pair could matter to the EXE (the
  pairs live in server.exe's device), so [I] session-to-session spread, not a cost of the pairs.

**Game results and 30 AI turns [L, 2026-11-17 04:34-05:18].** The same layout on the huge save (copy `C5H-0371-HUGE_0350` of
`C5H-VP8P-HUGE_0350`, turns 350-379), arm a (`render_bake_near` = 1) and arm n (= 0): 30 AI turns each, no box, 0 LOUD lines
(arm n: its two TEST ONLY lines), stopped by the harness; every player's statistics at the human turn after turn 379
(`proxy/tools/live/eq_stats.py`): **0 differing keys of 3,626** (`results/eq_huge30_a_vs_n.txt`). On the huge map the
pools held 8 x 256 and 32 x 512 at the end (91.3 MiB; the start camera's view; `cam_moving_frames` 0: no camera move in an
AI-turn run). The pairs are the server's own textures; nothing of them reaches the DLL.

**Pictures.** ACCEPTANCE (D112, D114): the composite's back buffer at each of the four steps of run 4 (E9: PrintWindow on the
product path gave UI-less frames for 3 of 12 shots in these runs; those are labelled DIAGNOSTIC), Visual Checks
`--issue 0371`, each with 0306's stock picture of the same step as an approximate reference (same save, steps and asset set,
another session). Whole-frame figures (`render/evidence/0371/compare_0371.py`, `results/compare_0371.jsonl`; arm a = run 4's
back buffers, arm n = run 3's PrintWindow shots, whose load picture is UI-less) against 0306's stock pictures, largest
channel difference over all 3.6 M pixels:

| Step | a vs stock: median / p95 / max, equal | n vs stock: median / p95 / max, equal |
|---|---|---|
| load view | 2 / 54 / 248, 41.1 % | (n's picture UI-less) |
| cam2 | 2 / 61 / 248, 38.4 % | 2 / 60 / 255, 39.1 % |
| cam1 | 6 / 44 / 245, **18.8 %** | 6 / 48 / 248, **8.3 %** |
| cam1o | 4 / 33 / 250, **21.4 %** | 5 / 34 / 248, **13.7 %** |

The pixels equal to stock rise with the pairs where many tiles hold one (cam1, cam1o: the BC1 rounding gone on those
tiles, 3.5.1); the rest of the difference is what differs between sessions (units, the water's and the fog's clocks, the
interface's state) and the tiles whose pair assignment differs (above), so these figures are reported, not a gate; a
terrain-only compare at one session's cameras needs 4309's method (camera blocks, the same session's stock and product
frames), which this ticket did not run.

**Not verified, open (2026-11-17):** (a) the per-tile pair assignment after a camera move (the live table's cam2, cam1,
cam1o rows): the follow-up is a per-frame comparison through one LookAt (both machines' up-res per frame; a stock
multi-frame capture of the travel against the server's `near 1s:` lines); (b) a terrain-only picture compare at one
session's cameras (4309's method); (c) a 512 pair's bake on another GPU or driver (this check is this RTX 4080's); (d) the
tile's own pass-3a decals in the up-res check (given as the EXE's macro shadow here); (e) the cost of the state-5 bakes in
frame time (2 per settled frame, 1 while moving: a timing series on oldpc, D106, if a claim is ever made).

## 4. Decals (roads, farms, the river bed, model grounds)

*Corrected 2026-11-15, epic 0166: decals are now captured, decoded and reproduced bit for bit; the current description
is `terrain-decals.md` (4: the passes from 7 captured decal bakes; 11: the server's pass). This section keeps the short
form. Replaced: "identified only"; "pass 0 into a 'Decal Layer' target of the size class (TR8+0x6C4[i], set as the
blend's fourth target set) after the blend ... The CacheBake draw object takes the decal layer's texture as well
(0x6CF830's arguments) [C]".*

- When TR8+0x538 != 0, 0x694A60 draws the TerrainDecalSystem batch (0x742EF0) twice [C]. In the live bake [L, 7 decal
  bakes in 21 stock frames, terrain-decals.md 4.1]: **the record-pass-0 decals with TerrainDecalShader's technique pass 1
  (PS `0x067f8f30`, three targets) straight into the blend's three 512 R16G16B16A16_FLOAT targets** (albedo,
  normal/height, spec), mip 0, not cleared, after the blend draw and before the blend's `GenerateMips` and
  NormalCalculate (so NormalCalculate sees the height after the decals), colour SRC_ALPHA / INV_SRC_ALPHA and alpha
  ONE / ONE; then **the record-pass-1 decals (tree shadows) with the same PS into the macro-shadow target alone**, ONE /
  ONE, after the three macro-shadow draws. There is **no separate decal layer** (TR8+0x6C4 binds the same three
  textures again [I]) and **CacheBake has no decal slot**: the decals reach it through t1 albedo, t3 spec and, through
  NormalCalculate, t0. Technique pass 0 (two targets) was never drawn.
- Constants per draw (0x70 bytes): DecalViewProj, (1, 1, 0, 0), the decal's rectangle, its priority. DecalViewProj in
  closed form [L, bit-equal on 6 captured tiles, terrain-decals.md 11.2]: with s = float(1/33), (s, 0, 0, 0), (0, -s, 0,
  0), (0, 0, -0.5, 0), (tx, ty, 0.5, 1), tx = -(originX + 32) s - 1/512, ty = (originY + 32) s + 1/512.
- Which tiles carry decals: those whose square a decal record's rectangle overlaps (terrain-decals.md 1.4, 10.2). The
  server's source is the decal tap of the EXE's records (route (a), terrain-decals.md 9, 10); `bakepost` runs the decal
  pass on the blend targets (`PostInputs::decals`, 4104, terrain-decals.md 11.1).

## 5. Inputs from the CPU and where they come from

| Input | Source in the EXE | Server source |
|---|---|---|
| size class | tile+0x17C (64/128/256/512) | the server's own choice (D88 default 256) |
| tile+0x2FC (needs t8) | 0x568430 sets it when 0xD90F28 is on and a listed layer's set has a spec map | issue 3401's layer list |
| height page + offset | tile+0xEC (A8 1024 x 1024, 11 mips), tile+0xE4/+0xE8 | built from the tapped height field by the rule of 5.0 (`c5r::bake::buildHeightPage`, 3406) |
| CacheBake grid normals | tile+0x308, prepared on the CPU by the vertex jobs (section 5.1) | `c5r::bake::gridNormals` from the height field (3405), bit-exact |
| HalfVector, shadow direction | TerrainFrame registers 21 and 17 | fixed stock values, camera-independent (5.3, `bakeTimeConstants`) |
| normal strength, texel step | 16.0 (`0x9EFBA8`), 1/512 | constants |
| macro shadow count | 0xD90F6C (3) | constant 3 (quality 3) |
| style cubes, LUT | TR8+0x3D0/+0x3E4, 0xD90E4C | game files: `Sunlight_<Style>_Terrain.dds`, `Shadowlight_<Style>_Terrain*.dds`, `Terrain_<Style>_ColorKey.tga` (5.2) |
| grid geometry and indices | the EXE's builders | rebuilt exactly (section 7, test `game_grids_and_constants_match_the_exe`) |

### 5.0 The height page (tile+0xEC) [C + L] (issue 3406, 2026-11-13)

*Rewritten 2026-11-13 05:45 EST by W6 (issue 3406). It replaced a fit by 3403 ("the page stores height / 0.2 as a byte
at u = x + 64, v = 1728 - y [I]"); the fit was right within one row, the rule below is the decoded one.* Static reading:
Ghidra 12.1.4 headless on W6's copy of the 2903 project, decompiles of 0x74A9E0, 0x678290, 0x548980, 0x4241E0;
check: issue 3406's comparison (end of this section). The issue brief called this "section 5.1"; it is 5.0 here (5.1 is
the grid normals, W5's).

**Who and when** [C]. One function, **0x74A9E0** (`this` = TR), builds every height page of the map from the EXE's
height field (the GridHeightmap that `[0xD90FC4]` points at, i.e. 0xD91270, ushort layer). It runs (a) at terrain
creation, from 0x678290 (called by TerrainSystem create 0x5BCEF0, and by 0x60FE40 for a second terrain object), after
the page tables TR+0x12AC and TR+0x16AC (0x400 bytes each) are zeroed; and (b) from the terrain draw 0x4241E0, after the
draw, whenever TR+0x234C < TR+0x2348 (TR+0x2348 is a change stamp, TR+0x234C is set to the frame counter
`[[0x3010570] + 0x33F74]` after each page is uploaded; both zeroed by 0x678290). So the pages exist before the load-time
pre-bake and are rebuilt as a whole (every page) when the height field changes; the vertex jobs and the bake only read
them. Nothing in the bake writes them.

**Allocation** [C, L]. Pages are 1024 x 1024 texels and cover the sample plane in steps of **896** samples with a margin
of 64 samples on each side (896 + 2 x 64 = 1024):

| Quantity | Rule |
|---|---|
| pages in x, y | `(samplesX + 896) / 896`, `(samplesY + 896) / 896`, integer division (Tiny 3,584 x 2,560 -> 5 x 3; Huge 8,192 x 5,376 -> 10 x 7) |
| page (px, py) covers samples | x from px * 896 - 64 to px * 896 + 959, y the same with py, both **wrapped** (below) |
| a tile's page | **px = originX / 896, py = originY / 896** (integer division of tile+0xDC, tile+0xE0) |
| tile+0xE4, tile+0xE8 | **64 - px * 896, 64 - py * 896** (signed; e.g. (64, -1728) for py = 2) |
| tile+0xEC | the page's texture handle, `[TR + 0x12AC + 4 * (px + 16 * py)]` (row of 16 handles per py) |
| second page set | when `[0xD90F24]` != 0, a R8_UINT 1024 x 1024 page (1 mip) of the material byte layer (+0x4C layer), same addressing, handles at TR+0x16AC (not read by the bake); `[0xD90F24]` = 0 in the Tiny and Huge dumps of 3402/3401, so stock builds none [L] |

A tile spans 65 samples from its origin, so it always lies inside its page's 1024 (origin mod 896 <= 895, + 64 <= 959).
Neighbouring pages overlap by 128 samples. Because the count is (samples + 896) / 896, the last column or row of pages
can lie wholly past the map and then holds a wrapped copy: on Tiny page x 4 starts at 3,584 = samplesX and equals page x
0 texel for texel [L, below]. The tile rule was checked on the 121 pre-baked tiles of `C5H-3200-HUGE_0350` (3204's
probes `D:/c5-64bit-work/3200/w3/live/probe_{1,2}*.json`, 2026-11-11): all 121 tiles' (tile+0xE4, +0xE8) equal the rule,
and the four pages they use ((6,1), (6,2), (7,1), (7,2)) each have one handle [L].

**Texels of mip 0** [C]. For page (px, py), texel row v = 0..1023 and column u = 0..1023:

- sample column `sx = (100 * samplesX + px * 896 - 64 + u) mod samplesX` and sample row
  `sy = (100 * samplesY + py * 896 - 64 + (1023 - v)) mod samplesY` (signed division after the bias of 100 widths,
  so the result is always the true modulo). **Both axes wrap, whatever the map's wrap flag** (the height field's own x
  wrap 0x548980 is applied on top and changes nothing); there is no clamping and no zero outside the map. Rows are stored
  bottom-up: texel row v holds sample row py * 896 - 64 + 1023 - v.
- **texel = height >> 8**, the high byte of the field's u16 sample (pooled or constant tile alike). No rounding, no
  scale: since the world height is u16 / 255 * 0.2, the byte is about height / 0.2 (0.2 * 256/255 per step).

**Mips 1-10** (only when BicubicTerrainTessellation `[0xD90F1C]` != 0; else the page has 1 mip) [C]. Mip m + 1 (size
n = 1024 >> (m + 1)) is made from **mip m as stored** (so each level from the sharpened previous level):

1. box: b(x, y) = (t(2x, 2y) + t(2x+1, 2y) + t(2x, 2y+1) + t(2x+1, 2y+1)) >> 2 (integer, truncating);
2. sharpen: S = 9 b(x, y) - (sum of the 8 neighbours of b), neighbours taken with **clamped** indices (edges repeat);
   out = b + ((S - b) >> 3) (arithmetic shift, i.e. floor division by 8), clamped to 0..255.

At 1 x 1 the sharpen is the identity. The texture is created (or, on a rebuild, updated with all mips) from one CPU
buffer, which is freed afterwards: **no CPU copy of a page survives** the build [C], so a page can be compared only
through a capture or by rebuilding it.

**Check against the EXE** [L, 2026-11-13 05:40 EST]: the page of 3403's capture (`frame380.rdc`, Tiny save copy
`C5H-3023-TINY`, stock, DLL `i0ship-bbfacca1-ia`, BicubicTerrainTessellation 1; `D:/c5-64bit-work/3400/w3/ref380/height.c5rt`,
the page bound for tile P) against pages built by this rule from the Tiny height tap of 3402
(`D:/c5-64bit-work/3400/w2/ref-tiny/after/tap.c5hf`, save `C5H-3402-TINY` of `c5h-9028-tiny-t0008`; the two save copies
hold the same height field, as this equality shows): page (0, 2) equals
it **byte for byte in all 11 mips** (1,398,101 bytes); page (4, 2) too (the wrapped copy); the 13 other pages give 5-64 %
(scratch script `D:/c5-64bit-work/3400/w6/pagecheck.py`).

**Every page in the captures** [L, 2026-11-13 06:00 EST]: each RenderDoc export holds the creation data of the height
pages the frame binds (1 to 5 per capture; resource ids rise with px outer, py inner, the builder's loop order). All 27
exports of 3023 run 3 (`C5H-3023-TINY`) and of 3402's two sessions (`C5H-3402-TINY`; `C5H-3402-SLOPE`, the Tiny copy with
Grand Mesa, Gibraltar and Barringer Crater placed by Lua, so with other heights) give **15 distinct pages, 8 of the Tiny
save ((0,0), (0,1), (0,2), (1,1), (2,1), (3,0), (3,1), (3,2)) and 7 of the slope save ((0,0), (0,1), (1,0), (1,1), (2,1),
(3,0), (3,1)); each equals the page the rule builds from its save's height tap, all 11 mips, byte for byte** (extractor
`D:/c5-64bit-work/3400/w6/pages_from_capture.py`, matcher `matchpages.py`; the test `game_captured_pages_equal_rule_all_mips`
rebuilds them with the server code). The tile rule holds for **all 2,240 tiles** of the Tiny save (tile+0xE4/+0xE8 of
3402's dump by PID, `ref-tiny/after/tables.json`, 2026-11-13 03:37) [L]. Not compared: a page of the Huge save (no capture
of a Huge frame exists; the CPU buffer is freed, see above). The rule has no map-size dependence beyond the page count
and the wrap, so the Huge pages are [C] for the texels; their allocation on the Huge save is checked live (5.3, issue
3406's probe).

### 5.1 The CacheBake grid normals (tile+0x308) and the blend's slope classes

Rewritten 2026-11-13 05:40 EST by issue 3405 (W5 of team-3400). The earlier text left the rule open ("traced together
with 3402's weight rule, not settled here") and called the (0, 0, 1) of tile C's last column and row an edge rule to be
traced; both are settled below: there is no edge rule, tile C is flat there. Static reading of 0x5118E0, 0x5CE920,
0x728240 and 0x747580 in the objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm` and 3402's decompiles
(`D:/c5-64bit-work/3400/w2/dec/b4.txt`, `b6.txt`) [C]; checks [L] below.

**Where.** 0x5118E0 allocates the tile's three CPU arrays together [C]: tile+0x30C ((w+2)(h+2) x 40 bytes, job
records), tile+0x304 ((w+2)(h+2) x 48 bytes, the blend vertices of 3402) and tile+0x308 ((w+2)(h+2) x 24 bytes, the
CacheBake vertices). It queues one job ("TerrainCell.cpp") per block of vertex columns x0 .. x1-1 and rows y0 .. y1-1,
blocks starting at -1 in steps of 12 (x1 = min(x0 + 12, w + 1), the same for y), so the last block of a 65-extent tile
is 7 wide. Each job runs **0x5CE920 (tile, x0, x1, y0, y1)**, which fills both arrays for its block. The bake 0x63CCD0
`memcpy`s both arrays into its dynamic vertex buffers and **frees them** (tile+0x304/+0x308 = 0): the arrays exist only
between the jobs and the bake [C].

**The normal of a vertex.** 0x5CE920 first calls **0x728240** (the height-field object 0xD91270, block origin (ox + x0,
oy + y0) not wrapped, width x1 - x0, height y1 - y0, a 13 x 13-entry float3 array on its stack). Vertex (i, j), i = x + 1
for x in x0 .. x1-1, stands on height sample (ox + i - 1, oy + j - 1) and takes the array's entry for that sample (row
stride = the block width; a fallback 0x719BA0 for samples outside a 13 x 13 block is never taken with 12-wide blocks).

- **0x728240 is 0x747580** (terrain-bake-records.md 2.2a, 3401's port) with a smaller window [C: the two listings,
  addresses removed and jump targets normalised, differ only in the window constants]: 16 x 16 samples (block + 2 on
  each side; stride 16) instead of 76 x 76, and the frame size. Fetch (x wrapped `(samplesX * 100 + x) mod samplesX` when
  the map wraps, then x clamped to 0 .. samplesX-1, **y clamped, never wrapped**), the integer central differences, the
  two 16-tap weighted sums with the plane-prediction quirk, the kernels at height field +0xFF68/+0xFFCC, the plot
  steps (1.0, 0.866, 0.2) and the call of D3DXVec3Normalize are the same instructions. Output entry (r - 2) * width +
  (c - 2) = normal of window sample (c, r) = height sample (bx + c - 2, by + r - 2).
- Each normal depends only on its sample's 5 x 5 neighbourhood, fetched by absolute coordinates, so **the normal is a
  pure function of the sample** (block split, window origin and job order change nothing); 0x747580's 69 x 69 grid
  and 0x728240's blocks give the same value for the same sample. One port serves the list builder (3401), the slope
  classes (3402) and the grid normals (3403).
- **Edges.** The tile's grid has (ex+2) x (ey+2) vertices (67 x 67, or 66 in the last column / row with extent 64); its
  samples run from ox-1 to ox+ex. No special rule: the first row of tile row 0 (sample y = -1) and the last row of the
  last tile row (sample y = samplesY) read clamped rows; on a wrapping map the last column's right margin reads column
  0 and the first column's left margin the last column. (The blend vertex's *height* wraps y, 3402's 2.0; the normal
  clamps it.) Tile C's last column is (0, 0, 1) because its heights there are 0, not by rule. Flat ground above
  height 0 is *not* level: the plane-prediction quirk (the prediction advances by 1.0 per column, so the low and high
  side taps get different weights) leans the normal by roughly height * 7.4e-7 in x and 8.5e-7 in y (0.0091, 0.0105 at
  u16 height 12345) [C, computed with the rule; test `flat_field_at_height_0_..._tilts_higher_flat_ground`].
- **D3DXVec3Normalize** (the import at 0x9D6B88, `d3dx9_42.dll`) [L, 2026-11-13 05:30, a 32-bit probe calling the
  system's `C:/Windows/SysWOW64/D3DX9_42.dll` on 1,000,000 random vectors (v.z = 0.866, v.x, v.y in -2..2) on this
  workstation (Intel i9-13900KS)]: length squared l = (x*x + y*y) + z*z in single precision, then the processor's
  reciprocal square root estimate r = `rsqrtss`(l), one Newton step n = (0.5 * r) * (3 - (l * r) * r), and x * n, y * n,
  z * n: **1,000,000 of 1,000,000 bit-equal**. Dividing by sqrt(l): 26.7 %; multiplying by 1 / sqrt(l): 54.1 %; the bare
  estimate: 0.03 %. This corrects the [I] "v / sqrt(x^2+y^2+z^2)" of terrain-bake-records.md 2.2a (3401's normalGrid
  divides; its lists are unaffected, the closest n.z there was 2e-5 from the threshold). **`rsqrtss` is not specified
  bit for bit across processor vendors** (Intel and AMD tables differ): the EXE's normals, and so a port's, can differ
  in the last bits between machines; on one machine the server's `_mm_rsqrt_ss` gives the EXE's value [I for other
  machines; not measured on AMD].
- **The CacheBake vertex** (24 bytes) [C, field order from the listing]: POSITION f32x3 (2.6 of terrain-bake-blend.md),
  **NORMAL = (nx, ny, nz, 1.0) as halves**, each made by its own D3DXFloat32To16Array call, TEXCOORD0 = (i / (w+1),
  j / (h+1)) as halves. D3DXFloat32To16Array rounds to nearest, ties to even [L: the probe's 3,000,000 conversions equal
  numpy's RNE].
- **The slope class** (terrain-bake-blend.md 2.1) is taken from the same single-precision normal before the halves are
  made: nz > 0.9 (float 0x3F666666, `comiss`/`jbe`, so NaN gives a slope class) -> 0; else 1 when |ny| > |nx| (x87
  `fabs`, `fcomip`, strict), else 2 [C].

**Checks** [L, 2026-11-13]:

| Reference | Heights | Vertices compared | Equal |
|---|---|---|---|
| 7 bakes of 3402's first capture session (Tiny `C5H-3402-TINY`, c5h-c, DLL `i0ship-bbfacca1-ia`, stock) | the session's tap (`ref-tiny/after/tap.c5hf`) | 31,423 (all, grid edges included) | 31,423 |
| 17 bakes of 3402's second session (`C5H-3402-SLOPE`, c5h-a, same DLL) | `ref-slope/after/tap.c5hf` | 76,313 | 76,313 |
| tiles P and C of frame380 (3023 run 3, `C5H-3023-TINY`) | recovered from the blend vertices' heights (exact inverse) | 8,190 (vertices whose 5 x 5 neighbourhood is in the two grids) | 8,190 |
| tile (48, 13) read by PID from tile+0x308 (5.1a, `C5H-3405-TINY_0008`, c5h-e) | the same session, by PID | 4,489 | 4,489 |

Blend vertices with these slope classes (test `game_blend_vertices_with_these_slope_classes_equal_the_exe`, render
suite 2026-11-13 07:02): the 24 captured bakes and the PID catch, list from 3401's builder, weights from 3402's rule,
classes from `tileSlopeClasses`: **112,225 / 112,225 48-byte vertices equal**. 19 of the 25 tiles have class 1 or 2
vertices, but no slope-projected material lies within the kernel of any of them (classes all 0 change nothing there),
so this shows that the classes do no harm, not that the slope path is right; the slope path's own evidence is the
normal's bit-exactness above. On the slope save (`C5H-3402-SLOPE`'s tap) the 10 tiles with slope-projected samples
have 4,797 such vertices: class 0 1,924, class 1 1,689, class 2 1,184 (test `game_slope_save_tiles_have_slope_classes`).

The CacheBake vertex buffers were cut from 3402's RenderDoc exports with `render/tests/bakegrid/tools/grid_ref.py`
(a blend draw is paired with the next 24-byte-stride draw of the same vertex count: each bake pass is recorded on its
own deferred context, so pairing by context finds nothing). With the normalisation as division, multiplication by the
exact reciprocal or in double precision, 19, 14 and 24 of the 107,736 vertices differ, each by one half-ulp at a
rounding tie; with D3DX's estimate plus Newton step none. Live by PID: 5.1a.

#### 5.1a Reading tile+0x304 / +0x308 by PID (2026-11-13, issue 3405) [L]

The arrays are short-lived: 0x5118E0 allocates them, the jobs fill them and the bake frees them, all within about a
frame. `shell/tools/layer_probe.py --grid-arrays DIR` sweeps every tile's +0x304/+0x308 (the tile records lie in a few
heap runs, so one read per run: about 70 sweeps a second on Tiny, 30 on the huge map) and keeps a pair only when two
back-to-back reads agree with the pointers unchanged, or (marker `single_read`) one read of a complete 24-byte array
taken while the pointers were still set. Sessions on Goldberg instance c5h-e, CP/VP/EUI, stock `config.ini`, DLL
`i0ship-bbfacca1-ia` (SHA-256 E589306E...5C54, the current i0-ship pin), sweeping from the launch through the load and
while `UI.LookAt` moved the camera over the slope places and the map's x edges:

| Session (save copy) | Time | Sweeps | Pointer hits | Kept |
|---|---|---|---|---|
| `C5H-3405-SLOPE` (of 3402's `C5H-3402-SLOPE`) | 06:52-06:55, 40 ms between reads | 1,540 | 43 | 0 (all changed between reads) |
| `C5H-3405-TINY_0008` (of `c5h-9028-tiny-t0008`) | 06:56-06:58, back-to-back reads | 579 | 47 | **1**: tile (48, 13), caught 06:58:03 at the load screen |
| `C5H-3405-HUGE_0350` (of `C5H-VP8P-HUGE_0350`) | 06:58-07:01 | 441 | 0 (the 150 s ended at the load screen) | 0 |
| slope again, bulk sweep | 07:05-07:08 | 25,012 | 249 | 0 (freed before the read finished) |
| Tiny again, 24-byte array read first | 07:09-07:11 | 10,504 | 256 | 0 (freed) |
| huge again, 300 s | 07:12-07:17 | 8,502 | 252 | 0 (freed) |

So the pointers are seen about once a second, but the arrays are freed before a 100-300 KB ReadProcessMemory
completes in all but one case: the PID route gives the rule an occasional live sample, not a survey; RenderDoc
captures of the bake (3402's route) remain the bulk reference. **Result of the catch: tile (48, 13) of the Tiny save,
4,489 / 4,489 NORMAL halves equal to `gridNormals` + `packGridNormals`** computed from the same session's height field
(`tap_live.py rawdump` + `hfbake decode`, read 06:58 by PID). No tile in the last column or row and no slope tile was
caught live, so those cases rest on the static reading (5.1 "Edges") and the captured bakes (none of which is an
edge tile either: on Tiny they are water or polar). Data: `D:/c5-64bit-work/3400/w5/live/` (game data, not committed).

**Implementation**: `render/src/bakegrid/grid.cpp`, `render/include/c5r/bake/grid.h` (`c5r::bake`): `sampleNormals`
(any window, any coordinates), `gridNormals` (a tile's (ex+2) x (ey+2) float3, the input `bakepost` takes as
`gridNormals`), `packGridNormals` (the NORMAL halves), `floatToHalf` / `halfToFloat`, `slopeClassesFromNormals` and
`tileSlopeClasses` (the `SlopeClasses` `fillBlendVertices` takes). `bakepost`'s `buildCacheGrid` converts the floats with
DirectXMath's `XMConvertFloatToHalf`, which also rounds to nearest even: equal halves (test
`pack_matches_bakepost_cache_grid_conversion`).

Superseded approximation (kept for the record) [L, 2026-11-13 03:44, test `game_tile_p_with_normals_from_vertex_heights`]: the
normal (-dz/dx, -dz/dy, 1) normalised, from central differences of the blend grid's vertex heights (TEXCOORD1.z; x
step 1, y step 0.866; one-sided at the edges), differs from the EXE's normals by a mean of 0.0012 (tile C) and 0.017
(tile P) per component, max 0.065 / 0.089 (Sobel or face-normal averaging give the same, so the EXE's source is not the
vertex heights alone). Baking tile P with these normals: CacheBake colour 84 % of bytes equal (max difference 6),
**t0 BC1 blocks 48.7 % equal, decoded RGB p95 3 and max 17 of 255**; t8 unchanged (100 %; tile P's spec layer is
empty in rgb, only its alpha moved). So the exact normal rule is needed for the D66 goal; the approximation is a
fallback.

### 5.2 The style inputs from the install [C names, L contents]

- Names (the EXE's string table near VA 0x627600 of the file): `assets\terrain\lighting\Sunlight_<Style>_Terrain.dds`
  for all five styles; `Shadowlight_<Style>_Terrain.dds` for Generic and Africa, `Shadowlight_<Style>_Terrain_2.dds`
  for America, Asia and Europe. Styles 0..4 = Generic, America, Asia, Africa, Europe.
- The capture's tile P is **Generic**: its Sunlight cube equals `Sunlight_Generic_Terrain.dds` (and also the America,
  Asia and Europe files, which have the same content), its Shadowlight cube equals only `Shadowlight_Generic_Terrain.dds`
  (2026-11-13, install `%USERPROFILE%/Desktop/pure_vp`, read through `c5r::gamefs`, byte-equal on all 48 subresources).
  Both are BC1 128 x 128 cubes with 8 mips, viewed as BC1_UNORM_SRGB.
- **The ColorKey LUT rule** (settles the [I] of asset-pipeline.md 3.5) [L]: decode the 32-bit uncompressed TGA into
  reading order (its descriptor says bottom-left origin, so the last file row comes first), convert each pixel from the
  file's BGRA to RGBA, and take the first 32^3 = 32,768 pixels (the 1600-pixel rows run on: x fastest, then y, then z) as
  the R8G8B8A8 volume. This equals the capture's Terrain LUT byte for byte, alpha included; the other three orders
  (file row order, BGRA kept) give 0.15-4.9 %. All five `Terrain_<Style>_ColorKey.tga` of this install give the same
  volume.

### 5.3 Bake-time constants (issue 3406, 2026-11-13)

Written by W6 (issue 3406), static reading as in 5.0 (decompiles of 0x4241E0, 0x4AE6F0 call site, 0x6ADD60, 0x5996E0,
0x5BCEF0, 0x457C30, the TerrainSystem set-up around 0x6B8440, 0x4B2B40, 0x7BA820 call site; constants read from the
EXE file), values checked against the TerrainFrame of 3403's capture (`ref380/P_frame.bin`, the bake of tile P).

**The TerrainFrame the bake sees is a fixed "bake view", not the camera** [C]. Both bake drivers write the shared
TerrainFrame buffer (0x6ADD60) with **identity matrices for every argument** right before they bake: the terrain draw
0x4241E0 does it first thing, before it bakes the dirty list (0x627810 per tile) and before the per-view 0x6ADD60
calls of the draw itself; the load-time pre-bake 0x4AE6F0 does the same before its 4 tiles. So at bake time ViewProj =
identity, CameraPos = (0, 0, 0), CameraAxisZ = (0, 0, 1), ShadowMatrix0 = OverlayProj = identity [C; L: these are the
values of the capture's bake, which ran in the load screen].

**Answer for residency (issue 3407): t8 does not depend on the camera.** HalfVector (register 21) =
normalize(LightDir + (0, 0, 1)) at every bake, whatever the view. A resident t0/t8 needs no re-bake when the camera
turns or moves. It depends only on LightDir and AuxDir, which are constant for a session (below). *This corrects the
[I] of section 3.3 and of open point 3 in section 9 (2026-11-13 05:50, W6): "t8 depends on the camera at the time of the
bake" is wrong.* (What does depend on the camera is the size class, below, i.e. the cache resolution, not its content
per texel.)

| Constant (TerrainFrame register) | Source at bake time | Value (stock, every map) | Server rule |
|---|---|---|---|
| AuxDir = shadow direction (17) | TR+0x400..+0x408, set by 0x5996E0 from TerrainSystem TS+0x7572C..+0x75734 | **(0.4330127, -0.25, 0.8660254)**, float bits `3EDDB3D7 BE800000 3F5DB3D7` | dir(zenith 30 deg, azimuth 150 deg) |
| LightDir (18) | TR+0x40C..+0x414 from TS+0x75738..+0x75740 | **(-2.5877e-17, -0.42261827, 0.90630782)**, bits `A3EEAC75 BED8616C 3F6803CA` | dir(zenith 25 deg, azimuth 90 deg); use the bits |
| CameraPos (19), CameraAxisZ (20) | identity bake view | (0, 0, 0), (0, 0, 1) | constants |
| HalfVector (21) | normalize(LightDir + CameraAxisZ) | **(-1.3253e-17, -0.21643960, 0.97629601)**, bits `A37477F2 BE5DA258 3F79EE89` | s = LightDir + (0,0,1) in float; s * (1 / sqrtf(dot(s, s))) in float reproduces the bits exactly (a double-precision normalize is 1 ulp off in y) |
| HexScale (16) | TR+0x1ACC..+0x1AD4 = (1.0, 0.866, 0.2) | height unit 0.2 (16.z) | constants |
| HeightTexelScale (22) | constant | (1/1024, -1/1024) | constant |
| BakeTexelScale (23) | constant | (1/512, 1/512) | constant |
| MapSize (24) | TR+0x1AB0/+0x1AB4 = the height field's **samples** (not plots) | Tiny (3584, 2560) | samplesX, samplesY (read by no bake shader) |
| tile offset (own cb0[73].xy) | float(tile+0xE4), float(tile+0xE8) | 5.0 | `heightPageOffset` |

dir(zenith z, azimuth a) = (-sin z cos a, -sin z sin a, cos z), computed by the EXE in x87 extended precision from double
radians and stored as float. With double arithmetic the server gets AuxDir, LightDir.y/z bit for bit, but LightDir.x
(the -sin(25 deg) cos(90 deg) residue, about -2.6e-17) differs in its last bits; the server therefore uses the float
bits above for the stock values and the formula only for non-stock angles. (The residue does not matter in practice; it
matters for bit-exact HalfVector.x.)

**Where the angles come from** [C]. The TerrainSystem set-up (around 0x6B8440) writes compile-time constants: light
azimuth TS+0x75718 = 90, light zenith TS+0x7571C = 25, a second pair TS+0x75720 = 80 / TS+0x75724 = 135 (a third
direction at TS+0x75744, not read by the bake), TS+0x75728 = 0.5; and computes AuxDir from zenith pi/6, azimuth 5 pi / 6
and LightDir from 25 deg / 90 deg (as doubles in radians). TerrainSystem create 0x5BCEF0 then calls 0x678290 (which
puts the placeholder AuxDir (0.707, 0, 0.707) and LightDir (0.707, 0, 0.707) into TR+0x400/+0x40C) and **immediately
after** 0x5996E0 with the TS values. So the (0.707, 0, 0.707) of asset-pipeline.md 3.3 is only the value between those
two calls; it is never seen by a bake or a frame. The only other writer, 0x457C30, recomputes LightDir and the third
direction from the four angles of a debug tuning panel (it formats them with "%f"); it is not called in normal play [I:
its trigger not traced]. Nothing reads the map, the era, the time of day or the player's settings into these values.
**Server rule: the stock constants above for every map, every tile, every session.**

**The terrain style of a tile** [C, I where marked]. CacheBake binds the Sunlight cube TR8+0x3D0 + 4 s, the
Shadowlight cube TR8+0x3E4 + 4 s and the ColorKey LUT 0xD90E4C + 4 s for s = the lowest set bit 1..4 of tile+0x138,
0 (Generic) when none (0x6CF830). 0x678290 sets tile+0x138: for each plot in the tile's plot list (tile+0x04, 12 bytes
per plot, count tile+0xC4) it maps the plot's coordinates through an integer matrix in the height-field object (at
+0xC8..+0xF4 of `[0xD90FC4]`) and ORs `1 << b` into tile+0x138, b = a byte of a per-hex table at +0x164 of the same
object (row stride 255 plus y / 2). Bit 0 (b = 0) is ignored by the choice. The five styles Generic, America, Asia,
Africa, Europe match the DLL's ContinentArtTypes (NONE, AMERICAS, ASIA, AFRICA, EUROPE; `Plot:GetContinentArtType()`)
**[I: the filler of the +0x164 table was not traced]**. So the server's style of a tile = the lowest nonzero continent
art type among the plots the tile covers, Generic if all are 0. The 121 pre-baked tiles of the Huge save carry
tile+0x138 = 8, 9 or 1 (3204's probes): style 3 (Africa) or Generic.

**The size class** (tile+0x17C used, tile+0x180 wanted) [C]: the cull 0x7BA820 sets tile+0x180 = 0x4B2B40(tile,
ViewProj of the view): p = 0x744AE0 (the tile's projected size [I: in pixels]); 512 if p > 256, else 256 if p > 128,
else 128 if p > 64, else 64; then raised to at least `[0xD90F30]`, the minimum class set at start-up from
TerrainDetailLevel (`[0xD90F74]`); **at TerrainDetailLevel 3 (stock high) `[0xD90F30]` = 256** [L: the globals dumped by
PID by 3402 (Tiny, 03:37) and 3401 (Huge, 03:37), both `0xD90F30` = 256, `0xD90F74` = 3], so every cache there is 256 or
512 and the 64/128 classes never occur. A tile whose wanted class differs from its current one is re-baked when the pool
allows. **The size class is camera-dependent; the content per texel is not.** Server default for baking ahead: 256 (the
class of every cache 3204 read at the load view, and D88's default); a camera-driven server may apply the same four
thresholds to its own projected tile size.

**Live check on the Huge save** [L, 2026-11-13 07:25 EST]: Goldberg instance c5h-c (claimed, backed up by SHA-256 and
restored), stock config, DLL `i0ship-bbfacca1-ia` (E589306E..., the current i0-ship pin), save copy `C5H-3406-HUGE_0350`
of `C5H-VP8P-HUGE_0350` (deleted after), in game at turn 350, read by PID (`D:/c5-64bit-work/3400/w6/probe.py`, read
only) and through the Lua channel; data in `D:/c5-64bit-work/3400/w6/live/` and the test's `ref/`:

- TR+0x400..+0x414 and TS+0x7572C..+0x75740 hold exactly the stock float bits of the table above; TS+0x75718..+0x75728 =
  90, 25, 80, 135, 0.5. TR+0x2348 = 0, TR+0x234C = 19 (no page rebuild pending).
- **All 10,752 tiles** follow the page/offset rule of 5.0; the 70 entries of TR+0x12AC (10 x 7 pages) are all set; the
  60 pages that tiles use each carry exactly one handle, equal to the table entry (test
  `game_huge_every_tile_page_and_offset`). TR+0x16AC is empty (`[0xD90F24]` = 0). `[0xD90F30]` = 256, `[0xD90F74]` = 3.
- Size classes (tile+0x17C used / +0x180 wanted) at the load view: 256/512 for 76 tiles, 256/256 for 8, 256/0 for 184,
  0/0 for the rest.
- **The style table is the DLL's continent art type per plot**: byte `(y + 2) * 255 + x` of the table at +0x164 of the
  height-field object equals `Map.GetPlotByIndex(y * 128 + x):GetContinentArtType()` for **all 10,240 plots** (row
  stride 255, two margin rows; values 0 for 7,551 plots, 1: 38, 2: 43, 3: 1,345, 4: 1,263). And tile+0x138 equals the OR of
  `1 << table[...]` over the tile's plot list, decoded as 0x678290 does, for **all 10,752 tiles**. The plot list names
  plots of plot rows ty - 2 and ty - 1: 4 plots in 8,651 tiles (even ty: (tx-1, ty-1), (tx, ty-2), (tx, ty-1), (tx+1,
  ty-2); odd ty: (tx-1, ty-2), (tx, ty-2), (tx, ty-1), (tx+1, ty-1)), 5 in 1,763 (one more of x - 1), 0-3 at the edge rows.
  ~~The server's 4-plot rule (`tileStyle`) gives the EXE's style for 10,748 of 10,752 tiles; the 4 others list a fifth
  plot whose art type is lower. What decides the fifth plot was not traced.~~ (replaced 2026-11-16 by 0383, next
  bullet.) *This settles the style source above: continent art types [L]; the filler of the table is still not traced
  statically.*
- **The fifth plot and the edges (ticket 0383, 2026-11-16, from 3406's probe files above, read only; no decompile)**
  [L: the EXE's own lists and +0x138 of two live sessions]. Decoding every tile's list (tile+0x04, cube triples through
  the matrix at +0xC8: table column X = a - 1, table row Y = b + 2, plot x = X + (Y >> 1)) on both saves:
  - **Which tiles list five**: exactly the tiles with **1 <= tx <= (ty + 1) / 2** (integer division), a triangle at the
    map's left edge that widens by one tile column every two tile rows: 400 of 2,240 tiles on Tiny (56 x 40 tiles),
    1,764 of 10,752 on huge (128 x 84; the sum over ty of (ty + 1) / 2). Of those, 399 and 1,763 lists hold five plots
    (3406's counts); the one more is tile (1, 1), whose list holds the fifth plot (0, 0) but only 3 plots in all (two of
    its four are above the map). Only tx and ty decide; the map's content does not.
  - **The fifth plot** is in column tx - 1: (tx-1, ty-2) for even ty, (tx-1, ty-1) for odd ty, i.e. the 4-plot
    pattern's missing corner of column tx - 1.
  - **No x wrap**: the lists of tile column 0 name no plot of column W - 1 and those of column W - 1 none of column 0
    (3 plots each) on both maps, which wrap. The old server rule wrapped x; that was the third Tiny difference
    ((0, 21): the wrap reached an Asia plot of column 55, the EXE says Generic).
  - Rows: the lists of the bottom tile rows also name plots of rows y >= mapH; those sit in the table's margin rows and
    add only bit 0, which the style ignores, so skipping them changes no style. Tile row 1 follows the same rule (tile
    (1, 1) lists the fifth plot (0, 0)).
  - **Result**: the four plots plus the fifth, without wrap, give the EXE's plot list exactly for 2,072 of 2,240 Tiny
    and 10,288 of 10,752 huge tiles; every other list differs only by plots outside the map rows (168 and 464 tiles), and
    **the EXE's style for all 2,240 Tiny and all 10,752 huge tiles**: Tiny counts Generic 1,551 / America 353 / Asia 336,
    FNV-1a 64 (row-major style bytes, `ResidencySource::stylesFnv`) `fa4b79fa78260c58`, the EXE's FNV of 4305's live
    product-path session on the same map (`terrain-surface.md` 12.6); huge Generic 7,531 / America 151 / Asia 111 /
    Africa 1,509 / Europe 1,450, FNV `52c0219ec8b791d1`, the EXE's counts of 13.7's live product-path session. Each part
    alone is not enough: the fifth plot with wrap leaves (0, 21) on Tiny; no wrap without the fifth plot leaves 2 Tiny and
    4 huge tiles. Scratch: `C:/c5-64bit-work/0383/an2.py` (lists), `an3.py` (styles, FNV), deleted at hand-back; the
    rule is in `heightpage.cpp` `tileStyle` / `tileListsFifthPlot` and its tests.
  - Why the EXE's lists take that shape is inferred, not traced: the triangle is where the tile's axial column
    (tx - (ty + 1) / 2) is <= 0, which fits a hex range computed with rounding toward zero on negative coordinates [I].
- **The same on the Tiny save** [L, 07:29, same instance and pins, save copy `C5H-3406-TINY_0008` of
  `c5h-9028-tiny-t0008`, deleted after]: TR light bits stock; all 2,240 tiles follow the page/offset rule; 15 page
  handles (5 x 3), the 12 used pages each one handle equal to the table; size classes 256/512 for 43 tiles, 256/0 for
  1,260; style from the 4-plot rule equal to tile+0x138's for 2,237 of 2,240 tiles (689 not Generic).

**What the server implements** (`render/include/c5r/bake/heightpage.h`, `render/src/bakeheight/`): `heightPageCount`,
`heightPageForTile`, `heightPageOffset`, `buildHeightPage` (all 11 mips, from a `c5r::heightfield::HeightGrid`),
`buildHeightPageTexture` (an A8_UNORM D3D11 texture with those mips), `bakeTimeConstants` (the table above for a tile:
light, aux, half vector, height unit and texel scale, offset, style from the plots' continent art types, size class
256), `tileStyle` (the plot-list rule above from a per-plot art-type array: four plots, the fifth plot of
`tileListsFifthPlot`, no x wrap; since 0383, 2026-11-16, was the 4-plot rule with wrap), `styleFromBits`, `styleFromArtTypes`,
`lightDirection`, `bakeHalfVector`, `sizeClassForProjectedSize`. Test `c5r_bakeheight_test` (14 tests, 2026-11-13
07:33): synthetic rules and negative inputs (empty or inconsistent grid, short height array, page or tile outside, mip
count, odd mip source, null device, wrong level sizes), an upload round trip on a D3D11 device, and against the EXE: the
captured Tiny page of tile P (all 11 mips), all 15 captured pages of two saves, tile P's TerrainFrame bits, every tile of
both saves (page, offset, handle), the style of every tile of both saves. Expected data: `C5R_HEIGHT_REF` (default
`D:/c5-64bit-work/3400/w6/ref`, 201 MB, SHA-256 in its `manifest.txt`; game data, not committed).

## 6. Tools and what did not work

- RenderDoc replay API: the release folder `%USERPROFILE%/tools/renderdoc-1.47` has no `renderdoc.pyd` (`pymodules/` is
  absent); only `qrenderdoc.exe` embeds Python 3.8 with the module, and the harness does not start `qrenderdoc` (rdc.py).
  The C++ replay headers are not shipped either. **No replay was needed**: the capture's initial contents already hold
  the EXE's own results of the bake that ran just before the frame (section 7).
- Scratch scripts (not committed, game data in their outputs): `D:/c5-64bit-work/3400/w3/scripts/` (`dumpxml.py`: one
  line per call and per created object from the XML export; `ident.py`: capture shader -> EXE VA; `resolve.py`,
  `cbdump.py`, `vbdump.py`, `gridcheck*.py`, `lutcheck.py`, `heightpage.py`). Decompiles of this issue:
  `D:/c5-64bit-work/3400/w3/dec/`.

## 7. Server implementation and the comparison

**Reference: tile P, the EXE's own bake** [L]. RenderDoc stores, for every resource the frame writes, its contents at
the start of the frame. The bake's scratch targets are reused by every bake, so their initial contents in
`frame380.rdc` are the complete result of the bake that ran one frame earlier, of tile **P = origin (640, 2112)**, the
neighbour of the frame's own tile C (640, 2176): the three blend targets with their 10 mips (the EXE's `GenerateMips`),
the normal and macro-shadow targets (10 mips), both RGBA8 caches (6 mips) and both R32G32_UINT block targets (6 mips),
whose bytes are exactly what the `CopyResource` put into P's t0 and t8 (the deferred lists recorded before the frame
copy them into P's caches). P's CacheBake grid and constants are the initial contents of the dynamic buffers
(TerrainFrame differs from the frame's only in register 9, the FOW animation). `render/tests/bakepost/mkref.py` writes
these to `C5R_BAKE_REF` (here `D:/c5-64bit-work/3400/w3/ref380`, 27 files with a SHA-256 manifest, game data, not
committed).

**Implementation** (`render/src/bakepost/post.cpp`, `style.cpp`, `render/include/c5r/bake/post.h`): passes 2-8 of
section 2 on one D3D11 device with the EXE's SM5 blobs read from `CivilizationV_DX11.exe` at run time (each checked
against what the pass binds: stage, declared textures with their dimensions, constant-buffer slots and sizes, VS inputs
against the vertex layout, PS target count); the server builds the full-target quad, the 67 x 67 grids (the EXE's
index order and diagonals, positions and sample coordinates bit for bit, checked against both tiles' buffers), all
constant buffers and states itself. Inputs: the three blend targets, the height page and its offset, the grid normals,
the style views (from the install or given), the size class, the t8 flag and the light/camera values.

**Result, 2026-11-13 03:31 EST** (RTX 4080, this workstation, Release build of `ba57e27`, test
`c5r_bakepost_test`, 13 tests, 0 failed, 0 skipped; a 14th test with approximate normals was added at 03:44, 5.1):

| Compared with the EXE's own target of tile P | Equal |
|---|---|
| Normal Calculation, 512 RGBA8, all 10 mips | 100 % of bytes |
| Macro Shadow, all 10 mips | 100 % |
| CacheBake target 0 and target 1, 256 RGBA8, all 6 mips | 100 % |
| **t0: BC1 blocks, mips 0-5** | **100 % (5,460 of 5,460 blocks)**; decoded RGB median / p95 / max abs difference 0 / 0 / 0 |
| **t8: BC1 blocks, mips 0-5** | **100 %**; decoded 0 / 0 / 0 |
| the same with the blend mips made by the server's own `GenerateMips` from mip 0 | 100 % everywhere; the server's blend mips 1-9 equal the EXE's byte for byte |
| the same with the cubes and the LUT read from the install (style Generic) | 100 % everywhere |
| a second run on a new device | identical blocks |

So on this GPU and driver the server's post-blend stage reproduces the EXE's caches exactly; the BC1 encoding is
deterministic and reproducible. Difference images (all black): `<C5R_WORK>/bakepost/t0_mip0_diff_x16.bmp`,
`t8_mip0_diff_x16.bmp` under the build tree's `test-out`. Not shown by this test: another GPU or driver (the EXE on that
GPU would be compared there, by the same test, with a capture taken there).

## 8. API (`c5r::bake`, `render/include/c5r/bake/post.h`)

- `BakePost::init(device, ShaderSection, PostShaderVas)`: loads and checks the nine blobs, creates layouts, states,
  quad, grid index buffer, constant buffers and the 512 targets. `run(ctx, PostInputs, PostOutputs)`: the passes; makes
  per-size-class targets on first use; returns new BC1_TYPELESS t0 (and t8 when `needSecond`) with
  `cacheMips(sizeClass)` mips; `target(PostTarget)` exposes the intermediates.
- `PostInputs`: `blend[3]` (512 R16G16B16A16, 10 mips, RT|SR, GENERATE_MIPS), `blendMipsReady`, `heightPage` + `k.heightOffset`,
  `originX/Y`, `gridNormals` (67 x 67 x 3), `sunCube`, `shadowCube`, `colorKey`, `sizeClass`, `needSecond`,
  `decalLayer` (the hook: must be null, else Unsupported; *corrected 2026-11-15, epic 0166: replaced by `decals` (a
  `TileDecalList*`) and `decalPass`, the decal pass on the blend targets, terrain-decals.md 11.1*), `k` (`PostConstants`: half vector, shadow direction, height
  unit and texel scale, normal step and strength, shadow draws; defaults = the load-screen values of the capture).
  *Added 2026-11-15 (issue 0332, 3.5.3): `nearCache[2]`, the near pair of the state-5 bake (CacheBake at its size and
  `GenerateMips`), with `nearCacheMips`, `nearCacheBytes`, `createNearCache`.*
- Helpers: `buildGridIndices`, `buildShadowGrid`, `buildCacheGrid`, `cacheMips`, `styleName`, `sunlightFile`,
  `shadowlightFile`, `colorKeyFile`, `buildColorKeyLut`, `loadStyleInputs` (through `c5r::gamefs`).
- Errors (each with a negative test): no device / bad blob stage or declarations BadLayout, missing blob NotFound, run
  before init NotFound, wrong blend format / size / mips / bind flags, missing or wrong-dimension height page, cubes or
  LUT, wrong normal count, size class not 64/128/256/512, shadow draws 0 BadLayout, decal hook Unsupported; TGA errors
  Truncated / Unsupported / BadLayout; missing style file NotFound. Outputs stay null after a failure.

## 9. Not verified, open points

- ~~The CacheBake grid normals (tile+0x308): rule not decoded~~ settled 2026-11-13 by 3405 (5.1): the height field's
  normal filter (0x728240 = 0x747580), D3DX's normalisation, halves rounded to nearest even; bit-exact on 107,736
  captured vertices. Open there: other processor vendors (`rsqrtss`).
- ~~The height page tile+0xEC: layout fitted, not decoded~~ Decoded 2026-11-13 (W6, 3406, section 5.0): builder
  0x74A9E0, rule and allocation [C], equal to the captured page in all mips [L].
- ~~Whether in play the TerrainFrame in the buffer at bake time is the last view's~~ Settled 2026-11-13 (W6, 3406,
  section 5.3): it is not; every bake sees an identity view, t8 does not depend on the camera [C].
- Only one tile (P, style Generic, one material layer, no decals, size class 256, load screen) was compared; tiles with
  several layers, other styles, other size classes, near caches (state 5) and the non-array mode were not captured.
  *2026-11-15 (0332, 3.5.3): the near pair at 256 is checked on tile P (mips 0-5 against the EXE's CacheBake, the rest
  against its chain) and every captured near chain equals `GenerateMips` of its mip 0; mip 0 of a 512 pair against the EXE
  is still open (needs a capture of a frame with an up-res bake).* *2026-11-17 (0371, 3.5.4): a second tile, (35, 16) of
  the Tiny 0009V save, state 5 at 512 with t8, in play: every target and both near caches equal, mip 0 included (the
  macro shadow given: its decals were not in the capture).*
  W1's (3401) reference caches of every cached tile would extend the comparison once the blend inputs exist.
- ~~Decal pass details (outside the epic: identified only)~~ settled 2026-11-15 by epic 0166 (section 4,
  terrain-decals.md 4 and 11: the passes captured, the server's decal pass bit-equal on four reference tiles). The AuxDir value at the load screen, (0.4330, -0.25,
  0.8660), differs from the (0.707, 0, 0.707) that asset-pipeline.md 3.3 reads from the start-up 0x678290 [L vs C]:
  *settled 2026-11-13 (W6, 3406, section 5.3): 0x5BCEF0 overwrites the placeholder right after 0x678290 with the
  TerrainSystem's fixed directions; (0.4330, -0.25, 0.8660) is the value of every map [C].*
- Other shader levels (sets 4_0/4_1): their CompressBC/CacheBake blobs differ; only 5_0 was captured and checked.
- (3406, 2026-11-13) The height pages of a Huge map were not compared texel for texel (no capture of a Huge frame; 5.0).
  The filler of the per-hex style table at +0x164 of the height-field object was not traced statically; the style
  rule (continent art types) is checked against the live probe in 5.3. The debug tuner 0x457C30 that can change
  LightDir: its trigger was not traced. Other users of the pages: the water draw divides by 896 too (0x739FE9,
  0x73A497) and so reads the same pages [I, not traced].
