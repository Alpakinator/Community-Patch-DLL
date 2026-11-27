# Terrain decals: roads, railroads, farm fields, improvement grounds and the river bed (epic 0166)

Started 2026-11-15 00:29 EST by lead-0166 as a skeleton; sections are filled by tasks 4101 (the decal store, the
classes and producers, randomness, the change signal, the route) and 4109 (the decal passes from a RenderDoc capture,
the feed's `GameplayPlotStateChange` rate). Design before code: no code in `render/`, `server/`, `shell/composite/`
until the MC has reviewed this note (brief `docs/briefs/epic-0166-decals-roads-farms.md`, "MC answers" of
2026-11-15 00:26: route (a), the tap of the EXE's decal records).

Marks: `[C]` confirmed by decompile or dump, `[L]` verified live, `[I]` inferred. Addresses are DX11 VAs at
ImageBase 0x400000. D83: behaviour only, no decompiler text in this file.

## 0. Premises (P1-P16 of the brief) and their answers; the design for the C1 review

Kept by the lead from 4101's and 4109's hand-backs (2026-11-15 01:46, lead-0166). Saves: copies `C5H-0166-TINY_0008`,
`C5H-0166-TINY_SETUP`, `C5H-0166-HUGE_0350`; DLL `i0ship-26234371-ia` / `i2-shell-test-26234371`; VT present.

| # | Premise (short) | Answer | Mark | Where |
|---|---|---|---|---|
| P1 | D33 holds the same decals as stock on one save | **Holds**: every class but Tree_Shadow is in the D33 records; river, floodplain and model-ground decals bit-equal between the arms; random classes equal in count. Record +0x08 bit 0 ("uploaded") stays clear under D33 on late decals (8,592 of 17,788 on huge): the tap ignores it | L (C for the bit) | 2.3, 3.3 |
| P2 | river bed and flood plains are TerrainDecalSystem decals | **Holds**: Riverbank, Floodplains and their end caps (categories 4-9) are TDS decals made by 0x559FF0 through 0x60F180 (61 on Tiny t0008, 326 on huge); the capture shows `riverbank_d` + end caps drawn as decals over a river tile, not a blend layer. Bug 0130's channel is the missing decal | C, L | 2.2, 4.4 |
| P3 | records + type table suffice to redraw a tile's decals | Inputs: record (rectangle, priority, three texture handles, pass bit, triangles), its vertex bytes, DecalViewProj per tile, r4 = (1,1,0,0); no TerrainFrame, no other EXE state. Sufficiency stays I until 4104 reproduces `ref`. **Gap: texture handle -> file name** (7.1) | C, L inputs; I sufficiency | 1.4, 4.2, 6.1 |
| P4 | the decal draw: targets, PS, CacheBake slot | **Changed**: TerrainDecalShader pass 1 (PS `0x067f8f30`, 3 targets) draws **straight into the blend's three 512 RGBA16F targets** (albedo, normal/height, spec), mip 0, not cleared, after the blend and before NormalCalculate, SRC_ALPHA/INV_SRC_ALPHA colour, ONE/ONE alpha; tree shadows the same PS into the macro-shadow target ONE/ONE. Pass 0 never drawn. **CacheBake has no decal slot**; there is no separate decal layer | L | 4.1, 4.2, 4.4 |
| P5 | type table from `decal_filenames.xml`, textures from DecalTextures.fpk | **Too narrow**: the XML is inside `Resource/Common/MiscData.fpk` and its ten categories give the textures actually drawn only for river, floodplain and tree-shadow decals; roads, farms and model grounds bring their own textures from DecalTextures, Upgrade1Textures, MiscTextures, ImprovementTextures, UnitsTextures packs, VT's loose files and the VP mod. The server needs full `c5r::gamefs` resolution and a name per handle | C, L | 1.3, 4.3 |
| P6 | layout random per session | **Holds** for tree shadows, route curves, farm fields, farm tracks and city ground (`rand()` in 0x6016F0 and 0x5865B0, the forest's placement); river and model-ground decals deterministic. Only the joined session's own records give its layout: route (a) | L, C | 3.1 |
| P7 | late-load drops of farm fields on the huge save | **Fails as stated**: 381 of 425 farms have fields in both arms, 42 of the 44 fieldless farms are the same plots; no late arrivals 20-60 s after the load | L | 3.2 |
| P8 | add / remove through 0x4387D0 with the record's rectangle | **Holds**: AddDecal and RemoveDecal invalidate exactly the record's rectangle; 0x678FF0 and 0x6B5A50 are terrain edits. Live: a road and a farm reached the records within 0.4 s and moved the counter signal | C, L | 5.1-5.3 |
| P9 | road bridges are decals | **Fails**: no bridge decal key; a road across a river adds only route curves (188 road edges cross rivers on huge) | L | 2.2, 5.3 |
| P10 | blob / tree-shadow decals come from Forest | **Holds**: Tree_Shadow (category 3, macro-shadow pass) from Forest (0x439E00 -> 0x770EE0); 0 under D33. Intended difference with trees (MC answer 7) | C, L | 2.2, 4.4 |
| P11 | key 0x10's Decal system draws nothing | **Holds**: stock against `Decal = 1` at the load view, no pixel-shader draw lost; every TerrainDecalShader draw is a bake draw | L | 4.7 |
| P12 | tap cost and section size | Sizes: Tiny 0.28-0.35 MB, huge 7.2 MB (D33) plus a name table; cost and address space for 4102 | L sizes | 6.2 |
| P13 | decal arrives after the feed change | 0.4 s after the change in stock (5.3); for 4105 | L (one case) | 5.3 |
| P14 | t8 set equal with decals known | **Holds with the EXE's tile+0x300 read per tile** (4111's tap, the .dtap's area block): every tile's area set and every t8 decision equal on both stock recordings (one huge tile whose EXE copy lacks its t8). *Corrected 2026-11-15 by 0367 (14.6): the bit marks exactly the tiles of the decals added or removed after the TerrainRenderer exists; the river and flood-plain decals predate it. D33 runs every invalidation; its smaller set (Tiny 55-61 against 455-460) is stock's Tree_Shadow decals, absent under D33. Under D33 the bit is stock's rule for the product's decals; 4105's rule marks 275 / 285 extra tiles there. Replaced: "the final decal records do not determine the bit, and under D33 the EXE sets it on far fewer tiles, so the product keeps 4105's rule"*; the bit is used behind a switch (the MC's choice, 14.6) | C, L | 13.5, 14.1, 14.6 |
| P15 | `GameplayPlotStateChange` live | **Holds**: fires on the product path, Tiny 34 at load then +11/+4/+1 per AI turn, 0 LOUD; huge 9,966 at load, 7-236 per turn (3306, 3308). The pinned server.exe (a803776) has no feed; 4109 built one | L | 5.F |
| P16 | VT on every compared install | each comparison names it; 4109's VT hash 70402AD7... | L | |

### 0.1 Reconciling 4101 and 4109 (lead)

- **"Pass"** has two meanings. The record's bit 1 of +0x08 (4101) selects the target: 0 = the blend's three targets
  (4101 called it "the decal layer"; there is no separate layer, 4109), 1 = the macro-shadow target (Tree_Shadow and one
  natural-wonder key). Both are drawn with the shader's technique pass 1 (PS `0x067f8f30`); technique pass 0 was not seen.
  Sections 1.2 and 6.4 read "decal layer" as "the blend's targets".
- **Register 6.x** (4109's "layer value": 0 grounds, 0.1 tree shadow, 0.4 river, 0.5 fields, 0.6 dummy, 0.7 roads) is the
  record's priority +0x20 (4101), and the draw order is ascending priority by an unstable sort (4101 6.1 point 3): ties
  between equal priorities (4,650 fields at 0.5 on huge) are open (7.2); 4104 reproduces the captured order and 4106
  shows whether ties matter.
- **The 8 x 8 dummy texture** (4109 4.3, priority 0.6) is most likely 4101's farm tracks with albedo and heightmap
  handle 0 (Detail_Road; 7.3) bound to the engine's default texture [I]; 4104 settles it on `ref`.
- **Register 5** is the record's rectangle (4101 1.2: vertex positions are normalised to it) [C, L].

### 0.2 The design for the review (route (a); what changed against the brief)

1. **The tap (4102)** copies every live record (bit 31 of +0x04; bit 0 ignored): pass bit, priority, the three texture
   handles, rectangle, its vertex bytes (copied out of the chunk: compaction moves them), a content hash and list order;
   plus **a handle -> file-name table** (new, 7.1: decoded from the engine's resource manager by 4102 as its first item;
   fallback a hand-made table for the test saves). Change signal: the header and batch vector each Present (about 1 KB on
   huge), re-read only the batches that moved, diff by content hash, publish the added and removed rectangles; a 1 s hash
   of the live records as a safety net. Section about 8-10 MB on huge.
2. **The server's source (4103)** as the brief, plus the name table; the per-tile list is "every decal whose rectangle
   overlaps the tile's world square, in ascending priority, ties in list order".
3. **The decal pass (4104) runs inside `bakepost`, on the blend's three targets between the blend and NormalCalculate,
   not as a separate `decalLayer` input** (P4): the hook in `post.h` changes from "a decal layer texture" to "the tile's
   decal list + the decal pass"; tree shadows go into the macro-shadow target only if the record set has pass-bit-1
   decals (none under D33). Textures through `c5r::gamefs` from every pack, VT's loose files and the VP mod.
4. **Re-bakes and t8 (4105)**: a decal generation names the rectangles; the tiles they overlap are re-baked and get
   `areaChanged` (0305). Feed changes stay the second signal (P15 holds).
5. **Bridges** (P9): no decal; what stock shows at a road across a river is the route curves plus the river bed; no class
   to add. **4108 not needed** on 4101's census: the only class the tap cannot see under D33 is Tree_Shadow (MC answer 7:
   intended, with trees).

## 1. The decal store in the EXE: records, vertex chunks, the type table (4101)

Method (2026-11-15 00:35-01:00, task 4101): headless Ghidra 12.1.4, read-only, on a scratch copy of the `civ5`
project (`Dec.java` of 3802; output on `C:/c5-64bit-work/0166/4101/out/`, not committed, D83) for 0x49B5A0, 0x68F1B0,
0x714B80, 0x707F30, 0x40D400, 0x60E350, 0x60F180, 0x742EF0, 0x4387D0 and the producers; `llvm-objdump` for the call
sites; then the live census of `shell/tools/decal_probe.py` (section 2), which reads every field below and found them
as described on every arm. Names in quotes are the ones this note gives; the EXE has none.

### 1.1 TerrainDecalSystem ("TDS"), the object at 0xD165E0 [C, L]

TDS is a static object (TerrainSystem+0); every producer calls AddDecal with ECX = 0xD165E0. Members:

| Offset | What | Live (Tiny t0008, stock, 2026-11-15) |
|---|---|---|
| +0x00 | index buffer handle: one u16 buffer 0, 1, ..., 0xFFFE made by Startup (every decal draw is non-indexed in effect) | 1381 |
| +0x18 | the TerrainDecalShader object (0x45EE20: shader 78 `TerrainDecalShader`, constant buffer 0x70); earlier notes called 0x45EE20 the "decal-name manager" (*corrected 2026-11-15*) | pointer |
| +0x1C + 8k, +0x20 + 8k | category k's variant range [begin, end) in the variant table, k = 0..9 | 0..1, 1..2, ..., 9..10 |
| +0x6C..+0x78 | the world rectangle (min x, min y, max x, max y) every decal rectangle is clamped to, in the height field's sample frame (64 samples per plot) | 0, 0, 3583, 2559 |
| +0x6C..+0x93 | *Corrected 2026-11-15 by 0366 (18.1): the spatial index of the small decals is this object itself (a 2D bin grid whose first 16 bytes are the world rectangle): +0x7C / +0x80 inverse cell size, +0x84 cell heads, +0x88 / +0x8C cols / rows, **+0x90 the pointer to its item pool** (embedded at +0x4DC; 0x18-byte items with batch +0x10 and slot +0x14; 0x798A00 allocates, 0x69CD30 links at the head of the cell). Replaced: "+0x90 pointer to the spatial index (an embedded object at +0x4DC)".* | |
| +0x9C, +0xA8, +0xAC, +0xB0 | the large-decal list: 16-byte nodes {next, prev, batch, decal} in a pooled vector, count, head, tail. A decal goes here when its rectangle is wider or higher than the constant at 0xA039B0; its handle then has bit 31 set | 20 nodes |
| +0xB4, +0xB8, +0xBC | the batch vector (0x34-byte "batches"), count, capacity | 3 batches |
| +0xC0, +0xC4, +0xC8 | the variant table (16-byte entries), count, capacity | 10 entries |
| +0x4CC | byte, cleared by the XML reader | 0 |
| +0x4D0, +0x4D4, +0x4D8 | the draw's per-call list (12-byte entries {batch, decal, priority}), count, capacity; empty between draws | 0 |
| +0x4F0 | texture handle of `blank_texture_spec_decal.dds`, the default spec texture | 1975 |

Earlier note "+0x4F4..+0x50C plot/decal grids [I]" (`world-systems-and-stubs.md`): the decal system's own members end
at +0x4F0; nothing of the decal code reads +0x4F4..+0x50C (not checked further; the spatial index is at +0x4DC).

### 1.2 Batches, decal records, vertex chunks [C, L]

A **batch** (0x34 bytes, in the vector at +0xB4) is one vertex chunk with the decals that live in it:
+0x00 free-slot head, +0x04 live decals, +0x08 the decal pool (0x34-byte records), +0x0C slots used, +0x10 capacity
(reserved to 0x100 on creation), +0x14 decals in its list, +0x18 / +0x1C list head / tail, +0x20 the GPU vertex buffer
handle, +0x24 vertices used, +0x28 the vertex chunk (0x7FFF8 bytes = 65,535 vertices of 8 bytes, allocated by AddDecal
at `TerrainDecalSystem.cpp` line 0xD0), +0x2C "compaction pending", +0x30 "upload pending" (set on every add; the draw
skips a small decal whose batch still has it set). Batches are never freed while a game runs [I: no free path seen].

A **decal record** (0x34 bytes, slot s of its batch's pool; live when bit 31 of +0x04 is set, the pool 0x40D400 sets it):

| Offset | Field |
|---|---|
| +0x00 | next decal in the batch's list (0x0FFFFFFF = none); for a free slot, the next free slot |
| +0x04 | bits 0-30 previous decal in the list; bit 31 live |
| +0x08 | flags: bit 0 "uploaded" (cleared by AddDecal, set by the batch upload 0x480A30; the draw skips a decal without it), bit 1 the pass (AddDecal's last argument: 0 = the decal layer, 1 = the macro-shadow pass); the other bits are not initialised (stack garbage) |
| +0x0C | first vertex in the batch's chunk |
| +0x10 | triangle count (the decal owns 3 x count consecutive vertices) |
| +0x14, +0x18, +0x1C | texture handles: albedo, heightmap, spec (the draw binds them as the three textures of shader 78) |
| +0x20 | priority (float; the draw sorts by it and passes it in the constant buffer) |
| +0x24..+0x30 | the rectangle: min x, min y, max x, max y (floats, sample frame, clamped to TDS+0x6C) |

A **vertex** is 8 bytes: half2 position **normalised to the decal's rectangle** ((x - min x) / width, (y - min y) /
height, D3DXFloat32To16Array; *corrected 2026-11-16 by 4552, `trees.md` 12.1: (x - min x) x (1 / width) in float, then binary16
round to nearest even; a plain division misses 2 of 102,771 tree quads*) and half2 texture coordinate (the producer's u, v). The producer hands AddDecal float
vertices {x, y, u, v} (16 bytes), u16 indices and a triangle count; AddDecal de-indexes them into the chunk (3 vertices
per triangle, no sharing between decals or triangles). The rectangle is the bounding box of **all** vertices the
producer passed, not only the indexed ones: a river piece passes one vertex array and three index ranges (body,
end cap 1, end cap 2), so its three decals share one rectangle and each covers part of it [C, L: Tiny t0008 floodplain
end caps at 0.00-0.68 and 0.27-1.00 of a shared rectangle]. Limit: 3 x triangles <= 0xFFFE per decal.

AddDecal (0x49B5A0) picks the first batch whose chunk still has room (+0x24 + 3 x triangles < 0xFFFF); on its way it
compacts any full batch with "compaction pending" (0x60E350: walks the list in order, moves each decal's vertices down
so the chunk is contiguous in list order, rewrites +0x0C, sets +0x24 and "upload pending"); if none fits it makes a new
batch. It appends the record at the list tail, writes the vertices, and indexes the decal (large list or spatial
index). RemoveDecal (0x68F1B0) takes the handle, invalidates the record's rectangle (section 5), frees the slot and
sets the batch's "compaction pending"; the removed decal's vertices stay in the chunk until compaction. So **a decal's
vertex range moves without any change of its picture** (compaction), and freed slots are reused: a tap must identify
a decal by its content (key, rectangle, vertex bytes), not by (batch, slot, first vertex).

### 1.3 The variant table and `decal_filenames.xml` (P5) [C, L]

Startup 0x714B80 (ECX = TDS) loads `blank_texture_spec_decal.dds` into +0x4F0, makes the shader object (+0x18), reads
`assets/Decals/decal_filenames.xml` (0x707F30) and makes the identity index buffer (+0x00). The reader walks ten
category names, an array of 0x80-byte strings at 0xA26D58 in the EXE (`Detail_Road`, `Main_Road`, `Crops`,
`Tree_Shadow`, `Floodplains`, `Floodplains_EndCap1`, `Floodplains_EndCap2`, `Riverbank`, `Riverbank_EndCap1`,
`Riverbank_EndCap2`); for each category element present in the file it appends one 16-byte **variant** per child
element: {priority (attribute `priority`, float, default the constant at 0x9E402C), albedo texture (attribute `albedo`),
heightmap texture (`heightmap`), spec texture (a four-letter attribute, default the blank spec at +0x4F0)}, and stores
the category's [begin, end) at +0x1C + 8k. Textures load through the engine's texture loader (0x8E1740) and are kept
as handles.

The file is **not loose in the install**: it is `decal_filenames.xml` inside `Resource/Common/MiscData.fpk` (1,758 B,
SHA-256 `2dcd280f...e7f73e`; MiscData.fpk `d19e1a29...9817ce`, c5h-i's install, 2026-11-15), the only pack that has it.
It has one variant per category, so every range is one entry wide. Contents (fields as above):

| k | Category | priority | albedo | heightmap |
|---|---|---|---|---|
| 0 | Detail_Road | 0.6 | none | none |
| 1 | Main_Road | 0.7 | none | none |
| 2 | Crops | 0.5 | none | none |
| 3 | Tree_Shadow | default (live 0.1) | `assets/Decals/Trees/Tree_Shadow_2.dds` | same |
| 4-6 | Floodplains, _EndCap1, _EndCap2 | 0.4 | `assets/Decals/Floodplains/floodplains_D[_Endcap1/2].dds` | `floodplains_H[_Endcap1/2].dds` |
| 7-9 | Riverbank, _EndCap1, _EndCap2 | 0.4 | `assets/Decals/Riverbanks/riverbank_D[_Endcap1/2].dds` | `assets/Decals/grey.dds` |

Live the table held exactly these ten entries in this order, the spec handle of each = the blank spec (1975 on c5h-i)
[L]. So **P5 holds** for the river, floodplain and tree-shadow classes: their textures come from the file and
DecalTextures.fpk. It does **not** cover the other classes: roads, farms and the landmark and wonder grounds take their
textures from their producers (section 2), so the server's table needs those producers' texture names as well. The
`Detail_Road`, `Main_Road` and `Crops` entries carry only a priority.

The wrapper 0x60F180 (ECX = TDS; arguments category, vertices, vertex count, indices, triangle count, pass) picks a
variant of the category, **with `rand()` when the range is wider than one** (never in this file), and calls AddDecal
with that variant as the key.

### 1.4 What the draw reads (0x742EF0; P3, records side) [C]

The decal draw (ECX = TDS; arguments: the context, the tile's rectangle (min, max), the 4 x 4 DecalViewProj, the pass)
selects every decal whose rectangle overlaps the tile rectangle grown by the large-decal constant: small decals from
the spatial index (only when their batch has no upload pending), large ones from the large list; both need flag bit 0
and flag bit 1 = the pass. It sorts the selection by priority (0x648890) and draws each with shader 78: constants =
DecalViewProj (64 bytes), (1, 1, 0, 0), the record's rectangle, the priority (0x70 bytes in all); textures = the
record's albedo, heightmap and spec handles; vertex buffer = the batch's +0x20; first vertex = +0x0C; primitive count =
+0x10; index buffer = TDS+0x00. Nothing else of the EXE's state enters a decal draw on the CPU side: **records + the
batch vertex bytes + the three texture names per key + DecalViewProj** describe it [C]; the GPU states and targets are
4109's (section 4). The draw is skipped when the list would exceed the constant-buffer ring (the 1,000 margin test).

## 2. Decal classes, their producers, stock against D33 (4101)

### 2.1 Method and arms

Every census: `shell/tools/decal_probe.py --pid <EXE> --hash-verts --verts DIR` 20 s after the harness's in-game check,
then `plots.lua` through the Lua channel (getters only), joined by `shell/evidence/0166/4101/classify.py` (the plot
nearest each decal's rectangle centre; plot centre in the sample frame = 64 hx + 32 y + 32, 64 y + 128, from M_h2w) and
compared by `compare.py` (class by class: count, triangles, rectangles and vertex-byte hashes in common). Texture
handles are per session (the same texture has handle 1975 in one process and 1767 in another), so a class is the
variant category, or for a direct key "direct-n", the n-th direct key in handle order (the class sizes show the order
is the same in both sessions).

Instance c5h-i (Goldberg, F:, VT installed as on every pool instance, D94), DLL i2-shell-test-26234371 (deps/
i2-shell-test.lock, SHA-256 EB8D26C0...BE932) in both arms, so the arms differ only by config: **stock** = the
instance's own `[DisableSystems]` (OverlayDebug, ColorKeyEdit off) and no marker; **D33** = `darkred.py config --arm
d33` (OverlayDebug, Decal, Forest, River, Combat, Particle, Projectile, FOW, FluidFOW, YieldIcons, DistanceFog,
ColorKeyEdit, Arrows off; marker `clear=0x1`). Saves (copies): `C5H-0166-TINY_0008` (of `c5h-9028-tiny-t0008`, SHA-256
b2ceb663...29163, turn 9 when loaded, 56 x 36, rivers, 8 city route plots), `C5H-0166-TINY_SETUP` (of
`C5H-3000-TINY-SETUP`, 8ce90063...e8d1, 13 route plots incl. railroads, 4 farms, 2 mines, a plantation),
`C5H-0166-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, c499d3c6...88c3). Census files on `F:/c5-64bit-work/0166/4101/`
(`census_<tag>.json`, `plots_<tag>.json`, `verts_<tag>/batch<i>.bin`, `cls_<tag>.txt`, `cmp_<a>__<b>.txt`; not
committed, D141), 2026-11-15:

| Tag | Save | Arm | Time | Batches | Decals (large) | Triangles | Vertices used |
|---|---|---|---|---|---|---|---|
| t0008_stock_a | TINY_0008 | stock | 00:52 | 3 | 483 (20) | 62,364 | 191,574 |
| t0008_d33 | TINY_0008 | D33 | 00:57 | 1 | 314 (20) | 11,150 | 34,242 |
| t0008_stock_b | TINY_0008 | stock, second load | 01:01 | 3 | 481 (20) | 61,836 | 190,638 |
| setup_stock_a | TINY_SETUP | stock | 01:04 | 3 | 642 (18) | 54,081 | 168,525 |
| setup_d33 | TINY_SETUP | D33 | 01:07 | 1 | 504 (18) | 13,358 | 43,542 |
| huge_stock (+ `_late` at 01:12:34, identical) | HUGE_0350 | stock | 01:12 | 19 | 17,776 (135) | 291,523 | 1,224,525 |
| huge_d33 | HUGE_0350 | D33 | 01:21 | 13 | 17,788 (135) | 264,224 | 792,672 |
| p8_setup_stock, _stock2, _stock3 (5.3) | TINY_SETUP | stock | 01:25-01:37 | 3 | 649 / 660 / 649 (18) | 53,871 (stock3) | 168,525 (stock3) |

Read time of a whole census by ReadProcessMemory from Python: 0.04-0.26 s on Tiny, 1.5-1.9 s on huge (the vertex
chunks dominate; an in-process copy is a different figure, P12).

### 2.2 The classes and who makes them [C producers, L classes]

| Class (key) | Pass | Where (nearest plot, census) | Producer [C] | Random per load [L] |
|---|---|---|---|---|
| **Riverbank**, Riverbank_EndCap1/2 (variants 7-9, priority 0.4, `riverbank_D*.dds` + `grey.dds`) | 0 | river plots (12 + 12 + 11 on t0008, 18 x 3 on SETUP) | 0x559FF0 (called from 0x7857F0) through 0x60F180 with categories 7, 8, 9: one vertex array per river piece, three index ranges | no: equal bit for bit (rectangle and vertex bytes) on two loads and in D33 |
| **Floodplains**, _EndCap1/2 (variants 4-6, 0.4) | 0 | flood-plain river plots | the same function, categories 4, 5, 6 | no |
| **Tree_Shadow** (variant 3, 0.1, `Tree_Shadow_2.dds`) | **1** (macro-shadow pass) | forest and jungle plots (169 on t0008, 141 on SETUP) | 0x770EE0 (one quad per tree instance), called from the Forest per-frame update 0x439E00 and from 0x546110, through 0x60F180 category 3 | yes: no rectangle in common between loads (the forest's tree placement) |
| **Route curves** (direct, priority 0.7 = Main_Road's, own albedo/heightmap/spec) | 0 | road and railroad plots (13 on SETUP for 13 route plots) | RoadLayout: 0x583CE0 -> 0x49E660 / 0x6FAB50 / 0x44B690 -> 0x5EC600 -> AddDecal (key from the Main_Road variant's priority and the road textures); each rebuild removes the old curve first (RemoveDecal callers 0x49E660, 0x44B690) | yes: same count, 476 / 472 triangles, no rectangle in common |
| **Farm fields** (direct, priority 0.5 = Crops', nine albedo/heightmap pairs in a row of handles, spec shared) | 0 | farm plots (55 on SETUP for 4 farms) | farm builder 0x748FC0 -> layout 0x6016F0 (`rand()` picks field styles), queue type-3 handler 0x7AD6C0 -> 0x5865B0 -> AddDecal (two calls; `rand() % 3` turns a field) | yes: per-texture counts differ (5/6, 6/4, 9/15 ...), no rectangle in common |
| **Farm tracks** (variant 0 Detail_Road, 0.6, **albedo and heightmap handle 0**) | 0 | farm and railroad-farm plots (27 / 29) | farm builder 0x748FC0 -> 0x61FD40 -> 0x6E27D0 -> 0x55D0C0 (strips along polylines) -> 0x60F180 category 0; the same 0x6E27D0 also runs from 0x709B50 (CreateTerrain 0x60ABB0) | yes |
| **Farm ground** (direct, priority 0, 4 decals, 72 triangles) | 0 | farm plots | [I] the farm model's ground decal through the landmark visualizer | yes (rectangles differ) |
| **Improvement, resource and natural-wonder grounds** (direct, priority 0 or 1, one key per model texture set) | 0 (one Fountain of Youth decal pass 1) | plantation (16), mine (1), oasis, Solomon's Mines, Mt. Kailash, Sri Pada, Fountain of Youth | the "decal" / "Hex Ground Decal Template" meshes of the models (`landmarks-and-models.md` 3: `decal`, `HB decal`) turned into terrain decals by one shared routine, inlined three times: landmark visualizer 0x700030 (<- 0x85E7F0), cvWonderVis 0x65A150 (<- 0x76EBA0), tree cut 0x69F250 (<- 0x614350 <- 0x598380); pass = (mesh kind byte +0xC == 2) | no: equal bit for bit |
| **City ground** (direct, priority 0, one key, the lowest handles 616-624: loaded at start-up) | 0 | city plots and their ring (244 on t0008 for 8 cities, 320 on SETUP) | not traced [I: the city-field stamps of the landmark system] | yes: 31-41 % of rectangles in common between loads |
| two small direct keys on plain / road plots (2 + 1 decals) | 0 | | not traced | no |

The huge save (stock, 17,776 decals, 166 keys) adds the same classes at scale and more model-ground keys, one per
improvement art: academy, camp, citadel, customs house, fishing boats, fort, lumber mill (114), manufactory, mines
(several keys), and so on; city ground is 6,247 decals; route curves 1,376 decals (75,880 triangles) for 1,017 route
plots; farm tracks 2,463; farm fields 4,650 over 18 texture pairs. Keys at priorities other than 0, 0.1, 0.4, 0.5,
0.6, 0.7 and 1 (0.8: 183 decals; 0.51, 0.59, 0.21 ...) are model-ground decals whose material carries a priority [I].

**Bridges (P9) [L]**: the huge save has 188 route-to-route plot edges across a river (`bridges.py`). The decals
covering those edges' midpoints are only riverbank pieces, the route-curve key (priority 0.7, the same key as every
other route) and farm tracks: **no separate bridge key**. So P9 as stated ("bridges are decals with `bridges_diff`")
does not hold for TDS: a road crossing a river is the ordinary route curve over the riverbank decal; whether stock
draws a bridge there, and as what (a region of the route texture or a model of the landmark system), is for the
capture or a picture (4109) [I: a model, since no decal uses another texture there]. Pillaged and half-built farm
states and pillaged routes: 25 pillaged farms on the huge save carry fields with the same keys as intact ones; no
separate pillaged key was seen [L, keys only]. The `Decal_System` of key 0x10 (`SerialEventTerrainOverlayMod`) is a
different system; it adds nothing to TDS (off in D33, and the D33 census has every class of stock but Tree_Shadow).

### 2.3 P1: stock against D33 on the same save [L]

Class by class (`cmp_t0008_stock_a__t0008_d33.txt`, `cmp_setup_stock_a__setup_d33.txt`): **every class stock has is in
the D33 records, with the same count or the count its random layout gives, except Tree_Shadow** (169 and 141 in stock,
0 in D33). River, floodplain, improvement, resource and natural-wonder decals are equal bit for bit (rectangle and
vertex bytes) between the arms; route curves, farm fields, farm tracks and city ground differ only as two stock loads
differ (2.4). So **P1 holds** with two qualifications:

1. Tree_Shadow is made by the Forest system, off in D33 (P10, 2.5).
2. **The "uploaded" bit (record +0x08 bit 0) differs**: in stock every live decal had it set; in D33 176 of 244
   (t0008) and 237 of 320 (SETUP) **city-ground** decals had it clear, every other decal set, and the batch's "upload
   pending" stayed 1. AddDecal clears the bit; the batch upload 0x480A30 (ECX = TDS; for every batch with "upload
   pending": makes the vertex buffer if +0x20 is 0 (0xFFFF vertices, format 0x12), copies the chunk into it, sets bit 0
   on every decal of the batch's list, clears "upload pending") is the only writer found [C]; it runs with the terrain
   draw [I], which D33 turns off, so decals added after the load's last upload (the city ground, laid out late) keep
   the bit clear. It is **not** a visibility choice of the producer. The tap therefore ignores bit 0 and takes every
   live decal (stock uploads every decal within a frame).

The D33 records sit in one batch where stock needs three: Tree_Shadow's 51,238 triangles are 82 % of t0008's.

## 3. Randomness (P6) and late-load drops (P7) (4101)

### 3.1 P6: two loads of one save [L, C causes]

`cmp_t0008_stock_a__t0008_stock_b.txt` (two stock loads of `C5H-0166-TINY_0008` in two processes, 00:52 and 01:01),
with `cmp_setup_stock_a__setup_d33.txt` as a third pair (stock and D33 loads of `C5H-0166-TINY_SETUP`):

| Class | Equal between loads (rectangle and vertex bytes) | Cause [C where named] |
|---|---|---|
| Riverbank, Floodplains (+ end caps) | all (35 + 26 on t0008; 54 + 3 on SETUP) | none: the river pieces are laid out from the map; 0x60F180's `rand()` only picks among variants, and the file has one per category |
| improvement, resource, natural-wonder grounds | all | none: the model's decal mesh at the plot's place |
| Tree_Shadow | none (169 / 167 decals) | the Forest's tree placement (0x770EE0 puts one shadow quad per tree instance) |
| route curves | none (13 / 13 decals, 476 / 472 triangles) | RoadLayout's curves [I: random jitter in the route builder; not traced to a `rand()` call] |
| farm fields | none; per-texture counts differ | `rand()` in the farm layout 0x6016F0 (field style) and `rand() % 3` in 0x5865B0 (a field turned by a fixed angle) [C] |
| farm tracks (Detail_Road) | none (27 / 29) | follow the farm layout [I] |
| city ground | 31-41 % | not traced |

So **P6 holds** for every class but the river, floodplain and model-ground decals, which are deterministic. Nothing
here needs the job order: every random class has a `rand()` or a random placement upstream. The consequence stands
as the brief put it: only a tap of the joined session's own records can equal that session's picture.

### 3.2 P7: farm-field decals against farms on the huge save [L]

`p7.py` counts, per farm plot of `plots.lua`, the farm-field decals (direct key, priority 0.5) whose rectangle centre
lies within 48 sample units of the plot's centre. Huge `C5H-0166-HUGE_0350`, stock, census at 01:12 (20 s after the
in-game check) and again at 01:12:34 (identical: no decal arrived late):

- 425 farms (26 pillaged), 4,650 farm-field decals, 2,463 farm tracks;
- 381 farms with fields (356 + 25 pillaged), **44 without** (43 + 1 pillaged); fields per farm 1-33, median 14.
- Tiny SETUP stock: 4 farms, 55 field decals, every farm with fields (14, 14, 15, 24).

- Huge D33 (census 01:21, 20 s after in game): 4,737 field decals, again 381 farms with fields and 44 without, and
  **42 of the 44 fieldless farms are the same plots as in stock** (`p7.py` and a set comparison, 2026-11-15).

So **P7 as stated does not hold on this pin** [L]: no sign of late-load drops. Both arms give the same farms without
fields, which a random queue drop would not; the fieldless farms are an outcome of the farm-group layout (a group's
fields can lie over its neighbours) or of the 48-unit radius [I], the same in every load. 2026-10-01's "793 of 1,316
farms loaded without fields" was an old pin before EngineQueueGuard's fixes. Either way the tap copies whatever the EXE
holds.

### 3.3 The huge save, stock against D33 [L]

`cmp_huge_stock__huge_d33.txt`: 17,776 decals (stock, 19 batches) against 17,788 (D33, 13 batches). Riverbank and
Floodplains with end caps: 326 decals, equal bit for bit. Tree_Shadow: 98 in stock (the camera's surroundings only:
the Forest makes shadows near the view), 0 in D33. 126 of 169 classes have equal counts and equal records; the others
are the random classes of 3.1 (farm fields and tracks, route curves, city ground, one 0.8-priority key [I: an improvement
with random placement]) and three small barbarian-camp keys (3/1, 2/5, 4/3 decals). In D33, 8,592 of 17,788 decals
had the "uploaded" bit clear and 7 of 13 batches "upload pending" (2.3 point 2).

## 4. The decal passes in the tile bake, from a capture (4109)

Written 2026-11-15 from 00:55 EST by task 4109. Marks: `[L]` read from RenderDoc 1.47 captures of the stock EXE (below),
`[C]` from the shader blobs' own disassembly (task 2902's `D3DDisassemble` text, `D:/c5-64bit-work/2900/2902/dump/asm/`,
read only; behaviour in words, D83), `[I]` inferred.

### 4.0 Set-up and captures [L]

- Goldberg instance c5h-g (F:, pool claim `0166-4109`), CP/VP/EUI, 32-bit DLL pin `i0ship-26234371-ia` (3C2F47DA...),
  stock `config.ini` (terrain pass on, no D33 key, no viewmask marker), RenderDoc 1.47 preloaded by the harness, frame
  cap 30, 2554 x 1411, RTX 4080 (the workstation's driver as of 2026-11-15). Save `C5H-0166-TINY_SETUP` (copy of
  `C5H-3000-TINY-SETUP`, SHA-256 8ce90063..., Tiny 56 x 36, turn 18; 4 farms, 10 roads, 3 railroads, 21 other
  improvements, 115 river plots read over the Lua channel). Game launched 00:42, in game 00:44.
- Driver `shell/evidence/0166/4109/capture_drive.py` (mode 1): `UI.LookAt` to a farm, road or river plot and one capture
  0.15 s later (a pan brings new tiles into view); 2 s later 4 posted wheel ticks (in, then out at the next plot) and one
  capture 0.3 s later (the size class of the central tiles changes, so they are re-baked). Popups: none up at the start
  (`UI.IsPopupUp()` false, D114). 7 captures 00:45:09-00:45:40, 714 MB, in `F:/c5-64bit-work/0166/4109/captures/`
  (`c5h-g_20261115-004219_frame{894,1035,1141,1286,1388,1534,1641}.rdc`), never committed (D83). All 4109 data on F:
  (2026-11-15 01:42: 4.6 GB): `captures/` 23 captures 2.2 GB, `xml/` the conversions 1.6 GB, `exports/` the bake exports,
  `ref/` the reference set, `scan/`, `logs/` (driver, census and P15 logs with the P15 run's `out/`), `pics/`.
- Scan (`scan_decals.sh`: `cli.py rdc-export --select materials:78` per capture) and census (`census.py`: the material
  of every draw, by its pixel shader blob): **2 of the 7 frames hold a tile bake with decal draws** [L]:

  | Capture | When | Draws in the frame | The bake (frame-order draw indexes) | Decal draws | Tile (sample origin, from DecalViewProj) |
  |---|---|---|---|---|---|
  | frame1286 | zoom out 4 ticks at road (13, 7) | 892 | 132 blend, 133-180 decals, 181 normal, 182-184 macro shadow, 185-186 CacheBake, 187-198 compress | 48 | (512, 1600) |
  | frame1534 | zoom in 4 ticks at river (15, 4) | 643 | 114 blend, 115-147 decals, 148 normal, 149-151 macro shadow, 152-153 CacheBake, 154-165 compress | 33 | (512, 1664) |

  Both baked tiles lie by the farms (8, 22)-(8, 25), not under the camera's new centre: one bake per captured frame,
  and the tiles a pan or zoom dirties are baked over the following frames in the EXE's own order [L for what was
  captured; I for the order]. The other five frames hold either a bake without decals or none.

### 4.1 Where the decals go: into the blend's three targets, not a separate layer [L]

The pass list of one bake that draws decals (frame1286; frame1534 is the same with its own counts), to be read with
`terrain-bake-passes.md` 2:

| # | Draws | Program | Targets | States |
|---|---|---|---|---|
| 1 | 1 x 26,136 indices | TerrainShaderScreen (76) pass 1, the blend | 3 x 512 R16G16B16A16_FLOAT, 10 mips (res 43265 albedo, 43277 normal/height, 43289 spec) | ONE/ONE add |
| 1a | **48 (33)** | **TerrainDecalShader (78) pass 1: VS `0x067e6360`, PS `0x067f8f30` (three targets)** | **the same three 512 R16G16B16A16_FLOAT targets, mip 0, not cleared**, viewport 512 x 512 | **SRC_ALPHA / INV_SRC_ALPHA** for colour, ONE / ONE add for alpha, same on all three targets (IndependentBlend off), write mask all; no depth target; rasterizer solid, CULL_NONE, FrontCounterClockwise, DepthClip on, no scissor |
| 2 | 1 quad | NormalCalculate (74) | 512 RGBA8 (43314) | reads 43277, i.e. the height **after** the decals |
| 3 | 3 x 26,136 | TerrainShaderScreen pass 3, macro shadow | 512 RGBA8 (43301) | reads only the height page (A8 1024); **no decal draw follows it** |
| 4 | **2** | CacheBake (75) pass 1 | first draw: 2 x 256 RGBA8 (16298, 16286), second draw: 2 x 256 RGBA8 (43495, 43504) | t1 = 43265 albedo, t3 = 43289 spec (both after the decals) |
| 5-8 | 12 quads, 2 copies | CompressBC, CopyResource | 2 x R32G32_UINT 64 x 64, 6 mips -> t0 35742, t8 35745 (BC1 256) | as stage 1 |

- **No draw of TerrainDecalShader pass 0** (PS `0x067f86a0`, two targets) and **no decal draw into the macro-shadow
  target** in either bake [L]. *This corrects `terrain-bake-passes.md` 2 and 4, which read the code as "pass 0 into a
  decal layer, pass 1 into the macro shadow" [C, not seen live]: in the live bake the decals are drawn with pass 1's
  three-target pixel shader straight into the blend's own albedo, normal/height and spec targets, between the blend and
  NormalCalculate. What TR8+0x6C4 ("the blend's fourth target set") holds is therefore a second binding of the same three
  textures [I]. When pass 0 and the "Decal Macro Shadow" call draw anything was not seen.*
- **CacheBake has no decal slot** [L]: its inputs are those of `terrain-bake-passes.md` 3.3 (t0 normal, t1 albedo, t2
  macro shadow, t3 spec, t4/t5 cubes, t8 LUT, ...); the decals reach it through t1 and t3 (and through t0, since
  NormalCalculate reads the height channel after the decals). So the server's hook `PostInputs::decalLayer` is not a
  separate texture: the decal pass must run between the server's blend and its NormalCalculate, on the blend targets.
- CacheBake drew **twice** in every decal bake captured: once into a pair of RGBA8 textures of the tile (16298/16286,
  16418/16406, 16370/16358 at 256 x 256, 9 mips; 17461/17448 at 512 x 512 in frame654) and once into the array-mode
  scratch pair (43495, 43504) that the compress pass reads [L]. The first pair is the near-camera RGBA8 cache of state 5
  (`terrain-bake-passes.md` 3.5, issue 0306): it is what the frame's terrain draw samples for these tiles; the tiles'
  BC1 t0/t8 (e.g. 35784/35787) are referenced by no later captured frame, while their RGBA8 pairs are [L, 4.5].

**Run 2** (the same instance, pins and save; game launched 00:54, `capture_drive.py --targets "18,14;8,24" --frames 3`:
pan, 1.5 s, 4 wheel ticks, one multi-frame request of 3 frames, i.e. frames F, F+2, F+4; 6 captures 00:56:57-00:57:09,
`c5h-g_20261115-005422_frame{650,652,654,844,846,848}.rdc`, 613 MB; largest free block of the EXE's address space down
to 194 MB at the sixth). Bakes with decals in 654 (tile (576, 1472), size class 512: 14 decal draws), 844 (tile (576,
1664): 34), 846 (tile (704, 1664): 6 + 1 into the macro shadow, below); none in 650, 652 and 848. Over both runs: **5
decal bakes in 13 captured frames**, every one with the same pass order and states. Runs 3 and 4 (01:27-01:38, 4.5) added
8 frames and 2 more decal bakes (frame335: 32 decal draws; frame337: 11 + 1), the same again: **7 decal bakes in 21
stock frames** (plus the 2 frames of 4.7, without decals). In run 3 a multi-frame request captured frames 318 and 321,
three apart, not two as in `renderdoc-capture.md` 10.3 [L].

### 4.2 The inputs of one decal draw (P3, draw half) [L, C for the shader behaviour]

Every input of the 136 decal draws of the five bakes, from the exports (`render/evidence/0166/4109/ref_header.txt` names
the files; tables by `shell/evidence/0166/4109/bake_table.py --cb`):

- **Geometry.** Topology triangle list; `DrawIndexedInstanced`, 1 instance, BaseVertexLocation 0; index buffer one
  immutable R16_UINT buffer of 65,535 indices that is **the identity** (index i = i), so a draw is a run of whole
  triangles from one vertex chunk, given by (first vertex = StartIndexLocation, count = IndexCountPerInstance, a
  multiple of 3: 3 to 6,624 seen). Vertex buffer: one of the **vertex chunks**, D3D11 DEFAULT buffers of **0x7FFF8 =
  524,280 bytes = 65,535 vertices x 8 bytes**, created with their contents (`CreateBuffer` initial data; no `Map` or
  `UpdateSubresource` of them in any captured frame); three chunks in use on this save. Input layout: **POSITION
  R16G16_FLOAT at 0, TEXCOORD0 R16G16_FLOAT at 4, stride 8** (half floats, not the f32 of earlier notes). POSITION is
  the vertex's place inside the draw's rectangle, 0..1 in each axis; TEXCOORD0 is unbounded (farm fields repeat their
  texture; the sampler wraps). A farm-field polygon is a triangle fan stored as a list (every triangle repeats the first
  vertex) [L, frame1286 draw 157].
- **Constant buffer** (VS and PS slot 0, the EXE's shared 256-byte dynamic buffer; the VS reads 7 registers = 0x70
  bytes, the PS none), written before each draw:
  | Register | Content [L values] | Use by the VS [C] |
  |---|---|---|
  | 0-3 | **DecalViewProj**: (1/33, 0, 0, 0), (0, -1/33, 0, 0), (0, 0, -0.5, 0), (tx, ty, 0.5, 1); per tile only tx, ty change (tile (512, 1600): -16.4868, 49.4565; (576, 1664): -18.4262, 51.3959; (704, 1664): -22.3050, 51.3959; (576, 1472): -18.4262, 45.5777), i.e. tx falls by 1.9394 = 64/33 per 64-sample tile column and ty by the same per row: the tile's 66-sample square (origin - 1 .. origin + 65, with the bake grid's half-texel shift) onto clip -1..1, y flipped [L values]. *Closed form settled 2026-11-15 by 4104 (11.2; replaces "I the closed form, to be fitted by 4104"): with s = float(1/33), tx = -(originX + 32) s - 1/512, ty = (originY + 32) s + 1/512, bit-equal on all six captured tiles [L]* | clip = DecalViewProj x (wx, wy, z, 1) |
  | 4 | (1, 1, 0, 0) in every draw | uv = TEXCOORD0 x r4.xy + r4.zw |
  | 5 | **the draw's world rectangle** (x0, y0, x1, y1) in height-field samples, e.g. (549.82, 1620.28, 573.18, 1637.29) | (wx, wy) = r5.xy + POSITION x (r5.zw - r5.xy) + (1, 1) |
  | 6 | x = **a layer value per decal class**: 0 improvement grounds and city base, 0.1 tree shadow, 0.4 river bed, 0.5 farm fields, 0.6 the 8 x 8 dummy type, 0.7 roads/railroads (every draw of the four exported bakes); y, z (0 / -0.5 / -0.392573, 0.333333) are not read by either shader | wz = r6.x; no depth target is bound, so it only has to stay inside the clip range. **The draws come in ascending order of this value** in every bake (grounds, then fields, then the dummy, then roads; river then shadow) [L order; I that the EXE sorts by it], and with alpha blending the order decides the result |
  The four floats of register 5 and the float of register 6.x match the **0x34-byte decal batch record's float rectangle
  at +0x24..+0x30 and its +0x20** (`world-systems-and-stubs.md`, RemoveDecal 0x68F1B0) [I: the brief's "type entry
  +0x24..+0x30" of `asset-pipeline.md` 3 is this record, per draw; 4101 confirms from the records]. So one draw = one
  batch record.
- **Shaders.** VS `0x067e6360` (SHA-256 38faf345..., 936 B), PS `0x067f8f30` (1b4a84c0..., 848 B), the EXE's own blobs
  [L]. The PS [C]: target 0 = t0 sampled at uv (rgba); target 1 from t1 sampled at uv: with h = t1.r and s = 2 t1.g -
  1, rgb = h and alpha = s when s >= -0.0001, else rgb = 1 - h and alpha = -s (a signed height delta with its weight);
  target 2 = t2 sampled at uv. With the blend state above each decal lays its colour over the albedo by t0.a, moves the
  height toward h or 1 - h by |2 t1.g - 1| (the normal map then sees the road or the river bed as relief), and lays t2
  over the spec target by t2.a. Pass 0's PS `0x067f86a0` is the same without target 2.
- **Textures and samplers.** t0, t1, t2 = the decal type's diffuse, height and spec maps (all mips, the view formats
  below), each with sampler MIN_MAG_MIP_LINEAR, WRAP in u, v, w, MinLOD 0, MaxLOD FLT_MAX, bias 0, aniso 0 (s0-s2 the
  same state object). No other resource is bound (no TerrainFrame: the decal draws read no shared buffer).
- **Viewport** 512 x 512 at (0, 0), depth 0..1, whatever the size class (the blend targets are always 512).

### 4.3 The decal textures: which files [L]

Matched byte for byte (every subresource) against the DDS payloads of the c5h-g install and its MODS folder
(`ident_textures.py`, `ident_loose.py`; texture names only, no data committed):

| Class (from the textures) | t0 diffuse | t1 height | t2 spec | Seen in |
|---|---|---|---|---|
| road / railroad | `roadsandrails_new_d.dds` (Upgrade1Textures.fpk, BC2 512 x 256, view BC2_UNORM_SRGB) | `roadsandrails_h.dds` (DecalTextures.fpk, BC3) | `roadsandrails_new_sref.dds` (Upgrade1Textures.fpk, BC1) | all 5 bakes |
| farm fields | `crops_africa_01/02/03/05/06/07/08_d.dds` (DecalTextures.fpk, BC1, SRGB view); `crops_maize_d.dds` (**VP mod**, `MODS/(2) Vox Populi/Assets/Models/Resources/Maize/`, BC3) | `crops_generic_01/02/05_h.dds` (DecalTextures.fpk) | `blank_texture.dds` (DecalTextures.fpk, BC3 4 x 4) | 1286, 1534, 844 |
| city base | `citybase_diff_anc.dds` (**VT**, loose `Assets/DLC/VT/Cities/`) | `citybase_ht.dds` (MiscTextures.fpk) | `citybase_sref.dds` (MiscTextures.fpk) | 1286 |
| improvement grounds | `ancient_farm_decal_d/_h` (ImprovementTextures.fpk); `mine_generic_decal_d` (MiscTextures.fpk) + `_h`/`_s` (ImprovementTextures.fpk); `crop_ground_d/_h`, `sub_mid_farm_grnd_d/_h`, `plantation_mid_ground_d/_h`, `aluminum_decal_diff` / `aluminum_ore_decal_h` (MiscTextures.fpk) | (pairs as named) | `invisible.dds` (UnitsTextures.fpk, BC1 4 x 4) or the type's own | 1286, 1534, 654, 844 |
| **river bed** | `riverbank_d.dds`, `riverbank_d_endcap1.dds`, `riverbank_d_endcap2.dds` (DecalTextures.fpk, BC3 2048 x 256) | `grey.dds` (DecalTextures.fpk, BC3 256) | `invisible.dds` | 846 |
| tree shadow (blob) | `tree_shadow_2.dds` (DecalTextures.fpk, R8G8B8A8 64 x 64, 7 mips) as t0 and t1 | | `invisible.dds` | 846, macro-shadow pass |
| (none) | the EXE's own 8 x 8 R8G8B8A8 texture (res 910; texel (0, 0) = (255, 0, 0, 255), all others 0) as t0 and t1 | | `invisible.dds` | 7 to 18 draws per bake; *settled 2026-11-15 by 4104 (11.3; replaces "[I: a type whose textures are missing or not yet loaded]"): the EXE's default texture for handle 0, made in code by the server from this description and bit-equal in the bake [L]; the draws (priority 0.6) are the farm tracks, Detail_Road with handle 0 [I: by the priority]* |

*Note 2026-11-15 (4104, 11.3): the spec texture named `invisible.dds` above has the same payload as `blank_texture_spec_decal.dds` (Upgrade1Textures.fpk, TDS+0x4F0's default spec of 1.1) and Upgrade1Textures' `trading_post_grnd_sref_early.dds`; the EXE's name for it is `blank_texture_spec_decal.dds` [I]. `tree_shadow_2.dds` is an A8R8G8B8 file, created by the EXE as R8G8B8A8 with its bytes unchanged [L].* *Corrected
2026-11-15 by 4111 (14.3): the file is grey, so "bytes unchanged" and "channels in their meaning" give the same bytes; the
stone quarry's A8R8G8B8 height map shows the EXE samples such files with their channels in their meaning (B8G8R8A8).*

So the decal types take their textures from **several packs, VT's loose files and the VP mod**, not only from
DecalTextures.fpk (P5's "textures from DecalTextures.fpk" is too narrow): the server needs the game's full file
resolution (packs, DLC, mods; `c5r::gamefs`) for decals, and the type table must carry the three names per type.

### 4.4 The river bed and the tree shadows (P2 capture half, P10 note) [L]

- **The river bed is a TerrainDecalSystem decal** [L, frame846, tile (704, 1664) beside the river plots (10, 22)-(10,
  25), (11, 24)]: 6 decal draws with `riverbank_d` (2 draws of 6,588 and 6,624 vertices: the river's long strips) and
  `riverbank_d_endcap1`/`_endcap2` (4 draws of 72-108 vertices: the ends), height `grey.dds`, spec `invisible.dds`,
  register 6.x the river class's depth. The tile's lit colour cache (`ref` B, mip 0, Visual Checks
  `vc-20261115-010247-workstation-6efd`, DIAGNOSTIC) shows the river bed with its banks. So the dark blue channel of bug
  0130 is the missing decal; `river_arrows.md` 4's "the riverbanks are terrain pieces" does not describe the bake
  [L here; which producer adds them (CreateTerrain's tail) is 4101's census]. Flood plains were not on this tile (no
  `floodplains_*` draw seen).
- **Tree shadows are decals drawn into the macro-shadow target** [L, frame846 draw 134]: after the three macro-shadow
  draws, one TerrainDecalShader pass-1 draw (PS `0x067f8f30`) with **only the macro-shadow target bound** (512 RGBA8,
  43301), blend **ONE/ONE add**, t0 = t1 = `tree_shadow_2.dds`, t2 = `invisible.dds`, 666 vertices (222 triangles: one
  quad per tree, 111 trees [I]), register 6 = (0.1, -0.392573, 0.333333, 0). This is the bake's "Decal Macro Shadow"
  pass of `terrain-bake-passes.md` 2. The colour cache shows the blobs as dark patches (B picture). P10: these are made
  by whatever adds tree shadows (Forest is on in stock); under D33 (Forest off) they are absent from both the EXE's and
  the server's picture [I; 4101's census].

### 4.5 The reference set for 4104 [L]

`F:/c5-64bit-work/0166/4109/ref/` (398 MB, 1,112 files, four tiles; never committed), manifest with every file's SHA-256 and the
header (save, arm, pins, GPU and driver, VT hash, captures): `render/evidence/0166/4109/ref_manifest.txt`; built by
`shell/evidence/0166/4109/build_ref.sh`.

- **A_roadfarm**: tile (576, 1664), baked in frame844 (34 decal draws: roads/railroad, eight farm-field types incl. VP's
  maize, ancient farm, a mine, the 8 x 8 dummy). `inputs/` = the export of draws 120-190 (every input of the bake:
  blend, decal, normal, macro-shadow, CacheBake and compress draws with their buffers, constants, textures, states and
  DXBC). `expected_f846/` and `expected_f848/` = **the EXE's own output**: the tile's near-camera CacheBake pair (colour
  16418, spec 16406; R8G8B8A8 256 x 256, 9 mips, the EXE's `GenerateMips`) from the frame-start contents of frames 846 and
  848, byte-identical in both (the tile was not re-baked between).
- **B_river**: tile (704, 1664), baked in frame846 (6 river-bed decal draws, 1 tree-shadow draw into the macro shadow).
  `inputs/` = draws 115-160 of frame846; `expected_f848/` = the pair 16370/16358 from frame848.
- **C_run4_f335** (tile (512, 1664): roads, farm fields, ancient farm; 32 decal draws) and **D_run4_f337** (tile (640,
  1664): river bed with both end caps, a farm field, a mine, a tree-shadow draw into the macro shadow; 11 + 1 decal
  draws), from run 4 (`build_ref2.sh`; game launched 01:33, `capture_drive.py --targets "9,23;8,24" --frames 3 --settle
  1 --ticks 4`; captures `c5h-g_20261115-013343_frame{157,159,161,335,337,339}.rdc`, the first series held no bake);
  expected from frames 337/339 (C, byte-identical in both) and 339 (D). D is the second river tile and the only tile with
  a river bed and a farm field together.
- **The intermediates of a decal bake cannot be had this way** [L, negative, 2026-11-15 01:27-01:38]: `pair_check.py`
  compares frame F+2's frame-start CacheBake scratch (43495) with the near cache F's bake drew; it never matched (pairs
  844/846, 846/848, 335/337, 337/339: 0 of 65,536 texels equal), i.e. the uncaptured frame between two captured ones
  always baked another tile. A run with `--settle 6 --ticks 1` (run 3, frames 318 and 321, 3 frames apart) caught no bake
  at all. So the reference for the decal pass itself is end to end (CacheBake output), not the blend targets after it.
- Method (`mkref_4109.py` = `render/tests/bakepost/mkref.py` for any resource id): RenderDoc keeps the frame-start
  contents of a resource the frame references; a captured multi-frame request takes frames F, F+2, F+4, and the frame
  draw of F+2 samples the near caches baked in F, so F+2's frame-start contents are the EXE's result of F's bake. Not
  in the set, and why: the BC1 t0/t8 of these tiles (no later frame references them: near tiles are drawn from the RGBA8
  pair) and the targets between the passes (blend after decals, normal, macro shadow: rewritten by the next bake before
  F+2; see the negative result below). For 4104 the comparison is therefore: the server's bake from `inputs/` against `expected_*` (CacheBake colour and
  spec, all 9 mips), and the BC1 blocks by stage 1's compress of the same CacheBake output (exact by
  `terrain-bake-passes.md` 7).

### 4.6 Answers (for the lead's section 0)

| # | Answer | Mark |
|---|---|---|
| P2 (capture half) | The river bed is a decal draw: `riverbank_d` and its two end caps, TerrainDecalShader pass 1 into the blend targets (frame846, 4.4); not a blend layer | L |
| P3 (draw half) | Every input of a decal draw is named in 4.2: the vertex chunk and its vertex run, the identity index buffer, DecalViewProj (per tile), r4 = (1,1,0,0), the record's rectangle and depth, the three textures of the type, one sampler state, the fixed blend / raster states; no TerrainFrame, no other EXE state. Sufficiency (records + type table redraw a tile) holds for the draw if 4101's records carry chunk, vertex run, rectangle, depth and type, and the type table the three texture names | L for the inputs, I for sufficiency until 4104 reproduces `ref` |
| P4 | Decal pass = TerrainDecalShader pass 1 (PS `0x067f8f30`, 3 targets) into the blend's three 512 R16G16B16A16_FLOAT targets (albedo, normal/height, spec), mip 0, not cleared, SRC_ALPHA/INV_SRC_ALPHA (alpha ONE/ONE), after the blend and before NormalCalculate; "Decal Macro Shadow" = the same PS into the macro-shadow target alone, ONE/ONE, for the tree-shadow blobs; pass 0 (2 targets) never drawn; **CacheBake has no decal slot** (the decals reach it through t0, t1, t3) | L |
| P11 | The Decal system of key 0x10 draws nothing in the stock frame at the load view: with `Decal = 1` no pixel shader of the stock frame is missing (4.7); every TerrainDecalShader draw of 15 stock frames is a bake draw of TerrainDecalSystem | L (one view and 15 frames) |

### 4.7 P11: the Decal system of key 0x10 [L]

`shell/evidence/0166/4109/p11_ab.sh`, 2026-11-15 01:05-01:11 EST, c5h-g as in 4.0: two fresh launches of
`C5H-0166-TINY_SETUP` at the load view, no camera move, one frame captured 20 s after in game; arm S = the stock
`config.ini`, arm D = the same with `Decal = 1` in `[DisableSystems]` (the file backed up by SHA-256 before, set back to
stock after; both arms read back from the file). Captures `c5h-g_20261115-010539_frame718.rdc` (S, 653 draws, 43 pixel
shaders) and `c5h-g_20261115-010903_frame731.rdc` (D, 656 draws, 44 pixel shaders). Draws per pixel shader (blob SHA-256,
`census.py`): **no pixel shader of S is absent from D**; the only differences are one draw fewer of two unnamed shaders
(73daa800..., c704c80b...: 16 against 15) and 5 draws of a shader that only D has (70ed01e8...), none of them a decal
material [I: animation and timing noise between two launches]. Each frame held one bake (blend, normal, macro shadow,
CacheBake) of a tile without decals. Together with the 13 frames of 4.0-4.1 (every TerrainDecalShader draw is a bake
draw into the blend or macro-shadow targets) this answers P11: the Decal system draws nothing in a stock frame of this
save; `disable-systems-and-masks.md` row 4's one-view result ("nothing visible changed") holds in the draw list. Not
covered: a game where `SerialEventTerrainOverlayMod` is raised (a mod's terrain overlay), which this save has not.

### 4.8 Open after 4109 (capture side)

- ~~The closed form of DecalViewProj's translation per tile~~ settled 2026-11-15 by 4104 (11.2). (4.2 gives four tiles' values; 4104 fits the bits, as stage 1
  did for the bake grid's half-texel positions).
- When TerrainDecalShader pass 0 (two targets) is drawn: never in 5 decal bakes (all with a spec target bound) [I: a
  bake without the spec target, tile+0x2FC = 0, if the blend then binds two targets].
- Flood plains (`floodplains_*`) and bridges (`bridges_*`, P9) were on no captured tile; the river-bed draw was seen on
  one tile only. The class of the 8 x 8 dummy draws (layer 0.6) is not known (4101's records name their type).
- The intermediate targets after the decal pass of the same bake (blend targets, normal, macro shadow) are not in the
  reference set (4.5); if 4104 needs them, capture F and F+2 with no bake in F+1 (a pan that dirties one tile only), or
  compare its CacheBake output only.
- The capture order of bakes: one bake per captured frame here; how many the EXE does per frame when many tiles are
  dirty was not counted.
- The corrections owed in other files (not 4109's to edit; for the lead's H list): `terrain-bake-passes.md` 2 and 4
  (decals into the blend targets with pass 1, the macro-shadow decal pass for tree shadows, no CacheBake slot; layout
  R16G16_FLOAT), `asset-pipeline.md` 3 (TerrainDecalShader's targets; "type entry +0x24..+0x30" is the per-draw record
  rectangle), `terrain-rendering-inventory.md` 2.2 (river banks are decals) and 2.3 (decal textures come from several
  packs, VT and the VP mod), `render/include/c5r/bake/post.h` (`decalLayer` is not a separate input: the decal pass runs
  on the blend targets before NormalCalculate), `renderer-in-server-design.md` 2.2 (the live rate was measured by 3306
  and 3308, 5.F).

## 5. The change signal: the tile invalidation (P8, 4101) and the feed (P15, 4109)

### 5.1 Who changes the records, and what each change invalidates (P8) [C]

The writers of TDS's records found (2026-11-15, decompile of each and a search of the listing for the record and
batch fields): AddDecal 0x49B5A0, RemoveDecal 0x68F1B0 (frees through 0x45D120), the compaction 0x60E350 and the
batch upload 0x480A30. Only the first two change what a decal draws:

- **AddDecal** calls 0x4387D0 (at 0x49B925) with the new decal's rectangle **after** the clamp to TDS+0x6C, the third
  argument 0 and the fourth = 1 when the decal has a spec texture of its own (not 0, not the blank spec at TDS+0x4F0),
  which also stamps TR+0x2348 with the frame counter. 0x4387D0 is called only while the TerrainRenderer pointer
  0xD8C584 is set.
- **RemoveDecal** calls 0x4387D0 (at 0x68F233) with the removed record's own rectangle (+0x24, +0x2C) and 0, 0.
- The **compaction** moves vertices and rewrites +0x0C without changing any picture and without invalidating; the
  **upload** sets bit 0 and touches no tile.

0x4387D0 itself (`terrain-bake-residency.md` 3) turns the rectangle into tile ranges and, for each tile, sets +0x1CC to
the frame counter, the layer count +0x1F8 to 0, **+0x300 ("area changed") to 1**, and queues the tile's job again.
Its two other callers are not decal paths: **0x678FF0** (the earlier note's "0x67915E in 0x678FD0": 0x678FD0 is a
two-line thunk that hands 0x678FF0 to a dispatcher; *corrected 2026-11-15*), called from 0x61ECE0, and **0x6B5A50**
(from 0x6A74D0) both walk a per-plot byte mask, edit the terrain of each marked plot through 0x775130 (0x6B5A50 only
where the height-field value lies between 13,000 and 18,000) and invalidate the mask's bounding rectangle with the
third argument 1: terrain edits (map scripts, plot type changes), which `BakeRelevantChange`'s terrain field already
covers. *Corrected 2026-11-18 by epic 0465 (4900, `live-terrain.md` 4.1) [C, static]: 0x678FF0 is the Nexus terrain editor's 32 x 32 material brush, not a map-script or plot-type edit; 0x6B5A50 is the marsh stamp (from `GameplayFeatureChanged`); 0x678FD0 is an unrelated Lua registration. Replaced: "terrain edits (map scripts, plot type changes)".*

So **P8 holds** [C]: every decal add and remove passes 0x4387D0 with exactly the rectangle stored in the decal's
record, and those two are the only decal paths into it. A tap that sees the records before and after a change can
name the tiles to re-bake and the tiles that get tile+0x300 = 1 (the t8 set of 0305): the tiles overlapped by the
union of the rectangles of the decals added and removed, clamped as 0x4387D0 clamps. Tiles changed by terrain edits
come through the feed as today. The live road of 3408 6.5 (16 tiles in stock, 8 under D33): the stock rectangle
union is wider because the road's re-layout also re-lays decals that D33 lacks [I: tree shadows near the road; not
traced].

### 5.2 A change signal a tap can read cheaply [C, L]

Every add appends at a batch's list tail and raises that batch's +0x14 (listed), +0x04 (live) and +0x24 (vertices
used); every remove lowers +0x04 and +0x14, rewrites the free head +0x00 and sets +0x2C; a compaction lowers +0x24 and
sets +0x30; a new batch raises TDS+0xB8; a large decal moves TDS+0xA8..+0xB0. `decal_probe.py --watch` samples exactly
these (one 0xC0-byte read of the header plus one read of the batch vector, 3 x 0x34 bytes on Tiny): "batches, large,
large head and tail, live, listed, verts, slots, the hash of every batch's head and tail, pending". A remove and an
add of the same size into the same freed slot at the tail between two samples would leave these equal; so the tap
should also hash the live records themselves (0x34 bytes x decals: 25 KB on Tiny t0008, section 6) at a lower rate,
or every time the counters move. Measured live: section 5.3.

### 5.3 Live: a road across a river and a farm (P8, P9) [L]

`shell/evidence/0166/4101/p8.py` (driver; `p8_edit.lua` picks plots and sets them through the Lua channel; files in
`F:/c5-64bit-work/0166/4101/p8_setup_stock3/`), stock arm, `C5H-0166-TINY_SETUP` (a copy; diagnostic setters only),
2026-11-15 01:35-01:37, the signal of 5.2 sampled every 0.1 s:

- **Idle 30 s** (camera still, no edit): no change of the signal, censuses before and after equal. The same on the
  huge save in stock (`watch_huge_stock_idle.jsonl`, 40 s at 0.25 s, 01:12-01:13: one signal value) and between its
  two censuses 20 s and 60 s after the in-game check (identical). Camera moves were not tried (the Forest's
  per-frame update 0x439E00 adds and removes tree shadows near the view [C], so in stock a pan may churn
  Tree_Shadow; under D33 there is no Forest).
- **Road on (10,22) and (11,22)**, two plots visible to the player with a river on the edge between them, at
  01:36:01.6: the signal changed **0.41 s** after the setter returned; 5 route-curve decals added, 2 removed (the
  neighbour's curve re-laid), all of the one route key (priority 0.7); union rectangle 175 x 104 sample units (about
  3 x 2 plots). **No bridge decal** (P9, as on the huge save).
- **Farm on (7,20)** at 01:36:17.0: the signal changed **0.40 s** later; added 18 farm fields (priority 0.5, 8 of the
  9 texture pairs), 9 farm tracks (Detail_Road), 1 farm-ground decal, and **2 Tree_Shadow decals re-laid** (removed
  and added with a few triangles fewer: the Forest re-lays the shadows next to a new landmark); "vertices used" fell
  (a compaction ran). Union rectangle 161 x 159 units.
- A first try on plots chosen without a visibility test ((14,5), (15,5) and a farm on (11,2); 01:25-01:26,
  `p8_setup_stock`; their visibility and revealed state were not read) changed **nothing** in TDS within 15-20 s
  [L]: the EXE lays out the art of a plot change only when the plot is visible or revealed to the active player [I],
  so a re-bake keyed on the feed alone can bake a change the EXE does not draw (yet).

So in stock a road or farm change reaches TDS within about 0.4 s and the counter signal sees it; the Tree_Shadow
re-layout is the likely reason stock invalidated 16 tiles for 3408's road where D33 (no Forest) invalidated 8 [I].

### 5.F The feed's `GameplayPlotStateChange` (P15, 4109)

**It fires live; it was already measured twice on the joined route** [L, earlier runs; found by 4109 on 2026-11-15]:
`renderer-in-server-design.md` 2.2's "live rate not measured" was written before 3306 and 3308 ran. Their
`feed-per-turn.tsv` (cumulative counts at the end of each turn; columns as `server/src/remote/srv_remote_feed.cpp`):

| Run | Route | Save | Load turn | Per AI turn after it | A reload in the same EXE |
|---|---|---|---|---|---|
| 3306 r2, 2026-11-13 (`shell/evidence/3306/feed-per-turn_r2.tsv`) | proxy route with the renderer in server.exe (`render=1`), D33 | huge `VP8P-HUGE_0350` copy, turns 350-379 | 9,966 | 7-236, median 40 (30 turns) | +9,809 |
| 3308 r1, 2026-11-13 12:09-12:28 (`shell/evidence/3308/feed-per-turn_r1.tsv`) | **product path** (ServerRenderer = 1, composite, D33 minus Overlay) | the same save, turns 350-359 | 9,966 | 218, 26, 8, 37, 179, 20, 40, 13, 13 | +9,836 |

**4109's Tiny run on the product path** [L, 2026-11-15 01:19-01:24 EST]: `shell/evidence/0166/4109/p15_run.py` =
`shell/evidence/3308/run_3308.py` with only the save replaced (prep, run, restore unchanged): c5h-g, `VP64Options.ini`
`[Renderer] ServerRenderer = 1`, the composite and its manifest beside the proxy, D33 by darkred.py minus Overlay, no
viewmask marker, the proxy's own server.cfg (`render=1`, `render_section=Local\VP64Shell_42972`, ring hand-over).
Builds: the x64 DLL of the pin layout `x64-04b1211b-pa803776` (74588135..., COMMON 18) with server.exe 2C75955D... and
proxy 43F08311... built from this branch (e3cda483 = main c887055a + 4109's research and scripts; build line "dirty=1"
from uncommitted research text only) with `server/build_server.py` on 0322's pinned fork copy and deps (the pin layout's
own server.exe, a803776, has no feed: it predates epic 3300's merge), composite 1F7A0E39... from `shell/build.cmd`. Save
`C5H-0166-TINY_P15` (copy of `C5H-0166-TINY_SETUP`, removed after), turn 18, 4 AI turns (5.4-6.3 s each), 0 LOUD lines
in server.log, render.log, vpproxy.log and shell_composite.log, guard check 0, start-up watch ok, harness stop 0, the
instance restored by hash. `feed-per-turn.tsv` (copy `shell/evidence/0166/4109/feed-per-turn_p15.tsv`):

| Turn (row = end of turn) | plot_state (cumulative) | fow | yield | rf_improvement (full refresh found an improvement no notification had patched) |
|---|---|---|---|---|
| 18 (load) | 34 | 107 | 15 | 0 |
| 19 | 45 (+11) | 223 | 23 | 2 |
| 20 | 49 (+4) | 324 | 26 | 2 |
| 21 | 50 (+1) | 432 | 27 | 4 |

On this Tiny save the load brings only **34** layouts (against 9,966 on the huge save), then 11, 4 and 1 per AI turn.
[I: the EXE lays out the plots its landmark system holds something for (improvements, routes, resources) among those
the active team has revealed; the Tiny copy at turn 18 has 25 improvements and 10 route plots, the huge save at turn 350
a revealed, developed map. Not traced.] And the full refresh at each turn change found **2 and then 2 more improvements
changed without a `GameplayPlotStateChange`** (`rf_improvement` 2, 2, 4; 0 in 3308's huge run) [L]: on this save some
improvement changes (AI workers' builds outside the active team's view, whose revealed improvement does not change [I])
reach the feed only through the turn-change refresh. **P15: holds** (it fires live on the product path at a rate the
feed carries: tens of calls per turn at most on Tiny, 7-236 on the huge save), with the two limits for the decal design:
the notification describes the active team's revealed state only, and it comes before the EXE's decal layout (P13); the
decal tap's own generation stays the re-bake signal (route (a)).

So after a load of the huge save the EXE lays out nearly every plot (9,966 of 10,240: one `GameplayPlotStateChange` per
plot whose layout the EXE asks for through `ICvMap1::UpdateLayout`; 34 of 2,016 on the Tiny copy above), and in play a
few to a few hundred per AI turn;
headless (no EXE) it never fires (3303). Fields [C, `renderer-in-server-design.md` 2.1, CvPlot.cpp 13917-14058]: plot,
resource (the active team's view), revealed improvement, improvement state (1 half-built, 2 built, 3 pillaged), revealed
route, route state (the DLL's road types: 0 road, 1 railroad, 2/3 pillaged, 4/5 under construction, 6/7 trade route, 8
none); the feed counts the calls per turn, it does not log the fields per call. For the decals: a route or improvement
change reaches the feed through this notification in the same Update as the DLL's change, i.e. before the EXE's
landmark system has laid out the new decal (P13: the re-bake must wait for the tap).

## 6. The route and the design: what the tap copies, its layout, sizes (4101 with the lead)

Proposal of 4101 (2026-11-15), for the lead's design note and the MC's review. **Route (a) stays**: P1 holds (every
class but Tree_Shadow is in the D33 records, river decals equal bit for bit), P2 holds (the river bed is a decal), P6
holds (only the joined session's own records give its layout). Nothing found needs the EXE's draw.

### 6.1 What the tap copies

1. **Per live decal** (every slot with bit 31 of +0x04 set, in every batch; bit 0 "uploaded" ignored, 2.3): pass (bit
   1 of +0x08), triangle count, priority, the three texture handles, the rectangle, and its **vertex bytes** (3 x
   triangles x 8 bytes from the batch chunk at +0x0C), copied out of the chunk so compaction (1.2) does not matter.
   Identity of a decal = a hash of (key, rectangle, vertex bytes): slots and vertex offsets are reused and move.
2. **The texture table**: for every distinct texture handle in use, the texture's file name, so the server can load
   it through `c5r::gamefs`. The variant table gives the names only for river, floodplain and tree classes (1.3); the
   route curves, farm fields and every model-ground key load their textures elsewhere. **Not solved here**: the
   handle-to-name map is in the engine's resource manager (the FSingleton at 0xCC7330 that 0x8E1740 asks; the handle
   is resource+0x1C -> +0x08). 4102 decodes it (open question 1); until then 4103/4104 can work from a census file
   with names filled in by hand for Tiny SETUP (35 keys) [I: 1-2 h of reading].
3. **The order the EXE draws in**: the draw collects the tile's selection (small decals in the spatial index's
   order, then the large list in list order) and sorts it with 0x648890, the C++ library's introsort (partition,
   heap-sort fallback, insertion sort below 33 entries) on the 12-byte entries [C]: **not stable**. Decals of one
   priority overlap (4,650 farm fields at 0.5 on the huge save), so their order decides the blend; equal results
   need the same input order and the same algorithm and comparator (open question 2). The tap has to give 4104 the
   input order: the spatial index's walk (0x6D7EC0 / 0x53A630, not decoded here) or, simpler, the EXE's own sorted
   order per tile is unreachable without the draw, so the 4106 comparison will show whether ties matter in practice.
4. The constants: the 256-unit large-decal threshold and query margin (0xA039B0 = 256.0), the default priority
   (0x9E402C = 0.1), the world rectangle TDS+0x6C.

### 6.2 Layout and sizes (P12's input) [L sizes]

A fixed header (magic, version, generation, decal count, key count, world rectangle), a key table (16 bytes per key:
priority, three texture ids into a name table), a decal table (48 bytes: key index, pass, triangle count, rectangle,
vertex offset into the vertex block, 64-bit content hash, list order), the vertex block (8 bytes per vertex, as the
EXE's). Sizes from the censuses (live decals; vertices = 3 x triangles):

| Census | Decals | Keys | Decal table (48 B) | Vertex block | Total [I: + names] |
|---|---|---|---|---|---|
| Tiny t0008, D33 | 314 | 12 | 15 KB | 268 KB | 0.28 MB |
| Tiny SETUP, D33 | 504 | 34 | 24 KB | 321 KB | 0.35 MB |
| Tiny t0008, stock | 483 | 13 | 23 KB | 1.50 MB (82 % tree shadows) | 1.5 MB |
| huge t350, stock | 17,776 | 166 | 853 KB | 7.00 MB (tree shadows 0.66 MB) | 7.9 MB |
| huge t350, D33 | 17,788 | 165 | 854 KB | 6.34 MB | 7.2 MB |

The EXE itself holds the same data in its chunks (19 x 0.5 MB on the huge save, 9.8 MB of vertices used of which
7.0 MB live). The queue form of 2026-10 was about 10.9 MB; a shared section of 8-10 MB on the huge save is the size
P12 has to place in the 32-bit address space (4102 measures the largest free block with the tap on).

### 6.3 Change detection

Per Present: read TDS+0xA8..+0xB8 and the batch vector (0x34 x batches: 19 x 52 = 988 bytes on huge) and compare the
counters of 5.2. On a change: re-read only the batches whose counters moved (each batch: its pool, 0x34 x slots, and
its used vertex bytes, at most 0.5 MB), rebuild their decals, diff by content hash against the last publication, and
publish a new generation with the added and removed decals' rectangles (the tiles 0x4387D0 invalidated, P8). As a
safety net against the remove-then-add collision of 5.2, hash every batch's live records at a low rate (once a second:
0.9 MB on huge, about 0.3 ms of hashing [I]). Copying on change, never per Present, keeps the per-Present cost to
the 1 KB read in the steady state (the idle huge save showed one signal value over 40 s, 5.3).

### 6.4 What the server needs per tile

For a tile with world rectangle T: every decal of the pass whose rectangle overlaps T (inclusive comparisons, as
0x742EF0 tests), in the EXE's order (6.1 point 3), with its vertices (positions normalised to its own rectangle, so the
vertex shader needs the rectangle: it is in the constant buffer), its three textures, its priority, and the tile's
DecalViewProj (4109). Pass 0 into the decal layer, pass 1 (Tree_Shadow and one natural-wonder key) into the macro
shadow. A decal change re-bakes exactly the tiles its rectangle overlaps, and those tiles carry tile+0x300 = 1 (P14).

## 7. Open questions

From 4101 (2026-11-15):

1. *Answered by 4102 (2026-11-15), section 8.* **Texture handle to file name** for the direct keys (route curves, farm fields, model grounds, city ground): the
   engine's resource manager (FSingleton at 0xCC7330, built by 0x8DE680) holds the map; 0x8E1740 loads by path through
   0x8DEF40, which normalises the path ('/' to '\'), hashes it (0x43D0E0) and looks it up in a per-type table
   (0x8DE760 -> 0x8DD550); the resource's +0x1C object carries the texture handle at +0x08; handles themselves are
   slots 0..0x2FFF of the renderer's texture table (0x8C5CB0), which stores no name [C]. Not decoded further. Needed
   by 4102/4103 (the server loads textures by name, D85). Two ways: walk the per-type table and read each resource's
   name, or hash the candidate names (DecalTextures.fpk's 75 entries, the improvement models' decal materials) with
   0x43D0E0's rule and look each up; fallback, match by hand on Tiny SETUP's 35 keys.
2. *4111 (14.2): decalsForTile now orders ties small newest first, then large in list order (4106's best); the walk's
   structure read (a grid of points with per-cell lists), the exact order not reproduced.* *Answered 2026-11-15 by 0366
   (section 18): decoded and reproduced; the tap publishes the walk block, decalsForTile orders as the EXE.* **Tie order of equal
   priorities**: the draw's sort is the unstable library introsort; the input order comes from
   the spatial index's walk (0x6D7EC0, 0x53A630) and the large list. Whether ties change pixels is for 4106's
   comparison; an exact order needs the walk decoded.
3. *Answered by 4102 (2026-11-15), section 8.4.* **Farm tracks with albedo and heightmap handle 0** (variant 0 Detail_Road): what the shader samples for handle 0
   (a null texture, a default) is for 4109's capture.
4. **City ground** (6,247 decals on the huge save, the lowest handles): producer not traced.
5. **The bridge picture** (P9): stock's look of a road across a river, and what draws it, from 4109's capture or a
   stock picture.
6. **D33 uploads** (2.3): the batch upload 0x480A30 is called from 0x51B070 (TerrainSystem per-frame update), 0x6BB810
   and 0x6539C0; which of them D33's view mask stops was not traced. Harmless for the tap (it ignores bit 0).
7. **The live road's 16 against 8 tiles** (3408 6.5): the extra stock rectangles were not traced (5.1).

Corrections owed in other files (not 4101's to edit; for the lead): `research/02-exe-internals/world-systems-and-stubs.md`
("decal-name manager 0x45EE20" is the TerrainDecalShader object; "+0x4F4..+0x50C plot/decal grids [I]" are not decal
members, 1.1; "CreateTerrain's tail ... index the never-loaded type table +0xC0": the tail's 0x55D0C0 lays farm tracks
of category 0, the river pieces come from 0x559FF0); `terrain-bake-residency.md` 3 ("0x67915E in 0x678FD0" is in
0x678FF0; 0x68F1B0 is RemoveDecal; 0x6B5A50 is a terrain edit, 5.1); `landmarks-and-models.md` 3 (bridges: no decal
key, 2.2); `terrain-rendering-inventory.md` 2.2 (river banks and flood plains are TDS decals [L], 2.2 here) and
`research/sources/.../river_arrows.md` 4's "the riverbanks are terrain pieces" (a source, not edited: superseded by
2.2 here).

## 8. The texture names (4102)

Task 4102, 2026-11-15 02:00-02:25 EST: headless Ghidra 12.1.4 read-only on a scratch copy of the `civ5` project
(`C:/c5-64bit-work/0166/4102/ghidra-copy`, removed at hand-back; outputs not committed, D83) for 0x8E1740, 0x8DEF40,
0x43D0E0, 0x8DE760, 0x8DD550, 0x8DE680, 0x8E1B30, 0x426BB0, 0x8DD3E0, 0x714B80, 0x8C6540, 0x797200, 0x5865B0, 0x8C5CB0,
0x6AE400; `llvm-objdump` for the call sites; checked live with `shell/tools/decal_probe.py --names` on
`C5H-0166-TINY_SETUP` (c5h-i, stock config of the instance, DLL `i0ship-26234371-ia` 3C2F47DA..., VT present; pid 22672,
in game 02:08; EXE loaded at 0x00CE0000, i.e. ASLR moved it: every address below is a VA at 0x400000 and is rebased).
**Result: every texture handle in use has a name** (68 handles of 657 decals; 0 missing) [L]. Closes 7.1 and 7.3.

### 8.1 Two loaders: one keeps the name, one does not [C]

- **0x8E1740 (load through the resource manager)** creates the manager on first use (an FSingleton pointer at
  0xCC7330, constructor 0x8DE680) and asks 0x8DEF40 for resource type 8 (textures). 0x8DEF40 copies the path, turns '/'
  into '\', interns it (0x43D0E0: a string table returning one shared copy per distinct string, so the same name always
  gives the same pointer) and looks it up in the type's map. The maps: the manager's +0x40 is a `std::map` (MSVC layout:
  +0x04 head node, +0x08 size) from type to that type's map (node value +0x10); a type's map is a `std::map` from the
  interned name pointer (node key +0x0C, compared as a pointer, 0x426BB0) to the resource (node value +0x10). Nodes: +0x00
  left, +0x04 parent, +0x08 right, +0x0C key, +0x10 value, +0x14 colour, +0x15 is-nil; the head's parent is the root.
  The resource's +0x1C object holds the texture handle at +0x08. The EXE itself walks this map from handle to resource
  in 0x8E1B30 (unload by handle): begin node, `+0x10 -> +0x1C -> +0x08 == handle`, next. A load with flag bit 1 is not
  put in the map [C, not seen in use].
- **0x8C6540 (load a file straight into a texture)** reads the file, takes the next handle from the texture handle pool
  (0x8C5CB0 with ECX = 0x413E2C0: a free list, else the counter at pool+0x40, at most 0x3000) and creates the texture.
  **No name is kept anywhere.** Users that matter for decals: TDS Startup (0x714B8A: `blank_texture_spec_decal.dds` into
  TDS+0x4F0) and the farm types (8.3).

So a tap gets a handle's name by **walking the type-8 map once** (2,153 textures on Tiny SETUP; the walk is bounded by
the map's size) and naming the two 0x8C6540 classes from their owners' tables.

### 8.2 The names as the map holds them [L]

The interned key is the **file name without its folder** (the decal file's `assets/Decals/Riverbanks/riverbank_D.dds` is
the key `riverbank_D.dds`), in the case the loader was given (`RoadsandRails_new_D.dds`, `CityBase_Diff_anc.dds`). The
server resolves them by name through `c5r::gamefs` (packs, DLC, VT's loose files, mods), case-insensitively. Live on
Tiny SETUP, every key (priority | albedo | heightmap | spec):

| Class | Names [L] |
|---|---|
| route curves (0.7) | `RoadsandRails_new_D.dds`, `RoadsandRails_new_H.dds`, `RoadsandRails_new_SREF.dds` |
| river bed (0.4) | `riverbank_D.dds` / `_D_Endcap1` / `_D_Endcap2`, `grey.dds`, spec `blank_texture_spec_decal.dds` (TDS+0x4F0) |
| flood plains (0.4) | `floodplains_D[_Endcap1/2].dds`, `floodplains_H[_Endcap1/2].dds`, the blank spec |
| tree shadow (0.1) | `Tree_Shadow_2.dds` twice, the blank spec |
| farm fields (0.5) | 8.3: `assets/Decals/Crops/Crops_Africa_0n_D.dds` + `Crops_Generic_0m_H.dds`, `crops_maize_d.dds` + `Crops_Generic_08_H.dds` (VP), spec `blank_texture.dds` |
| farm tracks (0.6) | handle 0, handle 0, the blank spec (8.4) |
| city ground (0) | `CityBase_Diff_anc.dds`, `CityBase_HT.dds`, `CityBase_SREF.dds` |
| model grounds (0) | `Aluminum_Decal_DIFF` / `Aluminum_ore_Decal_H`, `Ancient_Farm_Decal_D/_H`, `Crop_Ground_D/_H` (five keys of the same names, different handles), `Fountain_Of_Youth_Decal_DIFF` / `fountain_of_youth_Decal_H` / `Fountain_Of_Youth_SREF`, `Mine_Generic_Decal_D/_H/_S`, `Oasis_snap_Decal_D/_H/_S`, `Plant_Resource_Shadows` / `blank_texture` / `blank_texture`, `Plantation_MID_Ground_D/_H`, `Solomon_Mine_Mound_Diff/_H` / `Kilimanjaro_Ground_SREF`, `Sri_Pada_Ground_Diff/_H` / `FX_Blk_Blk4x4`, `Sub_MID_Farm_Grnd_D/_H`, `Wheat_Random_D/_H`; spec mostly the blank spec |

Against 4109's byte-matched table (4.3): the same files, with one difference of name: the route curves' height map is
loaded as `RoadsandRails_new_H.dds` (in `Resource/DX9/Upgrade1Textures.fpk`), where 4.3 matched `roadsandrails_h.dds`
(DecalTextures.fpk) [L names; I: the two payloads are byte-identical, so the matcher's first hit was the other file;
not compared]. The EXE's name is the one to load. Five model-ground keys carry the same three names under different
handles (one per landmark that loaded them [I]); the server may share one texture for them.

### 8.3 The farm-type table (farm fields) [C, L]

The farm fields' textures come from `farm_types.xml`, read by 0x797200 (called at 0x74CE9D from the farm system's
init 0x74CDC0, which loads its default spec `blank_texture.dds` into +0x454 just before). **The farm system is the
static object at VA 0x2D70334** (= 0x2D70310 + 0x24: 0x47347B passes `lea ecx, [esi+0x24]` to 0x74CDC0; the static
constructor at 0x9C7470 builds 0x2D70310 [C]; live at 0x3650334 with the EXE at 0xCE0000 [L]). Layout [C, L]:

| Offset | What |
|---|---|
| +0x24 + 4k | category k (k = 0..7: `Crops_Half_Built`, `Crops_Africa`, `Crops_America`, `Crops_Asia`, `Crops_Europe`, `Crops_Wheat`, `Crops_Wheat_HB`, `Crops_Damage`; the EXE's 0x20-byte string array at 0xA3DCC8): u16 first variant, u16 count |
| +0x44, +0x48, +0x4C | the variants (16 bytes: priority, albedo, heightmap, spec handle), count, capacity; live the table is inline at +0x50 (capacity 64) |
| +0x454 | the default spec handle (`blank_texture.dds`), used where a variant has no `spec` attribute |

Every variant texture is loaded by 0x8C6540: **no name in the EXE**. The reader appends, for each category present in
the file in the order above, one variant per child element (attributes `albedo`, `heightmap`, `spec`, `priority`). So
variant i = (category k, child j) by the ranges at +0x24, and its name is that child's attribute in **the
`farm_types.xml` the EXE read**: Vox Populi replaces the stock file (`MODS/(2) Vox Populi/Core Files/Core Changes/
farm_types.xml`, `import="1"`; stock in `Resource/Common/MiscData.fpk`). Live on Tiny SETUP: 29 variants (8, 8, 1, 1, 8,
1, 1, 1 per category: VP's file exactly), the 66 field decals use variants 8-16 = `Crops_Africa` 1-8 and
`Crops_America` 1 (`crops_maize_d.dds`), all with spec 9247 = +0x454. The tap therefore publishes these names as a
**reference** `@farm_types.xml/<category>/<child index>/<attribute>` (e.g. `@farm_types.xml/Crops_Africa/2/albedo`),
which the server resolves by reading `farm_types.xml` through the game's file system with the mods' precedence; the
XML's values carry folders (`assets/Decals/Crops/Crops_Africa_03_D.dds`) or not (`crops_maize_d.dds`): resolve by file
name. The default spec is published by name.

### 8.4 Handle 0 (farm tracks, 7.3) [C, L, I]

Variant 0 (`Detail_Road`) has no `albedo` and no `heightmap` attribute, so the farm tracks (32 decals on Tiny SETUP,
2,463 on huge) carry handles 0, 0 and the blank spec. Handle 0 is the **first handle the texture pool ever gave out**
(the counter at pool+0x40 starts at 0, 0x8C5CB0 [C]); no texture of the resource map and none of 8.3 has it, nor handle
1 [L]: they are textures the renderer made itself at start-up [I]. 4109's capture shows what the draw binds for the
0.6 decals: an 8 x 8 R8G8B8A8 texture with texel (0, 0) = (255, 0, 0, 255) and every other texel 0, as t0 and t1 (4.3,
"the 8 x 8 dummy") [L]. So the farm tracks draw that texture: nearly transparent colour (alpha 0 except one texel) and
a height of 0 weight except near texel (0, 0) [I: with WRAP sampling and the tracks' texture coordinates, the red texel
repeats along the strip]. The tap publishes handle 0 as `VPSHELL_DECAL_NO_TEXTURE`; the server draws it with a
texture of those 64 texels (built in code, not taken from the EXE). Whether the tracks change any pixel is for 4104 /
4106 against `ref`.

### 8.5 What the tap checks before it reads names (byte patterns, rebased) [C]

0x8E1B6D (the singleton 0xCC7330, `push 8`, the type map's head at +4), 0x8E1B90 (node +0x10 -> resource +0x1C ->
handle +0x08), 0x8DE773 (the type map at manager +0x40, head +0x44), 0x426BC5 (node key +0x0C, right +0x08, left +0x00,
is-nil +0x15), 0x714B8A (`blank_texture_spec_decal.dds` into TDS+0x4F0), 0x74CE86 (`blank_texture.dds` into farm
+0x454, `farm_types.xml` read by 0x797200), 0x47347B (the farm system at +0x24 of its owner), 0x9C7470 (the owner at
0x2D70310), and the strings at 0xA272D4, 0xA3DDC8, 0xA3DDDC, 0xA3DCC8. At run time the walk is bounded (the map's
size, at most 2^17 nodes; the farm table's count <= capacity <= 64, ranges inside the count and summing to it).

## 9. The tap as built (4102)

Task 4102, 2026-11-15. Code: `shell/composite/decal_tap.{h,cpp}` (in `shell_composite.dll`, called at every Present
after the overlay tap; ini key `decal_tap`, default 1 in the shipped `shell_composite.ini`, 0 = state OFF), the layout
`shell/common/shell_decal_tap.h`, the test entry `shell/tests/decal_tap_host.cpp` (a console program that links the tap
and calls it as the composite does, without D3D: the tap uses none) and `shell/tests/test_decal_tap.py`. Behaviour in
words; the byte patterns are in the source (D83: a few bytes each, as the other taps).

### 9.1 What it does

- **Before the first read** 32 byte patterns of the EXE (rebased on its load address: ASLR moves it, 8): the TDS address
  at a RemoveDecal call, AddDecal's batch vector / batch size / chunk size / blank spec, RemoveDecal's record size and
  rectangle offsets, the draw's reads of flags, rectangle, priority, texture handles, first vertex, triangles and vertex
  buffer, and the name path of 8.5 (resource manager, maps, the blank specs, the farm system and its eight category
  strings). One mismatch, or an EXE the composite did not validate: **FAILED PATTERN**, never a read, for the process.
- **Poll** (every Present while the camera block says a map is loaded, `gridValid`): one guarded read of TDS+0x00..+0xBC
  and of the batch vector (0x34 bytes per batch). Its hash against the last one; a batch whose first 0x2C bytes moved is
  marked. **Safety net** every `decal_net_ms` (1000), then one batch per Present: each batch's live records (bytes
  +0x08..+0x33, list order, a word-wise hash) against the hash of its last read; a difference marks the batch (a remove
  and an add that left the counters equal).
- **Read** once the signal has been still for `decal_settle_frames` (2) Presents or `decal_max_wait_ms` (500) after the
  first move, within `decal_budget_ms` (2) per Present: per marked batch, **in one Present**, the record pool and the
  used part of its vertex chunk copied out of the EXE, then the batch's entry read again (a batch that moved during the
  copy is read again later: a torn read), the list walked from its head and the counts checked (live count = listed =
  the list's length); then, **from that copy and over as many Presents as the budget needs** (64 records per step),
  every listed record checked (triangles > 0, 3 x triangles <= 0xFFFE, first vertex + 3 x triangles <= vertices used; a
  finite rectangle with min <= max; handles < 0x3000) and kept with its vertex bytes and a content hash (FNV-1a 64 of
  pass, priority, the three handles, rectangle, triangles, vertex bytes); a batch whose entry moved between its copy and
  the end of that work is read again. A failed check is read again up to 3 times, then **FAILED LAYOUT** (retried once
  per net tick; the last good image stays). *Corrected 2026-11-15 by 4110 (9.6; replaced: one read of the whole batch,
  checks and hashes included, in one Present).*
- **Publish**: the content-hash multiset of every batch, sorted, against the last published one; equal = nothing
  (compaction, an upload or a re-read with no change make no generation). Else the names of every handle in use (8: the
  type-8 map walked once, bounded by its size; the TDS blank spec; the farm table as `@farm_types.xml/...` references; a
  handle without a name: **FAILED NAMES**, no generation, retried every net tick), the image (header, records in batch
  order then the EXE's list order, names in order of first use, vertices, the changed rectangles, the content hashes),
  its FNV-1a 64 and the section `Local\VPShellDecal.<pid>.<generation>` through views of at most 256 KiB unmapped at once
  (no address space kept), **all in steps spread over Presents within `decal_budget_ms`** (9.6); then the control block
  READY under its seqlock. *Corrected 2026-11-15 by 4110 (replaced: "the image ... built in one Present; then over the
  next Presents ... its FNV-1a 64 and the section ... through views of 1 MiB").* The previous generation's section is closed; the current
  one when the server acknowledges it (ackResult 0); a refusal (ackResult != 0) is counted and LOUD, the section stays.
- **The changed block** holds the rectangles added and removed since `changedSince`, the last generation the server
  acknowledged with 0 (none yet: every rectangle). A server that applied a later generation than `changedSince` gets a
  superset of its own changes (harmless re-bakes); one that applied an earlier one re-bakes everything (header comment).
- **Faults**: every read of EXE memory is a guarded loop under SEH (dword-wise for large aligned reads); a fault =
  **FAILED SEH**, the tap is off for the life of the process, the EXE goes on. Every FAILED is one LOUD line (the same
  text again only every 1000th time).
- It calls no EXE function and writes nothing into the EXE (D132 R2); it uses no D3D object.
- *Added 2026-11-15 by 4111 (14.1): every generation also carries the EXE's tile+0x300 of every tile (the area block),
  read at the build's first step and by an idle sweep each `decal_net_ms`; a change of only those bits makes a
  generation.*

### 9.2 Tests [L on the stand-in, 2026-11-15]

`shell/tests/test_decal_tap.py` (19 tests; plain and once under MSVC /fsanitize=address, 19 passed each, 0 ASan reports; with test_bake_tap.py, test_water_tap.py, test_overlay_tap.py 111 passed): on the test entry with the synthetic decal system (two batches, a freed
slot, six live decals of five classes incl. handle 0 and a pass-1 decal, a resource map, a farm table): the image exact
byte by byte (records, names incl. the farm reference and the blank specs, vertices, hashes, changed rectangles), the ack
closes the section, no generation without a change; a change (one removed, one added) makes generation 2 in < 1 s
with changedSince 1 and its two rectangles; an unacknowledged generation's successor is diffed against the last
acknowledged one; gridValid 0 reads nothing; the kill switch OFF and on again; a refused ack counted and LOUD; a layout
break after READY keeps the last image. Negatives, each with state, failReason and the LOUD line asserted: a record past
its chunk, a handle without a name, an absurd batch count, an invalid rectangle (LAYOUT / NAMES, no generation, not
disabled), an unmapped batch vector (SEH, disabled, the host exits 0), the image limit (TOO_BIG), the real path on an
image without the patterns and on an unvalidated EXE (PATTERN), an unknown test mode named; the 32 patterns against the
EXE on disk; the header offsets. In the composite on the stand-in (test_host --fake-exe): the synthetic tap READY and
exact, then `decal_tap=0` through the ini = OFF; the real path on the fake EXE = FAILED PATTERN, the composite runs on.

*Added 2026-11-15 by 4110: three tests on the huge synthetic world (`decal_test=huge`, 9.6) and the host's per-Present
CPU line; the suite is now 22 tests. Results 2026-11-15 04:38-05:01 (`shell/evidence/0166/4110/suites.sh`, builds of
`shell/build.cmd` at eff67598): plain, with test_bake_tap.py, test_water_tap.py and test_overlay_tap.py, **114 passed**
(exit 0); test_decal_tap.py under MSVC /fsanitize=address **22 passed, 0 ASan reports** (exit 0).*

### 9.3 Live: the image against the census, a road (2026-11-15) [L]

Instance c5h-i (Goldberg, F:, VT installed, D94), **in-process route**: the pinned shell test DLL
`i2-shell-test-26234371` (deps/i2-shell-test.lock, SHA-256 EB8D26C0...) loads the composite through
`crashlogs\shell.helper`; D33 by `darkred.py config --arm d33` (config.ini + viewmask marker `clear=0x1`); the composite
of `shell/build.cmd` (this branch) in `mode=passthrough` with `tap`, `bake_tap`, `water_tap`, `overlay_tap` 0 and
`lockstep=0`, so the arms differ only by `decal_tap`; no server: the driver makes the main section itself (as
`shell/tests/tap_live.py`) so that the composite publishes its camera block. Driver
`shell/evidence/0166/4102/live_4102.py` (setup / arm / restore) and `live_chain.sh`; results
`F:/c5-64bit-work/0166/4102/<tag>.json` and `census_<tag>.json`. Not the product path (proxy + server.exe +
`ServerRenderer = 1`): the tap does not depend on the server, but the memory figures below are of this route.

Two launch facts on the way [L]: (1) the harness's frame cap writes the swap chain's Present slot; for a composite that
is not under `MODS\*\` (`framecap.composite_of`) it does so at once and the composite then refuses the Present target
("target ... is in steam_api.dll ... NOT hooked", `run-and-drive-the-game.md` 15): set `C5_FRAME_CAP_AFTER=
shell_composite.dll` for such a launch. (2) Without a server the composite reads no camera (no main section), so the
camera block never says `gridValid` and the tap stays NONE: the driver creates `Local\VPShellSection`.

| Arm | Save | In game | Generations at the load | Decals (image = census) | Keys | Image | Names |
|---|---|---|---|---|---|---|---|
| tiny_on | `C5H-0166-4102-TINY_ROAD` (copy of TINY_SETUP) | 03:04:41 | 4 | 503 = 503 | 30 = 30 | 368,400 B | 66 |
| tiny_on2 | the same | 03:42:46 | 4 | 500 = 500 | 30 = 30 | 372,736 B | 66 |
| huge_on | `C5H-0166-HUGE_0350` | 03:08:45 | 14 | 17,709 = 17,709 | 148 = 148 | 7,676,464 B | 256 |
| huge_on2 | the same | 03:47:21 | 12 | 17,657 = 17,657 | 148 = 148 | 7,628,720 B | 256 |
| huge_on3 | the same | 04:01:41 | 10 | 17,679 = 17,679 | 148 = 148 | 7,635,344 B | 256 |

"Image = census": `decal_probe.py`'s census (`--hash-verts --names`) of the same process right after the image was
read, compared as multisets of (priority, the three names, rectangle, triangles, FNV-1a 64 of the vertex bytes): **every
record equal on every arm**, 0 only in one side, every key with the same count [L]. The 32 patterns matched the real
EXE at its ASLR base 0x00CE0000 in every launch.

**A road** (tiny_on, tiny_on2): `p8_edit.lua` (4101) picks (10,22)+(11,22) and sets roads through the Lua channel
(diagnostic, a save copy). tiny_on2 acknowledged generation 4 first (as the server would): **one** generation 0.441 s
after the setter (tiny_on: 0.442 s), `changedSince` 4, +6 -3 route-curve decals (tiny_on: +5 -2, the random layout of
3.1), and the changed block = exactly the rectangles of those 9 records; image = census again (503 = 503). No further
generation in the next 2 s.

### 9.4 P12: cost and memory (indications, COMMON 23) [L]

Workstation, 2026-11-15 03:04-04:06, frame cap 30 fps, `quietlock.py check` before each memory sample listed 4-8 heavy
jobs of other teams (no lock, no window): **indications, not results**.

- **Per Present at rest** (the control block's `pollMs`, mean over 600 Presents, 45-65 s after in game, no change):
  Tiny 0.0088 ms; huge 0.1137 ms with the first safety net (every batch's records hashed byte-wise in one Present each
  second), **0.0350 ms** after it was changed to a word-wise hash, one batch per Present (huge_on3). The P12 target was
  below 0.1 ms (the bake tap's 0.061-0.079).
- **A publication** (huge, during the load: 10-14 generations while the EXE lays out the map): render-thread CPU 26-53
  ms per generation, spread: the image built in one Present (3.2-11.0 ms: the set, its sort, names, the vertex copy),
  then its FNV-1a 64 and the section in 1 MiB views over 8-18 Presents within `decal_budget_ms` = 2; wall 0.27-0.93 s
  from the first read to READY. Before the staging (huge_on) one Present took the whole 35-75 ms. Tiny: 3-4 ms in 1-2
  Presents. ~~The largest single-Present cost left is the build (up to 11 ms on huge); splitting it (per batch) is a named
  follow-up, not done.~~ *Corrected 2026-11-15 by 4110 (9.6): done; the build, the batch reads' checks and hashes, the
  hash and the section are now steps within `decal_budget_ms`; live on the huge save the largest Present of a
  publication fell from 5.0-13.9 ms (4102's build in one Present, 11 generations) to 2.2-2.9 ms (4 generations).*
- **Image and section**: 7.63-7.68 MB on huge (17.7k decals, 0.79 M vertices, 256 names), 0.37 MB on Tiny; within 6.2's
  estimate (7.9 MB with names). One section is held between generations (the current one until its ack), as system
  commit: the EXE maps none of it (views of 1 MiB only while writing).
- **EXE private bytes and largest free block** 20 s after in game (huge, three launches per arm): tap on 1606.6, 1593.8,
  1596.2 MB (mean 1598.9) against off 1578.1, 1591.1, 1593.6 MB (mean 1587.6): **about +11 MB with the tap**, inside the
  launch-to-launch spread of about 15 MB; largest free block on 1606.3, 1613.8, 1621.4 MB against off 1614.4, 1603.3,
  1619.7 MB: no difference seen. Expected from the code: the per-batch vertex stores (about 6.3 MB of live vertex bytes,
  capacities up to 19 x 0.5 MB), the records (17.7k x 52 B), two decal sets (2 x 0.4 MB), plus the image (7.7 MB) only
  while a generation is built. Tiny: 1378.2 and 1367.1 MB (no off arm).
- Not measured: the product path's figures (server + proxy), the testbed (COMMON 23), a pan in stock (Forest's
  Tree_Shadow churn; D33 has none), a reload in the same EXE.

### 9.5 Limits and follow-ups (4102)

- The tie order among equal priorities is the list order of each batch, batches in vector order (7.2 stays open: the
  draw's spatial-index walk is not reproduced).
- A decal whose vertex bytes the EXE writes after its counters, read by the tap between the two, would be published
  with stale vertices until its batch changes again [I: not seen; image = census on every arm]; a vertex-bytes net for
  the last generation's adds would close it.
- `FAILED` keeps the last good image and its generation in the control block; 4105 decides what an install failure is
  (D118); `ACK_TIMEOUT` is not used (the current section stays until its ack or the next generation).
- *Added 2026-11-15 by 4110:* spreading the work makes a generation later: the road of 9.6 reached READY 1.34 s after
  the setter (4102's one-Present build: 0.87 s; P13's "within about 1 s"), because 27 Presents at the 30 fps cap carry
  the 49 ms of work at 2 ms each. `decal_budget_ms` trades the two (4105 may raise it while the server waits for a
  generation, or the server may re-bake from the feed first, 5.F).
- *Added 2026-11-15 by 4110:* during the load, while the EXE lays out decals all the time, a build restarts up to 3 times
  in a row and then publishes its snapshot (9.6): the huge load gave 3 generations where 4102's tap gave 10; each is
  still one consistent read. The floor of a Present is one step: the largest steps seen live were 0.5-0.8 ms (a batch's
  copy out of the EXE, a batch's sort) and one 2.16 ms 64-record check-and-hash step (a preemption or the first touch
  of a new buffer [I]); a batch's copy out of the EXE (its record pool and up to 0.5 MB of vertices) is not split, so
  that the copy stays one consistent read of the batch.

### 9.6 The publication spread over Presents (4110)

Task 4110, 2026-11-15 04:16-05:10 EST, branch `task-4110-tap-split-research`. Code: `shell/composite/decal_tap.cpp`
(the layout `shell/common/shell_decal_tap.h` is unchanged), `shell/tests/decal_tap_host.cpp`,
`shell/tests/test_decal_tap.py`; scripts in `shell/evidence/0166/4110/`.

- **What changed.** (1) A batch read is two parts: `ReadBatchCopy` (one Present: the record pool and the used vertex
  bytes copied out of the EXE, the entry read again, the list walked and counted) and `ReadBatchWork` (from that copy,
  64 records per step: the checks, the vertex copy, the content hash; at its end the batch's entry in this Present's
  poll must still equal the one of the copy, else the batch is read again). (2) The build is a staged state machine
  (`StepBuild`): B_SET (per batch: its hashes and rectangles, sorted), B_MERGE (one pair of sorted runs per step), B_SAME
  (no generation when the set equals the published one), B_USED, B_NAMES (one walk of the texture map), B_DIFF, B_ALLOC,
  B_RECORDS (per batch: records, names in order of first use, vertices, hashes), B_FINISH (changed block, header). (3) The
  FNV-1a 64 in 64 KiB steps and the section in views of 256 KiB (`StepPublish`). (4) Every step runs only while the
  Present's time so far plus the step's estimate (a decaying maximum of its measured cost per unit, `Fits` / `Learn`)
  fits `decal_budget_ms`; the first step of a Present always runs. (5) No batch is read while a stage is in progress, so
  the build works on one snapshot of the tap's copies; a batch that moves meanwhile (the poll's signal) **restarts the
  build** from the read, at most `DT_BUILD_RESTARTS` = 3 times in a row; then the snapshot is published and the batch is
  read after it. A batch count that fell always restarts it. Before the image leaves the build, `SnapshotIntact` checks
  that every batch still holds the copy the build began with (a per-batch read sequence). (6) The image's
  `changedSince` and the control block's are the base generation taken at B_DIFF (an acknowledgement arriving during
  the build no longer changes the header's value against its changed block). (7) A log line per publication: "decal_tap:
  publication cost (READY | no change of the content set | failed, generation N): P Presents from the first read, per
  Present max X ms, mean Y ms (decal_budget_ms B), R build restarts; largest step (ms): ...".
- **The huge synthetic world** (`decal_test=huge`, test only): 17,800 decals in 19 batches (936-937 per batch, a free
  slot in each), 774,300 vertices, 257 names (256 in a 3,000-node texture map, plus the TDS blank spec), 148 keys incl.
  handle 0 and a pass-1 key; a 7.53 MB image; `decal_test_midbuild=N` changes batch 5 (frees or restores one decal)
  during each of the next N builds, at its B_RECORDS step. The host logs every Present's CPU time (QueryThreadCycleTime,
  calibrated against QueryPerformanceCounter; the first Present, which builds the test world, apart).
- **Tests** [L on the host, plain build, workstation, 2026-11-15; not under the quiet lock: indications for the times]:
  `test_huge_spread_equals_one_shot_and_stays_in_budget`: budget 1000 (one Present: the host's CPU max 52-53 ms; the
  negative of the check) against budget 2 (36 Presents from the first read, per Present max 2.000 ms in the tap, host
  CPU max 1.996 ms): **the two images byte-equal** (FNV-1a 64 6C3CC1A4C63A67D0), and every record, name, vertex range
  and a sample of content hashes equal to the generator recomputed in Python; limit asserted: budget + 1.5 ms (+ 3 ms
  under ASan). `test_huge_batch_changed_mid_build_restarts_the_build` (negative): the change at B_RECORDS of batch 5,
  "build restarted at step 7 (a batch moved during the build; restart 1 ...)", generation 1 = the changed world exactly
  (17,799 decals, not a mix) and byte-equal to the one-shot build with the same change.
  `test_huge_restart_limit_publishes_the_consistent_snapshot` (negative): a change in every build; 3 restarts, then
  "a batch moved during the build after 3 restarts: the snapshot is published", generation 1 = the world after the
  third change exactly; the next build finds the same set ("no change of the content set"), no generation 2.
  Before the read split, the largest Present was a batch read of 1.4-2.7 ms (the vertex hash of ~0.33 MB per batch);
  after it, the largest steps are 0.1-0.35 ms on the host.
- **Live** [L, indications, COMMON 23]: c5h-i (Goldberg, F:, VT installed, D94), route as 9.3 (the pinned shell test DLL
  `i2-shell-test-26234371` EB8D26C0..., D33 by darkred.py, the composite in passthrough with the other taps off,
  `decal_budget_ms=2`, no server: the driver makes the main section and acknowledges as the server would), save
  `C5H-0166-HUGE_0350`; arm **before** = 4102's tap (`epic-0166-decals` e2cc0123, composite B162935D...), arm **after**
  = 4110's (eff67598, composite 07CC1E65...); each: launch, in game, 30 s, acknowledge, `quietlock.py check --quiet`, a
  road across a river edge through the Lua channel (4101's `p8_edit.lua`, diagnostic, on the save copy), 5 s, harness
  stop. Driver `shell/evidence/0166/4110/live_4110.py` + `live_chain.sh`, figures by `summarize.py`; results
  `F:/c5-64bit-work/0166/4110/huge_road_{before,after}.json` (2026-11-15 04:56-05:03). `quietlock.py check --quiet` was
  NOT QUIET before both roads (before: 5 heavy jobs of 0140, 4105 and 3911, among them two other games; after: one
  other game, 3911's; this task's own suites had ended at 05:01, during the after arm's load):

  | Arm | Generations (load + road) | Largest Present of a publication | Mean per Present of a publication | Road: READY after the setter |
  |---|---|---|---|---|
  | before (4102) | 10 + 1 | at least the one-Present build: 5.0, 6.8, 8.2, 9.3, 8.5, 8.9, 8.9, 8.5, 12.0, **13.9** ms at the load; **5.0** ms for the road | (not logged) | 0.87 s |
  | after (4110) | 3 + 1 | **2.20, 2.71, 2.93** ms at the load (0, 3, 3 restarts); **2.25** ms for the road | 1.72, 1.16, 0.89 ms at the load; 1.87 ms for the road (27 Presents) | 1.34 s |

  "Before" has no per-Present line: its build ran in one Present, so the build time it logged is a lower bound of that
  Present's cost. The road added 2 route-curve decals in both arms (17,741 and 17,585 decals: the layouts are random,
  3.1). The after arm's composite CPU per generation (48.9-91.9 ms) is in the range of the before arm's (34.1-66.5 ms)
  plus the per-Present poll over more Presents; not compared statistically (one launch per arm). Not done live: the
  image against a census (4102's check, 9.3; the image is byte-equal to a one-shot build in the tests), the product
  path, the testbed. The decal member of the scene bundle (4103)

Written 2026-11-15 02:20 EST by worker 4103 (branch `task-4103-decal-source`, from `epic-0166-decals` aaf499c0). Code:
`server/src/render/srv_decal_tap.{h,cpp}` (the reader, the file form), `render/src/decal/decal_set.cpp` (the per-tile
query, its grid, the changed tiles; replaces lead-0166's `decal_set_stub.cpp`), additions in the DecalSet part of
`render/include/c5r/decal/decal.h`. Tests: `server/tests/render/decal/`. Everything below is `[C]` (read in this code
and asserted by its tests) unless marked; no game ran and no game data was used (D83: the tests make their images).

### 10.1 The reader `srvrender::DecalTapReader`

- **Handshake** (layout `shell/common/shell_decal_tap.h`, 4102's): the reader of EXE pid P opens
  `Local\VPShellDecalTap.<P>` (read-write, 4096 bytes; looked for again every 250 ms while absent) and takes a copy of
  the control block under its seqlock at each `poll()`. A block whose seq stays odd or moves during 16 tries is counted
  (`torn`) and read again at the next poll; it is never acked. Only state READY is read; a generation already acked is
  not read again. The image section is opened by name, its view's size checked against `imageBytes`, and the image
  **copied out first**: every check runs on the copy, so a writer cannot change a byte after its check. Then the
  immutable `c5r::decal::DecalSet` is built (grid included) and swapped in whole under a mutex
  (`std::shared_ptr<const DecalSet>`); a refused image leaves the last good set, the same object. Every image looked at
  is acked: `ackServerPid`, `ackResult`, then `ackGeneration` last.
- **Checks and their ackResult**, in this order (each with a counter `refusedBy[reason]` and a `c5r::Err` for the file
  form):

  | Reason | Check | ackResult | File form |
  |---|---|---|---|
  | block_magic | control block magic C5DT | -4 | - |
  | block | exePid = P; decalCount <= 262,144; 112 <= imageBytes <= 32 MiB; imageName = `Local\VPShellDecal.<P>.<generation>` | 5 BadLayout | - |
  | stale | generation newer than the applied set's | -3 | - |
  | open | the section opens and maps (file: opens and reads) | -2 | Io |
  | truncated | the view (file) holds imageBytes; at least the 112-byte header | 2 Truncated | Truncated |
  | magic, version | image magic C5DT, version 1 | -4 | BadMagic, BadVersion |
  | sizes | headerBytes 112, decalBytes 48 | -4 | BadVersion |
  | hash | the block's fnv1a64 is not 0 and equals FNV-1a 64 over imageBytes | -1 | - (the bundle's SHA-256) |
  | disagree | image and block agree on imageBytes, generation, loadSeq, decalCount (file: imageBytes = file size) | 5 | BadLayout |
  | count | decals <= 262,144, names <= 4,096, vertices <= 4 Mi, changed <= 524,288 | 5 | BadLayout |
  | offset | each non-empty block inside [112, imageBytes]; an empty one's offset <= imageBytes | 2 | Truncated |
  | align | each non-empty block 16-byte aligned; no two overlap | 5 | BadLayout |
  | world | world rectangle finite, min <= max | 5 | BadLayout |
  | rect | each record's rectangle finite, min <= max, inside the world rectangle +- 1 sample | 5 | BadLayout |
  | record | priority finite; flags only bit 0; 1 <= triangles <= 21,844 (3 x triangles <= 0xFFFE, 1.2) | 5 | BadLayout |
  | vertex | firstVertex + 3 x triangles <= vertexCount (64-bit sum) | 5 | BadLayout |
  | texture | each of the three indices < nameCount or 0xFFFFFFFF | 5 | BadLayout |
  | name | each path zero-terminated within its 192 bytes and not empty | 5 | BadLayout |
  | changed | each changed rectangle finite, min <= max, inside the world rectangle +- 1 | 5 | BadLayout |
  | memory | an allocation failed while copying or building | 9 OutOfMemory | OutOfMemory |
  | area (4111) | the area block: grid = the world rectangle's tiles (at most 1024 per axis), no bit past the last tile (past the image: offset; overlap or alignment: align) | 5 | BadLayout |

  Strict on purpose: a flag bit other than bit 0 is refused, so a new flag needs a version step.
- **What it exposes**: `current()` (shared pointer; null before a set), `generation()`, `drainChanges()` (10.3),
  `counters()` (polls, controlOpens, torn, accepted, refused, refusedBy[22], fileLoads, fileRefused, changesQueued,
  changeOverflow, rectsNotListed, decals, lastApplyMs), `lastResult()`/`lastWhy()`, and `decalsForTile(ox, oy, list)`,
  which returns the set pointer that keeps `list.set` alive. `poll()`, `loadFile()`, `loadImage()` run on one thread
  (the render thread); the getters may be called from any thread.
- **A decal's identity** across generations (`Decal::hash`, `DecalContentHash`): FNV-1a 64 of its rectangle,
  priority, pass, the three texture **names** (not indices: the name table can be renumbered) and its vertex bytes.
  firstVertex and listOrder are left out (they move, 1.2).
- **Not wired**: the scene, the residency and the host do not own a reader yet (4105). The image's `loadSeq` is in the
  set; comparing it with the camera block's gridSeq (as the water reader does) is for 4105, which has the main section.
- The reader compiles into server.exe already (`build_server.py` globs `server/src/render/*.cpp`), unused until 4105.

### 10.2 The per-tile query `c5r::decal::decalsForTile` and its grid

- **The square, chosen 2026-11-15**: a decal belongs to the tile of origin (ox, oy) (multiples of 64) when its
  rectangle overlaps **[ox - 2, ox + 66] x [oy - 2, oy + 66], inclusive** (min <= hi and max >= lo; a NaN is in no tile).
  Why: the decal VS draws at rectangle + (1, 1), and the tile's DecalViewProj maps world origin - 0.94 .. origin + 65.06
  onto the 512 viewport (4.2: tx = -16.4868 at tile 512 gives clip -1 at x = 511.06); in rectangle coordinates the tile
  sees origin - 1.94 .. origin + 64.06. The square is a superset of that and of the tile's 64-sample square, so no
  decal that can touch a texel is left out; a decal it adds beyond that is clipped and draws nothing. Inclusive, as 6.4
  asks. [C the arithmetic; I that the EXE's own selection (the tile rectangle grown by 256, 1.4) differs from this only by
  decals that draw nothing: 4104/4106 confirm on `ref`.] Constants `kTileQueryLo = -2`, `kTileQueryHi = 66` in `decal.h`.
- **Order**: ascending priority (a NaN last, so the comparator stays a strict weak order), ties by listOrder, then by
  the index in the set. Stable and total; whether the EXE's unstable introsort orders ties differently stays 7.2's.
- **The grid** (`buildIndex`, run by the reader for every set): one cell per tile; cell (cx, cy) lists, already in draw
  order, every decal whose rectangle overlaps tile (64 cx, 64 cy)'s square. Each decal's cell range per axis comes from
  the closed form (ceil((min - 66) / 64) .. floor((max + 2) / 64)) corrected by the exact predicate at both ends, so the
  grid holds exactly what a scan finds. Built from one sort of the whole set (O(n log n)) and two passes over the cell
  entries (counts, then a fill in draw order). A 64-aligned query is one cell's list (O(decals of the tile), no sort);
  any other origin, or a set without a grid (a non-finite rectangle, one beyond +-2^20 samples, more than 4 M cells), is
  a full scan plus a sort (O(n log n)).
- **Cost [I indication, workstation, 2026-11-15 02:15, plain clang-cl /O2 build, not under the quiet lock, COMMON 23]**:
  a random set of 17,800 decals on 3,584 x 2,560 samples (the huge save's count; 10 % of them up to 400 x 300 samples,
  a quarter on square edges): grid 58 x 42 cells, 56,643 entries (3.2 per decal), built in 5.8 ms; all 2,240 tiles
  queried at 0.45 us per tile; grid = scan = brute force on every tile and on 200 unaligned origins (case `tile_query`).
- **The changed tiles** (`tilesForRects(rects, n, worldRect)`): every tile of the world rectangle (origins 64 x
  floor(min / 64) .. 64 x floor(max / 64)) whose square overlaps one of the rectangles, by the same predicate; sorted by
  y then x, unique; a NaN or inverted rectangle skipped, an infinite edge clamped to the world.

### 10.3 Changes per generation (the queue 4105 drains)

Each applied set (tap or file) queues one `DecalChange`: generation, previousGeneration (0: none before), worldRect, and
`rects` = the image's changed rectangles plus the rectangle of every decal added or removed **by content** (hash diff of
the previous set against the new one, as multisets) that the image did not list (`rectsNotListed` counts them). So a
generation the reader never saw (two published between two polls, or a refused one) and the first set of a reader
(every decal is "added") still raise the right tiles. The tiles to re-bake are `tilesForRects(c.rects, ...)`. The queue
holds 256 entries; one more collapses it into a single entry with `all = true` (re-bake every tile that has or had
decals), counted in `changeOverflow`.

### 10.4 The file form and the bundle member

- **Form**: `decals/load<NNNN>.dtap` is the image bytes as is (`VPShellDecalImage` header first, magic C5DT, version 1,
  exactly `imageBytes` long, no page-rounding tail), like the C5HF member (`offline-scene-harness.md` 2.1) and unlike
  that contract's 16-byte member header: the image's own header carries magic, version and sizes. NNNN = the set's
  loadSeq, four digits.
- **Reader**: `DecalTapReader::loadFile(path)` / `loadImage(bytes, n)`: the image checks of 10.1 without the block's
  (no hash: the manifest's SHA-256 covers the file), the file exactly imageBytes long (shorter: Truncated; longer:
  BadLayout), applied like a polled set (set swapped whole, a change queued). Returns a `c5r::Status` with the code of
  the table; `fileLoads` / `fileRefused` count.
- **Writer (the recorder's side)**: `DecalTapReader::writeCurrentImage(path)`: the current set's image bytes as the
  reader accepted them (the reader keeps that copy: about 7.2 MB on the huge save, 6.2), written to `path.tmp` and moved
  over `path`; NotFound before a set, Io on a write failure.
- **How the recorder gets it [I, proposed to lead-0301]**: the reader keeps the raw image of the set in force, so the
  recorder needs no hook inside `poll()`: at the span's first frame it writes the current set (the decal state the
  span's bakes used) as `decals/load<NNNN>.dtap`. A generation applied during the span is not in version 1 of the
  member; the recorder notes it in `version_notes` ("decal generation changed inside the span") until a later version
  names each generation with the loop iteration that applied it. The member is game data (the player's EXE's records and
  texture names): never committed (D83).
- **Manifest entry [I, proposed]**: `"decals/load0001.dtap": {"kind": "decal_tap", "version": 1, "bytes": ..., "sha256":
  ...}`; the path rule `decals/load<4 digits>.dtap`; the checker's own test: magic C5DT, version 1, header 112, record
  48, imageBytes = file size; everything else by `loadFile`. Posted on queue thread 0301 for lead-0301, who owns
  `offline-scene-harness.md` (the entry id is in issue 4103's log).

### 10.5 Tests (`server/tests/render/decal/`, 2026-11-15)

`build_decal_tests.py --out DIR [--asan]` builds `decal_tap_test.exe` from the reader, `srv_render_common.cpp`,
`decal_set.cpp`, `status.cpp` and the test (no c5render, no D3D). `test_decal_tap.py` (env `C5_DECAL_BUILD`, and
`C5_DECAL_ASAN=1` for the ASan binary) runs each case: `section_and_file_equal` (a real named section and a file: equal
sets; the writer's bytes = the test's image), `refusals` (37 refusals through the section, each with ackGeneration,
ackResult, its counter, lastResult and the same set object kept, then a good generation read; and through the file form
with its Status code and counter), `refusals_other` (stale, open, truncated view, five file-form refusals, a FAILED state
not acked, no producer), `torn` (seq left odd: 5 polls, torn = 5, no ack, set kept, then read; and a writer thread
scribbling under the seqlock for 1.5 s: about 1.06 M polls, 0.98 M torn, 1 accepted, 0 refused), `changes_tiles` (exact
tile lists for a change, a skipped generation found by the diff, the edges 62 and 66.01, clamping, the first set, the
queue bound), `tile_query` (order, edges, a decal over 32 tiles, zero decals, NaN and infinite rectangles, the random
17,800 set), `fuzz` (20,000 images with random bytes changed through `loadImage`: 4,360 accepted, 15,640 refused, no
crash, no accepted set with a reference outside it), plus the runner's own negatives (`selftest_fail` exits 1 with FAIL
lines, an unknown case exits 2, the case list matches). Results 2026-11-15 02:18: plain (clang-cl) 10 passed; MSVC
`/fsanitize=address` 10 passed, 0 ASan reports (`log_path` folder empty). `render/build.py --target c5render` builds
with `decal_set.cpp` in the folder's glob (02:21, exit 0).

### 10.6 For 4102 (the producer): what the reader assumes of the layout

- The image's `imageBytes` equals the block's and the section is at least that long; the block's `fnv1a64` is never 0
  for a READY generation (a zero hash is refused).
- `flags` carries only bit 0; `triangles` >= 1 and 3 x triangles <= 0xFFFE; texture index 0xFFFFFFFF for handle 0.
- `changed` rectangles inside the world rectangle +- 1 sample (a removed decal's rectangle is, since the EXE clamps).
- Blocks 16-byte aligned, not overlapping, all after the 112-byte header; an empty block's offset <= imageBytes.
- The layout was sufficient; no change is needed. A proposal, not a blocker: the .dtap file has no hash of its own (the
  image header has none); `reserved[0..1]` could carry FNV-1a 64 of the image with those 8 bytes zero, so a file outside
  a bundle is checked too. The bundle does not need it (its SHA-256).

## 11. The decal pass as built (4104)

Written 2026-11-15 from 02:20 EST by task 4104 (branch `task-4104-decal-pass` from `epic-0166-decals`). Marks as above;
[L] here means "measured offline against the EXE's own output in 4109's captures" (D137: no game client was run).
Machine: the workstation, NVIDIA GeForce RTX 4080, driver 32.0.16.1088 (the GPU and driver of the captures), Release
build of render/ (clang-cl), and the MSVC `/fsanitize=address` build for the sanitizer run.

### 11.1 What was built

- **`c5r::decal::DecalPass`** (`render/src/decal/decal_pass.cpp`, interface `render/include/c5r/decal/decal.h`):
  `init(device, ShaderSection, GameFiles*, DecalShaderVas)` creates the EXE's TerrainDecalShader VS `0x067e6360` and the
  three-target PS `0x067f8f30` from the EXE at run time (checked by `c5r::shaders` reflection: stage, shader model 5, VS
  inputs POSITION/TEXCOORD only, cb0 within 7 registers, PS t0-t2 2D and three SV_Target outputs), the input layout
  (R16G16_FLOAT POSITION at 0, R16G16_FLOAT TEXCOORD0 at 4, stride 8), the states of 4.1/4.4, one sampler
  (MIN_MAG_MIP_LINEAR, WRAP, MaxLOD FLT_MAX) in s0-s2, the identity R16 index buffer (65,535 entries, index i = i) and
  the EXE's 8 x 8 default texture made in code from 4.3's description. `drawBlend(ctx, originX, originY, list,
  blend[3])` draws the list's pass-0 decals into mip 0 of the three blend targets (SRC_ALPHA / INV_SRC_ALPHA colour,
  ONE / ONE alpha); `drawMacroShadow(..., macroShadow)` the pass-1 decals into the macro-shadow target (ONE / ONE). One
  draw per decal in list order: the decal's vertex run gathered into one dynamic vertex buffer per call and drawn with
  `DrawIndexed(count, 0, base)` over the identity indices (the EXE draws `DrawIndexed(count, firstVertex, 0)` over its
  chunk: the same triangles); constants per draw = `decalConstants()` (11.2). Textures are loaded by name through
  `c5r::gamefs` (bare file name, cached per pass object) or given with `provideTexture(name, tex)`; every file becomes
  one typeless resource with an `_SRGB` view for t0 and a UNORM view for t1 and t2 (11.3). Every check runs for the whole
  pass before anything is drawn, so a failed call draws nothing; the errors and counters are in the header.
  `run()` of the lead's stub was split into `drawBlend` and `drawMacroShadow` (the two places the EXE draws decals);
  Decal / DecalSet / TileDecalList are unchanged.
- **`bakepost`** (`render/src/bakepost/post.cpp`, `render/include/c5r/bake/post.h`): `PostInputs::decalLayer` is gone;
  `PostInputs::decals` (a `TileDecalList*`) and `decalPass` replace it. With pass-0 decals, `drawBlend` runs on the blend
  targets before step 1 and the blend mips are then always generated (`blendMipsReady` is overridden); with pass-1
  decals, `drawMacroShadow` runs after the three macro-shadow draws and before that target's mips. A non-empty list
  without a pass is BadLayout. With no decals (null or empty list) the code path is stage 1's, bit for bit (11.4).
  `render/src/bake/` (the pipeline) was not touched: it passes no decals yet (4105).

### 11.2 DecalViewProj in closed form [L on 6 tiles; C/I for the form]

Fitted to the captured bits of the six tiles of 4.2 and 4.5 ((512, 1600), (576, 1472), (576, 1664), (704, 1664),
(512, 1664), (640, 1664)), 2026-11-15 02:05 (`render/evidence/0166/4104/fitvp.py`): with s = float(1/33) =
0x3CF83E10,

    DecalViewProj = (s, 0, 0, 0), (0, -s, 0, 0), (0, 0, -0.5, 0), (tx, ty, 0.5, 1)
    tx = -(originX + 32) * s - 1/512,   ty = (originY + 32) * s + 1/512

i.e. a translation to the tile's 66-sample square centred on origin + 32 (origin - 1 .. origin + 65), a scale of 1/33
onto clip -1..1 with y flipped, and a shift of half a texel of the 512 target (1/512 in clip units). **All 12 values
bit-equal** [L]. Evaluated in float or in double the result is the same for every tile origin of a 128 x 80-plot map
(10,240 origins, 0 differences), so the EXE's precision (x87 or SSE) does not matter. The forms tried first (D3DX's
ortho off-centre with a half-texel shift of 66/1024 in sample units; an affine form from origin - 1) missed by 1 ulp on
2 to 5 of the 12 values. Registers 4-6 over the 147 distinct decal draws of the six exported bakes (A-D, frame1286,
frame654): r4 = (1, 1, 0, 0) in all, r5 = the rectangle, r6.x = the priority; r6.y/z/w are nonzero (stale) in 18 of
them (3 of the 85 of A-D) and read by neither shader, so the pass writes 0 there. `decalConstants()` reproduces registers 0-5
and 6.x of **every one of the 85 decal draws of tiles A-D bit for bit** (test `game_ref_*`, check in `buildSet`).
*This settles 4.2's [I] and 4.8's first bullet.*

### 11.3 Facts of the capture the pass depends on [L]

- **No GenerateMips between the blend and the decals** (frame844, chunk indexes 2720-2868: the blend draw and its
  FinishCommandList, the 34 decal draws, then `GenerateMips` x 3 on the blend targets, then NormalCalculate). So in a decal
  bake the blend targets' mips are made once, after the decals; bakepost does the same.
- **The identity index buffer**: every decal draw of A-D binds one immutable R16_UINT buffer of 131,070 bytes whose
  65,535 indices are 0..65,534 (`mkref_decal.py` checks it per draw); BaseVertexLocation 0, VB offset 0, stride 8.
- **Views**: over the same 147 decal draws, t0 is always the
  `_SRGB` view of a typeless resource (BC1/BC2/BC3/R8G8B8A8_UNORM_SRGB) and t1, t2 always the UNORM view (BC1, BC3,
  R8G8B8A8_UNORM); the default texture and `tree_shadow_2.dds` are bound as t0 (SRGB) and t1 (UNORM) of one resource.
  Blend targets: RTV R16G16B16A16_FLOAT mip 0; macro shadow: RTV R8G8B8A8_UNORM mip 0; viewport 512 x 512 for both.
- **The default texture** (resource 910) is exactly 4.3's description: 8 x 8 R8G8B8A8_TYPELESS, one mip, texel (0, 0) =
  (255, 0, 0, 255), every other byte 0 (`mkref_decal.py` compares all 256 bytes). The pass makes it in code
  (`defaultDecalTexels`) for `kNoTexture`; with it the tiles that draw it (A, C, D: 3-18 draws each at priority 0.6)
  are bit-equal, which settles 4.3's "(none)" row: the EXE binds its default texture for handle 0 [L], and the 0.6
  draws are the farm tracks of 4101's census (Detail_Road, albedo and heightmap handle 0) [I: by the priority].
- **A legacy A8R8G8B8 DDS is sampled with its channels in their meaning** (corrected 2026-11-15 by 4113, 17.2; this
  bullet read "is created as R8G8B8A8 with the file's bytes unchanged"): `tree_shadow_2.dds` is A8R8G8B8 in
  DecalTextures.fpk (gamefs maps it to B8G8R8A8_UNORM), the capture's resource 13428 is R8G8B8A8_TYPELESS and its bytes
  equal the file's payload [L, one file]; the file is grey (R = B in all 4,096 texels), so "bytes unchanged" and "red
  and blue swapped into R8G8B8A8" give the same bytes. The stone quarry's height map tells them apart (14.3): the EXE
  samples red as red [L]. The pass creates such a file as B8G8R8A8 (gamefs's reading) since 4113.
- **The default spec texture**: the spec slot of the river, tree-shadow and default-texture draws (4.3 "invisible.dds")
  holds the 4 x 4 BC1 payload that four entries share: `blank_texture_spec_decal.dds` (Upgrade1Textures.fpk; the
  TDS+0x4F0 default of 1.1/1.3), `invisible.dds` (UnitsTextures.fpk) and `trading_post_grnd_sref_early.dds`
  (Upgrade1Textures.fpk) [L, SHA-256 of the payloads, c5h-g install, 2026-11-15]. The EXE's name for it is
  `blank_texture_spec_decal.dds` [I: 1.3]. Caution for name tables: ImprovementTextures.fpk has another
  `trading_post_grnd_sref_early.dds` (11,064 B, other bytes); gamefs resolves the bare name to Upgrade1Textures' copy.
- Other texture names of the four tiles, matched byte for byte against the c5h-g install (packs, VT loose files, the
  VP mod) by `mkref_decal.py` and re-read through gamefs by the test: `roadsandrails_new_d/_new_sref`
  (Upgrade1Textures.fpk), `roadsandrails_h`, `crops_africa_01/02/03/05/06/07/08_d`, `crops_generic_01/02/05_h`,
  `blank_texture`, `grey`, `riverbank_d[_endcap1/2]`, `tree_shadow_2` (DecalTextures.fpk), `crops_maize_d` (VP mod
  `Assets/Models/Resources/Maize/`), `ancient_farm_decal_d/_h`, `mine_generic_decal_h/_s` (ImprovementTextures.fpk),
  `mine_generic_decal_d` (MiscTextures.fpk). No citybase decal on these four tiles (VT's loose `citybase_diff_anc.dds`
  was seen only in frame1286, not in the reference set): VT's loose DLC files were exercised by name resolution and bytes only (11.4), not in a bake.

### 11.4 Exactness against the EXE (the four reference tiles of 4.5) [L]

Method (D137, offline): `render/tests/decal/mkref_decal.py` turns each tile of `F:/c5-64bit-work/0166/4109/ref/` into
plain files on `F:/c5-64bit-work/0166/4104/ref/<tile>/` (manifests with SHA-256 in `render/evidence/0166/4104/`). The
test `c5r_decal_test` then, per tile, on a new hardware device: runs **stage 1's `BlendPass`** on the captured blend
inputs (grid, indices, cb0, cb2, the 24 textures), builds the tile's **DecalSet and TileDecalList from the captured
draws** (vertex runs copied out of the captured chunks, rectangle and priority from each draw's register 5 and 6.x,
pass 0 for draws with three targets and 1 for the macro-shadow draw, texture names from the install match, list order
= the capture's draw order), runs **bakepost with the decal hook** (height page, constants, grid normals, cubes and
LUT from the capture), copies the CacheBake output into a 256 x 256, 9-mip R8G8B8A8 pair and generates its mips (the
EXE's near-camera cache: draw, then `GenerateMips`, chunks 2956-2958), and compares all 9 mips of colour and spec with
`expected_*`. Runs 2026-11-15 02:16 and (on the reference remade with the default spec texture's name) 02:25 EST, the same figures (`render/evidence/0166/4104/c5r_decal_test_release.log`):

| Tile | Decal draws (pass 0 + 1) | Captured textures: colour, spec, 9 mips | Textures through gamefs | Second run, new device | Without decals (mip 0 equal: colour / spec) |
|---|---|---|---|---|---|
| A_roadfarm (576, 1664) | 34 + 0 | **100 % of texels, every mip** | **100 %** (21 files) | identical | 1.75 % / 39.28 % |
| B_river (704, 1664) | 6 + 1 | **100 %** | **100 %** (6 files) | identical | 45.75 % / 96.77 % |
| C_run4_f335 (512, 1664) | 32 + 0 | **100 %** | **100 %** (17 files) | identical | 39.22 % / 55.87 % |
| D_run4_f337 (640, 1664) | 11 + 1 | **100 %** | **100 %** (12 files) | identical | 45.74 % / 77.06 % |

So **the server's blend + decal pass + bakepost reproduce the EXE's tile bake with decals bit for bit on this GPU and
driver**, for roads and railroads, eight farm-field types (VP's maize among them), the ancient-farm and mine grounds,
the default-texture draws, the river bed with both end caps and the tree shadows in the macro shadow, both with the
captured textures and with every texture loaded by name from the install (26 distinct files, each byte-equal to the
captured texture subresource by subresource, test `game_ref_textures_from_the_install`; the same test resolves VT's loose `Assets/DLC/VT/Cities/citybase_diff_anc.dds` and finds it byte-equal to frame1286's captured city-base albedo). No differing texel class is
left to explain. The capture's draw order was used: equal priorities (the farm fields at 0.5, the grounds at 0) kept the
order the EXE drew them in. Whether 4103's `decalsForTile` gives that order from the tap's records is 4105's/4106's
question (6.1 point 3, 7.2). The last column is the share the decals change: without them only 1.75 % of tile A's
colour texels stay equal, so the comparison is sensitive to every decal. The BC1 t0/t8 are not in the reference set;
CompressBC of the (equal) CacheBake output is stage 1's exact pass (`terrain-bake-passes.md` 7).

Further results, same build: `c5r_bakepost_test` 16 tests, 0 failed (tile P unchanged; new test
`game_tile_p_with_the_decal_hook_and_no_decals_is_stage_1`: an empty list with a pass, and a pass-0 plus a pass-1 decal
far off the tile, give stage 1's blocks, normal, shadow and cache targets bit for bit); `c5r_decal_test` 10 tests, 0
failed, 0 skipped. Whole render/ suite 2026-11-15: Release 24 of 24 passed, 0 skipped (02:17-02:23); MSVC
`/fsanitize=address` 24 of 24 passed, 0 skipped, **0 ASan reports**, canary reported (02:27-02:34; the decal test
is bit-equal in that build too).

### 11.5 Tests (`render/tests/decal/`, additions in `render/tests/bakepost/`)

- `view_proj_closed_form` (the six captured translations bit for bit, the constant layout, the default texels),
  `count_by_pass`; `negative_init_and_order` (no device BadLayout; draw and provideTexture before init NotFound; blobs
  not in the section NotFound; a PS where the VS belongs and the reverse, a two-target PS, a cube in t1, a VS reading 9
  registers, a VS input the layout lacks: BadLayout; synthetic EXEs compiled in the test, no game bytes);
  `game_synthetic_decals_and_negatives` (EXE shaders: an empty list and an off-tile decal draw nothing, bit for bit; list
  index, texture index, vertex range outside the set, a run not of whole triangles, an empty run: BadLayout and counted;
  a texture name without game files: NotFound and counted; wrong blend or shadow target format, size, bind flags, a null
  target, no context: BadLayout; none of the failed calls changes a target; a covering decal changes only the targets
  of its pass); `game_missing_texture_file_in_the_install` (an empty install: NotFound, nothing drawn);
  `game_ref_A_roadfarm` .. `game_ref_D_run4_f337` (11.4, with a second run on a new device, and the no-decal arm);
  `game_ref_textures_from_the_install`. A missing reference tile or install fails the test by name (not a skip).
- Inputs: `C5R_EXE`; `C5R_DECAL_REF` (default `F:/c5-64bit-work/0166/4104/ref`); `C5R_DECAL_GAME_DIR` (default
  `F:/c5-instances/c5h-g/install`, the captures' install) and `C5R_DECAL_MODS` (default its `MODS/(2) Vox Populi`).
  Make the reference: `python -I render/tests/decal/mkref_decal.py F:/c5-64bit-work/0166/4109/ref/<tile>
  F:/c5-64bit-work/0166/4104/ref/<tile> --install F:/c5-instances/c5h-g/install --mods "<MODS>/(2) Vox Populi"`.

### 11.6 Not verified, open

- Another GPU or driver (the comparison would need a capture there, as stage 1).
- Flood plains, VT's citybase decal and natural-wonder grounds in pass 1 were on none of the four tiles; the
  macro-shadow pass was checked with one tree-shadow draw per tile (B, D).
- The tie order from the tap's records (above); decals longer than 65,535 vertices (refused, BadLayout; the EXE's own
  limit is 3 x triangles <= 0xFFFE, 1.2); TerrainDecalShader pass 0 (two targets, never drawn: 4.1) is not built.
- The A8R8G8B8 rule rests on one file; the EXE's handling of the converted legacy formats (24-bit, 16-bit) was not seen.
  *Corrected 2026-11-15 by 4111 (14.3): the rule is wrong for files whose red and blue differ: the stone quarry's
  A8R8G8B8 height map is sampled by the EXE with its channels in their meaning (B8G8R8A8); with that, 37 more huge
  textures are bit-equal and none gets worse. The pass's `createFromDds` still applies the old rule (a fix for its owner).*
  *Fixed 2026-11-15 by 4113 (17.2): `createFromDds` creates A8R8G8B8 files as B8G8R8A8.*

## 12. Wiring as built (4105)

Task 4105, 2026-11-15 04:16-05:20 EST, branch `task-4105-decal-wiring` (from `epic-0166-decals` e2cc0123, main merged at
0c49a18c). Marks as above; [L] = measured in the live run of 12.6; [C] = in this code and asserted by its tests.

### 12.1 What was built [C]

- **Pipeline** (`render/include/c5r/bake/baker.h`, `pipeline.h`, `render/src/bake/pipeline.cpp`): `BakeRequest::decals`
  (a `const decal::TileDecalList*`; null or empty = the bake as before, bit for bit). `PipelineBaker::init` makes a
  `c5r::decal::DecalPass` beside bakepost (the EXE's TerrainDecalShader blobs from `PipelineConfig::exe`, textures through
  `PipelineConfig::decalFs`, else `fs`); `PipelineConfig::decals` (default on): a decal pass that cannot start fails
  init only when on; `decalVas` lets tests give other VAs. `bakeWith` fills `PostInputs::decals` / `decalPass` (11.1).
  `decalPass()`, `decalStatus()`, `decalCounters()`.
- **Residency source** (`server/src/render/srv_residency_source.*`): a `DecalBaker` sits between the residency and the
  pipeline. Each bake asks the set in force for `decalsForTile(set, 64 x, 64 y)` (10.2); a tile with decals is baked with
  `areaChanged = true` (the EXE's tile+0x300: AddDecal invalidates the decal's rectangle, 5.1; issue 0305), on its first
  bake too. `setDecals(set)` resolves the names (12.2), checks every one against the decal view and makes the set the one
  every later bake reads; `decalChanges(changes)` raises the tiles `tilesForRects(rects, worldRect)` names (the
  residency re-bakes a raised tile with `areaChanged`, its generation > 1), or every tile for a change with `all`
  (queue overflow, 10.3). Nothing in `render/src/bakeresidency/` changed: `raiseTile` and the decorator are enough.
- **Scene** (`server/src/render/srv_render_scene.*`): owns the `srvrender::DecalTapReader` (10.1), made once a block of
  the main section names the EXE's pid (the field tap's `VPShellTap::exePid`, before it the overlay tap's, which the
  composite writes from its first Present), polled every loop iteration. Order in `Scene::poll`: tap (field), water,
  overlay, decals. A set the reader applied goes to `ResidencySource::setDecals`, then (only once the residency has a
  field; until then they wait in the reader's bounded queue) the drained changes go to `decalChanges` with the tap's
  `readyQpc` as their signal. The scene also reads the control block itself (a read-only view, under its seqlock) for its
  state and fail reason. A device rebuild hands the set in force to the new residency source.
- **Host** (`server/src/render/srv_render_host.cpp`, one block): `render_decals` (1 default; 0 = no reader and no
  decals: the picture before 0166), test keys `render_test_decal_wait_ms` (the install bound, 100..600000, default
  20000) and `render_test_decal_trace` (log the first N bakes with decals); `DecalFatalStep` after `FowFatalStep`.
  Row in `server/README.md`'s `render_*` table.
- Counters: the per-second residency line has `decal_gen`, `decal_sets`, `decal_sets_refused`, `decal_changes`,
  `decal_raises`, `decal_bakes`, `decal_bakes_failed`, `decal_changes_skipped`; the totals line a `decal totals:` part
  (generation, sets, refused, in-play faults, changes, raises, skipped, bakes with decals, decal bakes failed, sets
  refused by names, install failure, readers). Per generation one line when the last of its raised land tiles is
  re-baked: `decals: generation N: its K land tiles re-baked, the last X ms after the publication (Y ms after the raise)`
  (the D92 latency; water tiles are never baked and are not waited for).

### 12.2 Texture names: the farm-field references (8.3) [C, L]

- `@farm_types.xml/<category>/<child>/<attribute>` (child 0-based, case-insensitive) is resolved through **the
  farm_types.xml the game's file system gives first**: a `c5r::gamefs::GameFiles` of the install with **every folder of
  `<Documents>\My Games\Sid Meier's Civilization 5\MODS` as an extra loose root above it** (`extraLooseFirst`, sorted by
  name; Documents from `C5_DOCUMENTS`, else `SHGetFolderPathW(CSIDL_PERSONAL)`, as the fog finds GraphicsSettingsDX11.ini).
  The attribute's value becomes its bare file name (`assets/Decals/Crops/Crops_Africa_03_D.dds` -> `Crops_Africa_03_D.dds`).
  Then every name of the set (plain or resolved) must exist in that view; the pass loads them from the same view
  (`PipelineConfig::decalFs`), while the layer textures keep the install-only view (so the layers do not change with the
  decals). A set with a reference is copied once with the names replaced (the reader's set stays as read).
- Live [L, 12.6]: `decals: farm_types.xml from .../MODS/(2) Vox Populi/Core Files/Core Changes/farm_types.xml: 87
  attributes`; 16 references resolved per set from generation 2 on; 64 names, all found.
- Cost [L, indication, workstation, COMMON 23]: the second index with three mod folders, 406 ms at the residency's init
  (render thread, once per device).
- **[I] open**: every mod folder counts, active or not (the activated set is in `Civ5ModsDatabase.db`, which the proxy
  reads with the engine's database library, `proxy_report_mods.cpp`; the server has no SQLite). An inactive mod with its
  own `farm_types.xml` or a texture of the same name would win. A follow-up: the proxy passes the activated mods' folders
  to the server (server.cfg), or the server reads the database.
- **Corrected 2026-11-16 (issue 0348):** the open point above is closed. Only the ACTIVATED mods' folders count now; the server reads `Civ5ModsDatabase.db` itself with a read-only reader and falls back to every folder, with the reason logged, when it cannot. See [decal-names-and-mods.md](decal-names-and-mods.md). (Replaced: "[I] open: every mod folder counts, active or not".)

### 12.3 Re-bakes, their order and t8 (P13, P14, issue 0305) [C, L]

- A generation's tiles are raised only after its set is in force (the reader applies, `setDecals`, then the drain), so
  a re-bake never draws an older set than the change it answers [C: test
  `test_decal_generations_rebake_exactly_their_tiles_after_the_set`, each traced bake later than its publication].
- The feed's route and improvement changes stay a second signal (`BakeRelevantChange`, unchanged). **P13 [L]**: the road
  of 12.6 re-baked **53** tiles: 42 raised by the feed's two `GameplayPlotStateChange` (two adjacent plots, each the
  5 x 7 tiles of the residency's invalidation, `Residency::onPlotsChanged`) and the 11 of decal generation 5. 53 = 42 +
  11 means the 11 decal tiles were baked twice: once for the feed's raise, **before the decal existed**, and once for
  the decal's (had both raises come before one bake, the tile would have been baked once). So a feed change alone bakes
  the tile before its decal is laid out, as P13 said; the decal's own generation re-bakes it about 0.33 s later.
- A field that arrives after sets were applied (the usual order at a load: on Tiny the four load generations came 3.9 s
  before the field) skips the changes of the generations up to the one in force when nothing was resident yet
  (`decal_changes_skipped`): each tile's first bake already drew them; no tile is baked twice at the load [C: test
  `test_set_before_the_field_bakes_once_with_decals`; L: 4 skipped in 12.6].
- *4111 (14.1): the tap now carries the EXE's tile+0x300 of every tile; the residency uses it only behind
  `ResidencySourceConfig::decalAreaFromExe` (off: under D33 the EXE's bit is not stock's); the rule below is the default.*
- t8 (0305): every tile whose decal list is not empty is baked with `areaChanged`, so it has a t8 from its first bake;
  raised tiles too. Whether this set equals the EXE's tile+0x300 set (P14) is 4106's measurement; the decal list uses the
  query square of 10.2 (the decal rectangle grown by 2 samples), not the EXE's own invalidation rectangle.

### 12.4 The failure route (decision 6, D118, D125) [C]

- **Install failures** (LOUD once, `Scene::decalFailed`, then `DecalFatalStep` -> `HostConfig::fatal(5, why)` once:
  server.exe's report and popup of D125, never a picture without decals): the control block FAILED **PATTERN** (the EXE's
  byte patterns: this EXE cannot be read) or **SEH** (an access fault: the tap is off for the life of the EXE, so no later
  decal could reach the server; treated as an install failure whenever it comes); **no decal set within the bound** after
  the first field with a valid camera grid (`gridValid = 1`), unless the tap says OFF; the decal pass not starting (the
  EXE's TerrainDecalShader blobs; it fails the residency's init); a texture name the install and the mods' folders cannot
  resolve, or no readable farm_types.xml (the set is refused by the residency, the previous one stays).
- **The bound: 20 s** after the field and a valid camera grid. Why: the tap publishes once its change signal has been
  still for 2 Presents or 500 ms (9.1), and the composite needs at most 0.93 s of wall time from the first read to READY
  on the huge save (9.4); the tap reads only while the camera block says `gridValid`, which is also when the server's
  wait starts; and on Tiny the first set was there 3.9 s *before* the field [L, 12.6]. 20 s is more than 20 times the
  slowest publication measured, so a slow load (the EXE's main thread busy, Presents rare) does not end a good game,
  while a broken install ends it within 20 s of the map. `render_test_decal_wait_ms` changes it for tests only.
- **In-play faults** (LOUD, the first 20 then every 100th, counted, the last good set stays, the game goes on): an image
  the reader refuses (torn, hash, a bad record: 10.1's table), the tap FAILED LAYOUT / NAMES / TOO_BIG / SECTION (it
  retries), a bake with decals that fails (the tile keeps its last bake; `decal_bakes_failed`).
- **OFF** (the composite's `decal_tap=0`, a test switch): LOUD once, no decals, no bound, not fatal; so the parity pair
  of done D can run with the tap off.

### 12.5 Tests [C, 2026-11-15]

- `render/tests/bake` (`c5r_bake_test`, 20 cases, 0 failed, 0 skipped; Release 04:19, and MSVC `/fsanitize=address`
  04:57-05:01 with `c5r_bakepost_test`, `c5r_bakeresidency_test`, `c5r_decal_test`: 4 of 4, **0 ASan reports**):
  `game_decals_null_or_empty_keep_the_bake_bit_for_bit` (null, an empty list, a set without the tile's decals, decals
  off: t0/t8 bit-equal at 64/256/512 with areaChanged on and off; a decal over the tile changes t0 with one draw,
  deterministic), `game_negative_decal_pass_init_and_request` (blobs not at the VAs: init NotFound when on, no pass when
  off; a request with decals and no pass BadLayout with no output; a list index past the set BadLayout, counted).
- `server/tests/render/test_decal_wiring.py` (15 cases; `render_host_test.exe` with the stand-in baker and a fake decal
  tap written by the test in the layout of `shell_decal_tap.h`; `render_host_test.exe --fatal-hook 1` counts the fatal
  route; `C5_DOCUMENTS` per test): exact re-bakes after the set, a set before the field baked once, refused images
  (hash -1, a record past its vertices 5) keep the last set and count, FAILED LAYOUT in play not fatal, option off reads
  nothing, option refusals (exit 2), install failure fatal exactly once (no tap within the bound, FAILED PATTERN,
  FAILED SEH), OFF LOUD once and not fatal, farm references through a mod's farm_types.xml above the install's, three
  unresolvable names fatal once, an empty set quiet. With `test_residency_source.py` and `test_render_host.py`: plain
  92 passed (05:03-05:09); MSVC `/fsanitize=address` with `test_residency_source.py` 45 passed, **0 ASan reports**
  (05:09-05:14; a first ASan run at 04:58 failed `test_idle_pacing_off_keeps_the_old_poll`, 462 legacy waits against
  > 500 in its 2 s, a timing flake on a loaded machine with `render_tiles=tap`, where the decals do nothing, and
  `test_failed_tile_stays_plain_and_is_counted`, whose totals parser read the decal part's `failed`: renamed
  `install failure`).
- The epic's other suites on this branch: `server/tests/render/decal` 10 passed; `shell/tests/test_decal_tap.py` 19
  passed.

### 12.6 Live: a joined load of Tiny on the product path, and a road (2026-11-15 04:55-04:59) [L]

Instance c5h-g (Goldberg, F:, VT installed, D94), save `C5H-0166-4105-TINY` (copy of `C5H-0166-TINY_SETUP`, 8ce90063...,
turn 18, 56 x 40 plots), driver `shell/evidence/0166/4105/live_4105.py` (run_3308's prep and restore; product path:
`ServerRenderer = 1`, the composite beside the proxy with its manifest, D33 minus Overlay, ring + fence, lockstep, depth;
the shipped composite ini, `decal_tap=1`; server.cfg written by the proxy, `render_decals` at its default). Builds: the
x64 DLL of the pin layout `x64-04b1211b-pa803776` (74588135...), server.exe b0807242... and proxy 7caa80a1... built from
this branch with `server/build_server.py --src D:/c5-64bit-work/9045/src-x64f --deps D:/c5-64bit-work/9045/bF/deps
--config release --offline` (the pin's server.exe a803776 has no decals and no feed), composite d894f107...
(`shell/build.cmd`). Results in `F:/c5-64bit-work/0166/4105/live/` (`joined/joined.json`, logs, pictures); the summary in
`shell/evidence/0166/4105/live_summary.json`.

- **The load**: the reader started from the overlay block's pid at 04:58:15; generations 1-4 (57, 149, 261, 502
  decals) applied 04:58:30-04:58:32, each 1-50 ms after its publication; the field at 04:58:34; bake-ahead 1,453 tiles in
  7.1 s, 0 failed, 0 gray frames after it; 430 bakes with decals by the end, 0 failed; the 4 load changes skipped (12.3).
- **0 LOUD lines** in render.log, server.log, vpproxy.log and shell_composite.log; harness stop rc 0.
- **Pictures** (DIAGNOSTIC, Visual Checks `--issue 4105`): the joined load against stock at the same save and load
  camera (`vc-20261115-045906-workstation-f9c4`, reference `vc-20261115-045500-workstation-6184`, side by side
  `F:/c5-64bit-work/0166/4105/live/compare_load_stock_vs_joined.png`): roads, VP farm fields (another layout: P6) and the
  river bed drawn by the server where stock draws them.
- **The road (D92's latency; a diagnostic stimulus)**: `p8_edit.lua` (4101) set roads on (10, 22) and (11, 22) through
  the Lua channel (the setter returned at qpc 50,876,113 ms). The tap published generation 5 (+6 -3 route-curve decals, 9
  changed rectangles) **333 ms after the setter returned**; the server applied it **2.3 ms** after the publication, raised
  11 land tiles and had them **re-baked 69.4 ms after the publication** (48.1 ms after the raise): within D92's 0.6 s of
  the tap's publication; **0.40 s from the DLL's change** (the setter's return) to the re-baked tiles. Not the player's
  worker (4107's ACCEPTANCE stimulus).

### 12.7 Not done, open

- The scene-bundle member (`srv_scene_bundle.*` kind `decal_tap`, `srv_scene_recorder.*` writing the current image at
  the span's first frame): waits for 0301's merge (lead-0301 #1398); the reader's `writeCurrentImage` (10.4) is ready.
- Active mods only (12.2: done by 0348); the huge save live (4107: E); the install failure shown live on the shipped layout (4107:
  F); P14 (4106).
- Lesson [L]: a stock launch before run_3308's `prep` leaves the front end prepared (the load token in MainMenu.lua), so
  prep's backup records the prepared file and `restore` puts it back; run `tools/harness/frontend.py -i <inst> restore`
  after the stock arm and before prep (here after the restore: provision verify rc 1, then 0).

## 13. Same-session comparison (4106)

Task 4106, 2026-11-15 04:16-05:05 EST (worker 4106, branch `task-4106-decal-compare` from `epic-0166-decals`, with
4105's step 1 `2dc6fea2` merged for `BakeRequest::decals`): the server's caches with decals against the EXE's own caches
**of the same session**, every cached tile, both saves. [L] = measured on the recorded sessions.

### 13.1 Recording [L]

- One **stock** session per save on c5h-i (Goldberg, F:; install with VT, folder hash `45b3b43359279105` by 9047's
  `vthash.py`, TerrainDef.csv `ce16f7e0...`, the same as 9047's), the owner's workstation, RTX 4080, driver 32.0.16.1088.
  Pinned shell test DLL `i2-shell-test-26234371` (SHA-256 `eb8d26c0...`; loads the composite through
  `crashlogs/shell.helper`), game config stock (no darkred, no viewmask marker: the EXE bakes and draws its own decals,
  tree shadows included), the composite of this branch (`shell/build.cmd`, `03d895d4...`) in `mode=passthrough` with
  `tap=1 tap_byte_layer=1 bake_tap=1 decal_tap=1`, `water_tap=0 overlay_tap=0 lockstep=0`, main section
  `Local\VPShellSection_4106` made by `shell/tests/tap_live.py consumer` (no server). Driver
  `shell/evidence/0166/4106/live_4106.py` (`run.sh setup|arm|restore`): after the load, as 3401's `capture.py`, the tile
  records polled every 10 s until the cached count was still for 3 polls and the decal generation still for 20 s; then
  the decal image written as `decals/load0003.dtap` (4103's file form = the image bytes), `layer_probe` (lists, +0x2FC,
  **+0x300**, +0x1CC / +0x1D0 of every tile: `tile_flags.json`), the bake tap's caches as raw BC1 joined with the tile
  records, `layer_probe` again, the decal control block again, `decal_probe.py`'s census.
- Saves `C5H-0166-TINY_0008` (SHA-256 `b2ceb663...` = 3401's `C5H-3401-TINY_0008`) and `C5H-0166-HUGE_0350`
  (`c499d3c6...` = 3401's huge), the copies in c5h-i's saves folder, read only; so 3406's plot art files of those saves
  (`D:/c5-64bit-work/3400/w6/live/{tiny,huge}_art.txt`) give the tile styles.

| Save | In game | Caches t0 + t8 | Changed during the copy | Decals (pass 1) | Generation | Image = census |
|---|---|---|---|---|---|---|
| Tiny t0008 | 04:23:08 | 1,318 + 1,250 | 0 | 482 (168 tree shadows) | 6, unchanged during the copy | 482 = 482 |
| huge t350 | 04:27:45 | 3,085 + 3,051 | 0 | 17,871 (296) | 10, unchanged | 17,871 = 17,871 |

Data (game-derived, never committed): `F:/c5-64bit-work/0166/4106/ref/<save>/` (manifest.json; armS/: caches 112 MB /
268 MB, heightfield.c5hf, decals/load0003.dtap 1.5 MB / 8.3 MB, tile_flags.json, layers_at_bake.json,
decal_census.json, capture.json). The instance's files were restored by hash at the end (`tools.harness.backup
verify`: every file equal or absent).

### 13.2 The comparator: `bakecompare --decals` (`render/tools/bakecompare`)

- `--decals FILE.dtap` is read through **the server's own reader** (`srvrender::DecalTapReader::loadFile`, the
  `srv_decal_tap.cpp` server.exe compiles: every check of 10.1); each tile is baked with `BakeRequest::decals =
  decalsForTile(set, 64 x, 64 y)` (4104's pass through 4105's pipeline). The tap's farm references
  (`@farm_types.xml/<category>/<child>/<attribute>`, 8.3) are resolved in the comparator from the `farm_types.xml` the
  install view gives (VP's `MODS/(2) Vox Populi/Core Files/Core Changes/farm_types.xml`), as 4105 does in the server's
  `ResidencySource`; every name is checked against the install view (`decal_names.not_in_install` in the report: empty
  on both saves). **Without that resolution 693 huge bakes fail NotFound**: the decal pass itself resolves no reference.
- Further options: `--mods DIR` (gamefs `extraLooseRoots`; here VP's folder), `--t8-rule decals` (areaChanged = 4105's
  rule: the tiles of `tilesForRects` over the first set's change, i.e. of every decal), `--tie-order
  list|reverse|small-rev|large-first`, `--tile-flags tile_flags.json` (the EXE's +0x300 on every tile against the server's
  rules), `--drop-decals ORDERS` (diagnostic). New classes "EXE cache stale (+0x1CC > +0x1D0)", "remaining, the tile has
  overlapping decals of equal priority", "remaining, decals without overlapping equal priorities"; aggregates
  `differing[_t0|_t8]`, `with_decals`, `without_decals`, `river_decal`; CSV columns decals, pass1, tie_pairs,
  river_decal, exe_f2fc, exe_f300, srv_t8, srv_area, srv_t8_by_list; report blocks `t8_set`, `area_changed_all_tiles`,
  `decal_pass`, `decal_names`.
- Tests: `render/tools/bakecompare/test_bakecompare_4106.py` (15 cases, each exit code and stderr text asserted: the
  usage refusals, a missing / random / truncated .dtap (3 / 4 / 4), a missing mod folder, the tile-flags refusals, and the
  recorded .dtap accepted) 15 passed with the plain and with the MSVC `/fsanitize=address` build, and the ASan build on
  200 Tiny tiles with decals: exit 0, **0 ASan reports** (`render/evidence/0166/4106/asan_run.sh`, 05:05). The suites
  `c5r_bake_test` and `c5r_decal_test` with this branch: plain 2 of 2 passed, ASan 2 of 2 passed, 0 skipped, 0 ASan
  reports, canary reported (04:54-05:00).
- Runs: `render/evidence/0166/4106/cmp.sh tiny|huge RUN` (EXE `%USERPROFILE%/tools/ghidra-projects/bin/
  CivilizationV_DX11.exe` b2ce96ae..., build 4390913, shaders read from it as in 3404 and 4104; install c5h-i; Release
  clang-cl); 6-18 s per save. Reports `F:/c5-64bit-work/0166/4106/cmp/<save>/<run>/`; summaries by `analyze.py`,
  `figures.py` (`F:/c5-64bit-work/0166/4106/figures.txt`), `residue.py`, `t8_rules.py`, `area_rule.py`, `by_texture.py`,
  `sel32.py`, `dtap_ties.py`, `river_tiles_9047.py` (all `render/evidence/0166/4106/`).

### 13.3 Results, every cached tile [L]

Bit-equal = all mips of the BC1 texture byte-identical; "decoded" = 3404's decoder, mip 0, the per-texel largest channel
difference (8-bit), over the texels of the **differing** textures only. t8 from the EXE's +0x2FC (13.5 for the server's).

| Save, run (tie order) | t0 bit-equal | t8 bit-equal | All | Differing t0: median / p95 / p99 / max | Differing t8 |
|---|---|---|---|---|---|
| Tiny, no decals (stage 1) | 850 / 1,318 (0.645) | 1,041 / 1,250 (0.833) | 1,891 / 2,568 | 0 / 74 / 99 / 231 | 0 / 0 / 54 / 255 |
| Tiny, decals, list order (decalsForTile before 4111) | 1,268 (0.962) | 1,242 (0.994) | 2,510 | 0 / 8 / 28 / 140 | 0 / 0 / 0 / 132 |
| **Tiny, decals, small newest first** (decalsForTile since 4111, 14.2) | **1,295 (0.983)** | **1,248 (0.998)** | **2,543 (0.990)** | **0 / 8 / 39 / 140** (23) | 0 / 0 / 8 / 132 (2) |
| huge, no decals | 1,322 / 3,085 (0.429) | 1,895 / 3,051 (0.621) | 3,217 / 6,136 | 0 / 99 / 135 / 244 | 0 / 16 / 118 / 255 |
| huge, decals, list order | 2,067 (0.670) | 2,714 (0.890) | 4,781 | 0 / 4 / 37 / 223 | 0 / 0 / 0 / 247 |
| **huge, decals, small newest first** (decalsForTile since 4111) | **2,439 (0.791)** | **2,891 (0.948)** | **5,330 (0.869)** | **0 / 0 / 33 / 223** (646) | 0 / 0 / 2 / 239 (160) |

Best order, BC1 blocks equal over all mips: Tiny 0.99895, huge 0.99477; decoded texel-equal (mip 0, all textures):
Tiny 0.99918, huge 0.99577. **Every texture of a tile without decals is bit-equal** (Tiny 780 + 741, huge 1,214 +
1,208). The decal pass drew 2,934 (Tiny) and 31,578 (huge) decals, 0 missing textures, 0 bad decals.

**River tiles (bug 0130, 9047 4.2):** 9047's 40 river t0 tiles (48 "remaining" fair-set Tiny t0 of comparison run 3, 40
with a river on their own plots; the list reproduced by `river_tiles_9047.py` from run 3's CSV, 3406's tile plot lists
and 9047's plot dump): 39 are cached in this session, **39 of 39 bit-equal** in the best order (30 in list order; the 9
others then differ only where equal-priority river and flood-plain pieces overlap). **0130's definition of done holds at
cache level** [L]. All Tiny textures with a river decal: 526 of 531 bit-equal (t8 251 of 251; the 5 t0 are tie tiles).

### 13.4 The cause of every remaining difference (best order) [L counts]

`residue.py`, first match wins:

| Cause | Tiny t0 + t8 | huge t0 + t8 | Evidence | Mark |
|---|---|---|---|---|
| tie order of equal priorities | 15 + 0 | 592 + 133 | below | L numbers, I mechanism |
| the stone quarry ground (`Stone_Quarry_Decal_D.dds`) on the tile | 0 | 40 + 19 | **40 of 40** t0 of tiles with it differ; the EXE's cache shows finer rock detail than the server's draw of the only file of that name in install + VP (`Resource/DX9/Upgrade1Textures.fpk`, 22,000 B = 128 x 128 DXT5 with mips) | L, cause open: the tap's handle -> name for that key, or another file the EXE reads. *Found 2026-11-15 by 4111 (14.3): the name is right; the height map `Stone_Quarry_Decal_H.dds` is A8R8G8B8 and the pass reads it with red and blue swapped; read as B8G8R8A8, 24 t0 + 13 t8 of these become bit-equal, the rest are ties* |
| EXE cache stale: record +0x1CC > +0x1D0 | 0 | 11 + 7 | the EXE changed the tile's content after its bake (state 3, near the camera, wanted size 512) and had not re-baked it when the cache was copied; e.g. (106,28): +0x1CC 60 > +0x1D0 57, the cache lacks a city's ground and an aluminium mine ground the records hold | L |
| east edge column of the wrapping map, with t8 | 6 + 2 (x = 55) | 0 | a horizontally shifted bright detail pattern in t0; as large without decals ((55,18): 0.0165 equal without, 0.6389 with); stage 1's rule for the wrap column, not decals | L, I cause |
| a decal reaching under 4 samples over the tile's edge, absent from the EXE's cache | 0 | 2 + 1 ((39,26) t0 t8: +0x300 = 0, (107,29) t0) | bit-equal when that decal is left out (`--drop-decals`); [I] the EXE did not re-bake the neighbour after the add | L |
| a tree shadow at the tile's border (pass 1) | 2 + 0 ((11,32), (15,30)) | 0 | 0.04-0.07 % of texels, max 28-36; leaving the decal out is worse | L, cause open |
| farm field and track, no overlapping tie | 0 | 1 + 0 ((71,64) t0, 0.02 % of texels) | - | open |

**The tie order.** List order (`decalsForTile` as built: ascending priority, ties by the tap's list order) gives Tiny
2,510 / huge 4,781 bit-equal; **every run of equal priorities reversed** 2,487 / 5,227 (makes 25 / 606 equal, breaks
48 / 160); **small decals (both sides at most 256 samples, the large-decal constant of 1.4) newest first, then the large
ones in list order: 2,543 / 5,330** (on Tiny makes 34 equal and breaks 1); large first, then small newest first: 2,514 /
5,316. So the numbers move with the order and only with it, and the small decals are walked newest first [L]. On Tiny,
tile (37,10)'s two river pieces (orders 44-46 and 47-49) need the second piece first. What stays: tiles with many
overlapping ties (huge: city ground at priority 0, 272 of 360 t0 of tiles with `citybase_diff_mod` differ; farm fields
and tracks at 0.5 / 0.6, 322 of 493 `Crops_Europe` tiles; roads at 0.7, 322 of 623). The EXE's sort leaving insertion
sort (more than 32 entries in the selection) does **not** separate equal from differing textures for a selection grown
by 0, 32, 64, 128 or 256 samples (`sel32.py`), so the walk of the spatial index is not modelled yet. **Exactness on tie
tiles needs 7.2 decoded**: the walk 0x6D7EC0 / 0x53A630 over the tile rectangle grown by 256, then the library introsort
0x648890 on that selection. Recommendation for 4105: the small-newest-first order in `decalsForTile` (or as a choice)
until then.

### 13.5 The t8 set and tile+0x300 (P14, issue 0305) [L]

The EXE makes a t8 where the list rule says so or tile+0x300 = 1 (comparison 7). Cached tiles, the EXE's t8 (a t8 record)
against the server's (`needSecondByList` or areaChanged) for three areaChanged rules at load (`t8_rules.py`):

| areaChanged at load | Tiny (1,318 tiles): both / neither / EXE only / server only | huge (3,085) |
|---|---|---|
| 4105's: `tilesForRects` of every decal (query square -2..66) | 1,250 / 39 / 0 / 29 | 3,045 / 6 / 6 / 28 |
| the plain tile square floor(min / 64) .. floor(max / 64) of every decal | 1,250 / 39 / 0 / 29 | 3,044 / 8 / 7 / 26 |
| the plain square without the river and flood-plain decals | 1,244 / 68 / 6 / 0 | 3,026 / 33 / 25 / 1 |

Every tile of the map (`area_rule.py` on `tile_flags.json`): +0x300 = 1 on 457 of 2,240 Tiny and 3,015 of 10,752 huge
tiles; 4105's rule marks 556 Tiny tiles (99 server only, 0 EXE only). Per class with the plain square: every tile that a
route, farm field, farm track, city or model ground or a tree shadow covers has +0x300 = 1 (0 exceptions on both saves),
but river decals leave it 0 on 99 of 298 Tiny and 87 of 1,995 huge tiles (flood plains 28 of 46, 14 of 236), and 7 Tiny
tiles have +0x300 = 1 with only river decals on them. ~~**P14 fails as stated: the final decal records do not determine
tile+0x300**; CreateTerrain's river decals set it on some tiles and not on others [L], cause not traced [I: the order of
its adds against the tile grid's set-up, or decals added and removed during the load].~~ *Corrected 2026-11-15 by 0367
(14.6) [C, L]: the river and flood-plain decals never set it: CreateTerrain lays them all before it creates the
TerrainRenderer, and AddDecal invalidates only while the renderer exists. A river tile has the bit when a later decal
covers it (mostly tree shadows in stock); the 7 river-only tiles with the bit are tiles of decals removed during the load
(tree shadows, city ground: the watch saw them). Rule: the tiles of every decal but the categories 4-9, plus removed
ones; exact live on Tiny, stock and D33.* The t8 differences change no t0:
the runs with t8 from +0x2FC and from 4105's rule differ in no texture's bit-equality (all 56 server-only t8 tiles have a
bit-equal t0 in both), so the cost is which tiles carry a t8 (memory, bake time, and the spec draw of 0189 in the frame).
For 0305's done ("the t8 set equal"): either a tap reads the EXE's +0x300 per tile (a data read beside the bake tap's tile
records), or 0305 accepts a rule from the decals with 29 Tiny / 33-34 huge differing t8 decisions, all with bit-equal t0.
The MC's. *Done 2026-11-15 by 4111 (14.1): the tap reads the EXE's +0x300 per tile (MC #1519 (a)); with it the t8 set
is equal on both stock recordings; but under D33 the EXE sets the bit on 61 of the 460 Tiny tiles stock marks, so the
product keeps 4105's rule (the bit behind a switch).*

### 13.6 Pictures (Visual Checks, DIAGNOSTIC, `--issue 4106`)

Offline comparison figures (server | EXE | |d| x 8, mip 0 of the t0), `F:/c5-64bit-work/0166/4106/pictures/`, recorded
2026-11-15 04:54 by `render/evidence/0166/4106/pictures.sh` (vc-20261115-045459 to -045505): Tiny (5,14) without decals
(the river channel against the EXE's river bed: 0130's picture) and with the decals in list order (only the tie overlap
differs; bit-equal in the best order); Tiny (55,18) the edge column; huge (49,39) and (28,60) the stone quarry ground;
huge (106,28) a stale EXE cache; huge (107,29) a decal sliver the EXE's cache lacks.

### 13.7 Not done, open

- 7.2 decoded (13.4): the remaining 15 Tiny / 725 huge tie textures. *4111 (14.2): the walk's structure read, not
  reproduced; 14.4 has the figures after 4111.*
- *The stone quarry ground: found by 4111 (14.3), without a capture.* The stone quarry ground and the tree-shadow slivers (13.4): a RenderDoc capture of such a tile (4109's method) settles
  both.
- The east edge column with t8 (13.4) is stage 1's (epic 3400's edge rule); not looked at further.
- A second session of each save, to show the order figures are not session-specific, was not run.

## 14. Exactness fixes (4111)

Task 4111, 2026-11-15 05:19-07:00 EST (worker 4111, branch `task-4111-exact-t8-ties-quarry` from `epic-0166-decals`
61cb6bf6, main merged at 462a349d). Marks as above; [L] = measured on the recorded sessions or live, [C] = in this code
and asserted by its tests, or read in the EXE's code (behaviour only, D83).

### 14.1 The EXE's tile+0x300 of every tile through the tap (P14, 0305, MC #1519 (a)) [C, L]

- **What the tap reads** (`shell/composite/decal_tap.cpp`, `AreaBegin` / `AreaStep`): TerrainSystem (= TDS) +0x75FA4 ->
  the TerrainRenderer TR; TR+0x2310 / +0x2314 / +0x2318 tiles in x, in y, the count; TR+0x1AE0 the tile pointers,
  column-major (index x * tiles in y + y; the array TR+0x1ADC points at, `terrain-rendering-inventory.md` 5.4); per tile
  the dword +0x300, which only 0x4387D0 writes (1) of the decal paths [C, 5.1]. Guarded data reads only, no call, no
  write. Four more byte patterns before the first read (36 in all): 0x68EFE0 (TR = TS+0x75FA4, the bake tap's own),
  0x4387D8 (0x4387D0 reads tiles in x at TR+0x2310), 0x4388E0 (the tile pointers TR+0x1AE0, column-major by TR+0x2314),
  0x438911 (tile+0x300 = 1). Checks: 1 <= tiles <= 1024 per axis, count = x * y, every tile pointer set, every value 0
  or 1; a failure is FAILED LAYOUT (retried at the safety-net tick, LOUD once), a fault FAILED SEH; no TerrainRenderer
  (TS+0x75FA4 = 0): the generation is published without the block.
- **When**: every build reads the whole grid at its first step (`B_AREA`, 1,024 tiles per step within
  `decal_budget_ms`; largest step 0.12 ms on the huge save, 0.07 ms on Tiny [L, composite log of the recordings, an
  indication]), so each generation carries the bits of its time; between builds an **idle sweep** once per
  `decal_net_ms` (1 s) compares the grid with the published bits and starts a build when only they changed (a terrain
  edit invalidates through 0x4387D0 too, 5.1): a generation with the same decals, no changed rectangle, the new bits
  ("tile flags changed"). A decal change abandons the idle sweep (the build reads its own).
- **Layout** (`shell/common/shell_decal_tap.h`, version 1 kept: the fields were reserved, 4103's reader ignores them):
  `VPShellDecalImage::areaOffset` (+88), `areaTilesX` (+92), `areaTilesY` (+96): one bit per tile, bit (y * x tiles + x)
  % 8 of byte (y * x tiles + x) / 8, ceil(x * y / 8) bytes after the hashes, 16-byte aligned, the unused bits of the last
  byte 0; all three 0 = no block. Control block: `areaSet`, `areaTilesX`, `areaTilesY` (+236..+244).
- **The reader** (`server/src/render/srv_decal_tap.*`): the block's grid must equal the world rectangle's tiles
  (floor(max / 64) - floor(min / 64) + 1 per axis; 56 x 40 on Tiny, 128 x 84 on huge [L]), lie inside the image after
  the header, aligned, not overlapping another block, and have no bit past the last tile; else refused (`DREF_AREA`,
  ackResult 5; past the image: `DREF_OFFSET`, Truncated). An image without the block is accepted as before.
  `DecalSet::areaTilesX / areaTilesY / areaChanged` (one byte per tile), `c5r::decal::areaChangedAt(set, x, y, known)`,
  `srvrender::DecalAreaAt` (the bit, else 4105's rule "the tile has decals"), `DecalChange::areaTiles` (tiles whose
  state changed between the previous set and this one: the bits, or the rule where one side has no block), counters
  `areaSets`, `areaTilesChanged`, `areaSet`. The .dtap file form carries the block unchanged (the image bytes).
- **Live, stock** [L, 2026-11-15 05:43-05:55, c5h-f (Goldberg, F:, VT present), pinned shell test DLL
  `i2-shell-test-26234371` (eb8d26c0...), stock game config, the composite of this branch (4bd5cd65...) in passthrough
  with `bake_tap`, `decal_tap` on; driver `shell/evidence/0166/4111/live_4111.py` = 4106's driver with this task's
  folders; saves `C5H-0166-4111-TINY_0008` (b2ceb663..., = 4106's) and `C5H-0166-4111-HUGE_0350` (c499d3c6..., =
  4106's)]: the recorded .dtap's block against `layer_probe`'s tile+0x300 read right after it, every tile: **Tiny 460 =
  460, huge 3,021 = 3,021, 0 differing** (`armS/area_check.json`). Caches Tiny 1,320 + 1,254, huge 3,083 + 3,051, 0
  changed during the copy; image = census (483, 17,720). Data `F:/c5-64bit-work/0166/4111/ref/<save>/` (never
  committed).
- **The t8 set equals the EXE's (P14)** [L]: bakecompare with `--t8-rule tap` (areaChanged = the .dtap's bits, what the
  residency does with the switch below) on these recordings: every tile's server area set = the EXE's tile+0x300
  (**Tiny 2,240 of 2,240, huge 10,752 of 10,752**); the t8 decision on every cached tile: Tiny 1,254 both / 66 neither /
  **0 EXE only / 0 server only**; huge 3,051 / 31 / **0 / 1**. The one server-only tile, huge (110, 28), has +0x2FC = 1
  and +0x300 = 1 but no t8 cache in the EXE's copy: the EXE's own state at copy time, not a rule difference. With
  4105's rule on the same recordings: Tiny 27 server-only t8, huge 7 EXE-only (skipped by the comparator: "the server
  made no t8") and 27 server-only. The t0 / t8 bit-equal counts move only by those 7 tiles (huge 5,335 -> 5,342).
- **Under D33 the EXE sets tile+0x300 on far fewer tiles** [L, 2026-11-15 05:55-05:57, the same instance, save and
  composite, D33 by `darkred.py config --arm d33` (config.ini + the viewmask marker), `live_4111.py d33`, result
  `F:/c5-64bit-work/0166/4111/d33/C5H-0166-4111-TINY_0008.json`]: **61 tiles** against 460 in stock on the same save
  (image = probe, 61 = 61; 314 decals, D33 has no tree shadows, 2.3). The tap's generations show it: generation 1 (61
  decals) carried 0 bits, generation 2 (301 decals) 57, generation 5 (314) 61. ~~So under D33 most decal adds do not reach
  0x4387D0's store [I: 0x4387D0 runs only while the renderer pointer 0xD8C584 is set (5.1); under D33 that holds for
  few of the load's adds; not traced]. **Consequence: the bit equals the stock picture's t8 set only in a stock
  session.** In the product (D33) it would drop the spec term on about 400 Tiny tiles that stock gives a t8, far worse
  than 4105's rule (0 EXE-only, 98-99 server-only).~~ *Corrected 2026-11-15 by 0367 (14.6) [C, L]: every D33 decal add
  after the renderer's creation reaches the store, as in stock; generation 1's 61 decals are the river set, laid before
  the renderer exists in both configurations. The ~400 tiles are stock's Tree_Shadow decals, which D33 does not have
  (Forest off), so no rule from D33's records can give them either (165 Tiny t8 decisions, MC answer 7's trees). On
  D33's own records 4105's rule is the worse one: 275 extra tiles, 130 extra t8 on Tiny.* Hence (4111's decision, before
  0367):
- **The residency** (`server/src/render/srv_residency_source.*`): `ResidencySourceConfig::decalAreaFromExe` (default
  **false**): when set, every bake takes areaChanged from the set's block (decals or not; first bakes and re-bakes), and
  `decalChanges` also raises `DecalChange::areaTiles` (a change of the bit re-bakes that tile); counters `decal_area_exe`,
  `decal_area_raises` in the per-second line. Off, the bake is 4105's, bit for bit. No host key exists for it
  (`srv_render_host.cpp` is not this task's file): the switch is for a stock-configuration session or a later decision.
  What would make the product exact is a rule that reproduces stock's +0x300 from the decal records, which 4106's
  13.5 showed the final records do not determine (river decals); open, see 14.5. *0367 (14.6): under D33 the switch
  itself is that rule (the EXE's bit is stock's rule for the product's decals); a records rule ("no river") is exact on
  D33 Tiny and 23 short on D33 huge. The MC's choice.*
- **Tests** [C, 2026-11-15]: `shell/tests/test_decal_tap.py` (26 cases): the block exact in generation 1 (every bit
  recomputed: every live decal but the river bed marks floor(min / 64) .. floor(max / 64), plus a lone tile), after a
  decal change (the removed and added rectangles marked), in the huge synthetic world (every 5th tile), a flags-only
  generation (`shell_composite.areachange`: generation 2 with the same 6 decals, 0 changed rectangles, the new bit, and
  no generation after it), no TerrainRenderer (READY without a block), negatives: a grid count other than x * y and a
  tile value 7 (FAILED LAYOUT, one LOUD line, no generation, not disabled), the 36 patterns against the EXE on disk.
  `server/tests/render/decal` case `area`: the block read and exposed, `areaChangedAt` inside and outside it, the first
  set's `areaTiles`, a bits-only generation (2 tiles), an image without the block after one with it (the rule again: 2
  tiles), the file form and the writer; negatives through the file form (Status, counter) and the section (ackResult,
  the last good set kept): the block past the image's end, a grid wider and one shorter than the world, a zero count, an
  offset without counts, an overlap, a misaligned block, **a bit past the map** (5 x 3 tiles, bit 15 set); and the old
  layout (every other case: 4102's images) still accepted.
- **Suite runs** [C, 2026-11-15 05:40-06:56, workstation, the machine loaded by other teams' games and suites
  (`quietlock.py check`: 5 heavy jobs at 06:55); `shell/evidence/0166/4111/suites.sh`, logs in the task's scratch]:
  `shell/tests/test_decal_tap.py` plain 26 passed, under MSVC /fsanitize=address **26 passed, 0 ASan reports**; with
  test_bake_tap, test_water_tap, test_overlay_tap plain 117 passed, 1 failed (`test_overlay_tap.py::
  test_late_query_is_counted_stale`: its 1.8 s reader saw no stale episode; passed alone at 06:3x, 1 passed: a timing
  case of the overlay tap under load, no decal code in it). `server/tests/render/decal` plain 11 passed, ASan 11 passed
  (the first ASan run found an overflow in the test's own image writer, fixed). `server/tests/render/test_decal_wiring.py`
  with `test_residency_source.py` plain **45 passed**; ASan 42 passed, 3 failed: `test_failed_tile_stays_plain_and_is_counted`
  (2 frames drawn in its 4 s: the tile was never reached; passed on the rerun) and the two idle-pacing tests (197 legacy
  waits against > 500 in 2 s; 4105 saw the same under load, 12.5), all on paths without decals; 0 ASan reports.
  `render/`: `c5r_decal_test`, `c5r_bake_test` Release 2 of 2, ASan 2 of 2, 0 skipped, 0 ASan reports.
  `render/tools/bakecompare/test_bakecompare_4106.py` (18 cases: + the tap rule without --decals, on a .dtap without the
  block (exit 5), on 4111's .dtap) plain and ASan 0 failed; the ASan build on 200 Tiny tiles of 4111's recording with
  `--t8-rule tap --decal-bgra all`: exit 0, 0 ASan reports (`render/evidence/0166/4111/asan_run.sh`).

### 14.2 The tie order of equal priorities [L, C]

- **Done**: `c5r::decal::decalsForTile` (and the grid's cell lists) order equal priorities **small decals (both sides
  at most 256 samples, `kLargeDecal`) newest first (descending listOrder), then the large ones in listOrder**, then the
  index (`render/src/decal/decal_set.cpp`, `DrawLess`). Re-run on 4106's recordings (4106's best, measured with
  `--tie-order small-rev`, is now the default): **Tiny 2,543 / 2,568, huge 5,330 / 6,136** bit-equal, against 2,510 /
  4,781 with the list order (`--tie-order listorder`); per texture Tiny +34 / -1, huge +653 / -104 [L].
- **The EXE's exact order, not reproduced** (the time box); what the static reading gave [C, behaviour only, unless
  marked]:
  - **AddDecal 0x49B5A0** puts a decal whose sides are both at most 256 samples (0xA039B0) into the **spatial index at
    TDS+0x6C** as a **point, the centre of its rectangle** ((min + max) x 0.5, 0x9FCF90), with its batch and slot
    (allocated by 0x798A00, inserted by 0x69CD30); a larger decal is appended at the tail of the **large list**
    (TDS+0x94: count +0x14, head +0x18, tail +0x1C; 16-byte entries at +0x08, the draw walks them from TDS+0xAC through
    TDS+0x9C).
  - **The index** is a uniform grid of cells: origin at +0x00 / +0x04 (the world rectangle's min, 1.1), inverse cell size
    at +0x10 / +0x14, the cell heads at +0x18 (an item index per cell, 0x0FFFFFFF empty), columns at +0x1C [rows at +0x20:
    I], the item pool at +0x24 (24-byte items at its +0x08: +0x00 the next item shifted left one bit, +0x04 the previous,
    +0x08 / +0x0C the point, +0x10 batch, +0x14 slot). A point's cell is trunc((point - origin) x inverse size) per axis,
    clamped to the grid (0x53F0C0). **0x69CD30 inserts at the head of the cell's list: within a cell, newest first.**
  - **The draw 0x742EF0** queries the tile rectangle grown by 256 (0x6D7EC0 / 0x724CA0: cells trunc((min - origin) x
    inverse) .. trunc((max - origin) x inverse) + 1, exclusive, clamped) and walks it with 0x53A630 / 0x645E70: **rows
    outer, columns inner, each cell's list from its head**; an item is taken when its point lies in [query min, query
    max). It keeps the items whose record has +0x08 bit 0 set, the wanted pass bit, a batch whose +0x30 is 0 and a
    rectangle overlapping the tile (the 1.4 test), as 12-byte entries (batch, slot, priority) in the vector at TDS+0x4D0,
    appends the large list's decals with the same tests in list order, and sorts by priority with 0x648890 (the C++
    library's introsort: insertion sort below 33 entries, which keeps ties in input order; above, its partitions).
  - So the measured rule ("small newest first, then large in list order") is exact for ties whose decals sit in one
    cell and whose batch list order is their insertion order, with fewer than 33 decals on the tile; it misses ties
    across cells (the walk's row-major cell order decides), decals added into an earlier batch after a removal (list
    order is not age), the large list's own order, and tiles of more than 32 decals.
  - **To reproduce it exactly**: the tap publishes the index's origin, inverse cell size and grid size, per small decal
    its cell and its rank in the cell's list (a walk of the cell heads, byte patterns for 0x69CD30, 0x53F0C0, 0x645E70,
    0x742EF0's query), per large decal its rank in the large list; the server collects a tile's decals in the walk's
    order with the point test, appends the large ones, and sorts as the library introsort does (pivot choice and
    partition included, above 32 entries). ~~Estimated 2-3 h plus a fresh recording; not started (a follow-up for
    lead-0166).~~ *Done 2026-11-15 by ticket 0366 (section 18): as described here, with these corrections and additions
    [C]: the rows at +0x20 are confirmed (not [I]); the index object is TDS+0x6C itself and +0x24 points at its item pool
    (TDS+0x4DC), whose +0x08 holds the items; item +0x00 bit 0 marks a free item; the large list's decals skip the
    upload-pending test; the tile rectangle the bake passes is [o - 1, o + 65] per axis (64 in the last column), so the
    EXE selects fewer decals than the 4111 square; each pass is selected and sorted on its own; the sort's partition,
    median and heap path are written out in 18.3.*
- **What stays** [L, 4111's recordings, tap rule, the quarry probe of 14.3]: Tiny 13 t0 + 1 t8 textures, huge 601 t0 +
  135 t8 on tiles with overlapping equal priorities (huge: city ground and model grounds at priority 0, farm fields and
  tracks at 0.5 / 0.6, roads at 0.7, as 13.4).

### 14.3 The stone-quarry ground: the A8R8G8B8 height map [L]

- **The name is right**: the huge set's key is `Stone_Quarry_Decal_D.dds` / `Stone_Quarry_Decal_H.dds` /
  `CityBase_SREF.dds` (handles 1654 / 1655 / 624; 19 decals of 68.6 x 75.1 samples at priority 0), and the install
  (with VT and VP's folder) has one file of each name (`fpkscan lookup`; every .fpk of c5h-i listed: only
  `Resource/DX9/Upgrade1Textures.fpk`). The albedo is DXT5 128 x 128; **the height map is a legacy A8R8G8B8 256 x 256
  file** (32-bit masks R 0xFF0000, G 0xFF00, B 0xFF, A 0xFF000000), the only such height map in the sets (the others of
  that format: `Tree_Shadow_2.dds`, `blank.dds`, both grey). In its bytes the memory order is B, G, R, A: byte 0
  (blue) is 255 everywhere, byte 2 (red) holds the relief (77..191).
- **The cause**: the decal pass creates an A8R8G8B8 file as R8G8B8A8 with its bytes unchanged
  (`render/src/decal/decal_pass.cpp` `createFromDds`, from 11.3's capture of `tree_shadow_2.dds`), so the shader's red
  reads byte 0 (255, flat) and the quarry draws no relief, where the EXE's cache shows it. 11.3's capture could not tell
  "bytes unchanged" from "channels in their meaning": the tree shadow file is grey (R = B in every texel) and both give
  the same resource bytes. The quarry's file tells them apart: **the EXE samples it with its channels in their meaning**
  (gamefs's own B8G8R8A8_UNORM reading).
- **Proof** [L]: bakecompare `--decal-bgra all` (diagnostic: every A8R8G8B8 decal texture of the set handed to the pass
  as B8G8R8A8; the pass itself unchanged): 4106's huge recording 5,330 -> **5,367** (+37: 24 t0 + 13 t8, **0 made
  worse**); 4111's huge recording 5,342 -> **5,379** (+37, 0 worse); both Tiny recordings unchanged (no quarry; the grey
  files change nothing). On the 40 t0 / 40 t8 textures of tiles carrying the quarry (4106's huge): 0 / 21 bit-equal
  before, **24 / 34** after; the 22 left are tiles with other priority-0 grounds overlapping (ties, 14.2).
- **The fix is in `decal_pass.cpp`** (not this task's file): drop the A8R8G8B8 -> R8G8B8A8 reinterpretation (create such
  a file as gamefs gives it, B8G8R8A8), and correct 11.3's rule. Asked of lead-0166 in 4111's hand-back.
  *Done 2026-11-15 by 4113 (17.2): made, 11.3 corrected; on 4111's huge recording without `--decal-bgra` +37
  textures, 0 broke, identical to the probe above.*

### 14.4 Figures after 4111 (bit-equal textures, all mips) [L]

`render/evidence/0166/4111/cmp.sh`, `runs.sh`, `summary.py`, `delta.py`, `residue.py`, `quarry.py`; reports
`F:/c5-64bit-work/0166/4111/cmp/<src>-<save>/<run>/`; EXE `%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`
(b2ce96ae..., shaders read from it); Release clang-cl; installs c5h-i (4106's sessions) and c5h-f (4111's), both with VT.

| Recording, run | t0 | t8 | All | Remaining by cause (t0 + t8) |
|---|---|---|---|---|
| 4106 Tiny, list order (before) | 1,268 / 1,318 | 1,242 / 1,250 | 2,510 | |
| 4106 Tiny, decalsForTile now | 1,295 | 1,248 | **2,543 (0.990)** | ties 15 + 0, edge column 6 + 2, tree-shadow border 2 + 0 |
| 4106 huge, list order (before) | 2,067 / 3,085 | 2,714 / 3,051 | 4,781 | |
| 4106 huge, decalsForTile now | 2,439 | 2,891 | **5,330 (0.869)** | ties 592 + 133, quarry 40 + 19, stale 11 + 7, other 3 + 1 |
| 4106 huge, now + quarry fix (probe) | 2,463 | 2,904 | **5,367 (0.875)** | ties 608 + 139, stale 11 + 7, other 3 + 1 |
| 4111 Tiny, tap t8 | 1,299 / 1,320 | 1,251 / 1,254 | **2,550 (0.991)** | ties 13 + 1, edge column 6 + 2, tree-shadow border 2 + 0 |
| 4111 huge, tap t8 | 2,447 / 3,083 | 2,895 / 3,051 | **5,342 (0.871)** | ties 584 + 130, quarry 40 + 19, stale 11 + 7, other 1 |
| 4111 huge, tap t8 + quarry fix (probe) | 2,470 | 2,909 | **5,379 (0.877)** | ties 601 + 135, stale 11 + 7, other 1 |

Every remaining texture's cause: **ties** (14.2: tiles with overlapping equal priorities); **stale** (the EXE's cache
older than its tile's content, +0x1CC > +0x1D0, 13.4); **edge column** (Tiny x = 55 with t8: stage 1's wrap-column rule,
13.4); **tree-shadow border** (Tiny (11, 32), (15, 30): a pass-1 decal at the tile's border, 0.04-0.06 % of texels, 13.4,
open); **other**: huge (107, 29) t0, a decal reaching under 4 samples over the tile's edge that the EXE's cache lacks (13.4);
on 4106's huge also (39, 26) t0 + t8 (same kind, 13.4) and (71, 64) t0 (farm field and track without an overlapping tie,
0.02 % of texels, open). A tie's count rises slightly with the quarry fix (608 against 592) because quarry tiles that are
also tie tiles move from "quarry" to "ties" (first match wins).

### 14.5 Not done, open

- ~~The quarry fix in `decal_pass.cpp` and 11.3's rule (14.3): for the pass's owner.~~ Done by 4113 (17.2), 2026-11-15.
- ~~An exact product rule for areaChanged under D33 (14.1): the stock +0x300 set from the records is not determined by
  the final records (13.5); the tap's bits under D33 are not stock's. Options for the MC: keep 4105's rule (98-99 Tiny
  tiles with an extra t8, no t0 change, 13.5), or find what makes D33's EXE skip the invalidation and read the stock set
  some other way.~~ *Answered by 0367 (14.6), 2026-11-15: D33's EXE skips nothing; options (a1) switch on, (a2) the
  no-river records rule, (b) 4105's rule (130 / 110 extra t8 under D33, not 98-99); the MC decides.*
- ~~7.2's exact walk (14.2)~~ (done by 0366, section 18), the tree-shadow border and the (71, 64) farm texture (13.4).
- A wiring test of the switch (`decalAreaFromExe`) in `server/tests/render/test_decal_wiring.py` (not this task's file);
  the residency's use of the bit is covered by the reader's and the comparator's tests only.
- *Answered 2026-11-15 by 0367 (14.6): D33 skips no invalidation. The EXE marks every tile a decal added or removed after
  the TerrainRenderer exists covers; the river and flood-plain decals are laid before it exists and never mark; D33's
  smaller set is stock's Tree_Shadow decals, which D33 does not have (Forest off). Under D33 the tap's bit is stock's
  rule for the product's own decals; the product's rule today (4105's) marks 275 Tiny / 285 huge extra tiles there (130 /
  110 extra t8). The MC's choice between the bit (switch on) and a server rule from the records is in 14.6.*

### 14.6 The stock t8 rule and D33 (ticket 0367) [C, L]

Ticket 0367, 2026-11-15 12:43-13:20 EST (account B implementer, branch `task-0367-exact-t8-rule`). Scripts
`shell/evidence/0367/`; data `F:/c5-64bit-work/0367/` (never committed); decompiler output
`C:/c5-64bit-work/0367/out/` (D83, never committed). Behaviour only.

**Static pass [C]** (headless Ghidra 12.1.4, read-only, on a scratch copy of the `civ5` project, `gh.sh`; every
instruction with the operand 0x300 listed and the tile-object ones kept):

- **tile+0x300 has two writers**: 0x4387D0 (store 0x438911, = 1) and the tile initialiser **0x679AF0** (= 0, with +0x2FC
  = 0 and +0x1CC = +0x1D0 = 1), called only by the grid builder 0x5DF260 (one call per tile), called only by the
  TerrainRenderer's initialiser 0x678290. **Nothing clears it after a bake**: the bit stays 1 until the grid is built
  again. Its one reader is the tile's layer job 0x568430: +0x2FC = (byte 0xD90F28 set) and (+0x300 set or a layer whose
  texture set has a spec texture, TR8+0x808).
- 0x4387D0 marks every tile of its rectangle unconditionally. **Its four callers skip it while the TerrainRenderer pointer
  TS+0x75FA4 (0xD8C584) is 0**: AddDecal reads it at 0x49B90B, RemoveDecal at 0x68F20C; the terrain edits 0x678FF0 and
  0x6B5A50 test the same member through their own `this`.
- **The pointer is written in two places**: 0x5BCEF0 allocates the renderer (0x29D1C bytes, `TerrainSystem.cpp`),
  runs 0x678290 (so a fresh grid, every bit 0) and stores the pointer as its last step (0x5BD0CD); 0x6684D0 (the terrain
  teardown, from the world teardown 0x580C50) frees it and stores 0 (0x668534). 0x5BCEF0 is called once, at the very
  end of CreateTerrain's body 0x709B50 (from 0x60ABB0), after the terrain pieces and CreateTerrain's own farm-track pass
  (0x6E27D0).
- The callers' callers: AddDecal from 0x69F250 (tree cut), 0x5EC600 (route curves), 0x65A150 (wonders), 0x700030
  (landmarks), 0x5865B0 (farm fields, two sites) and the variant wrapper 0x60F180, which is called by 0x770EE0 (tree
  shadows), 0x559FF0 (rivers and flood plains, five sites; reached from 0x7857F0 through a task object, no direct
  reference) and 0x55D0C0 (farm tracks); RemoveDecal from 0x85E900, 0x6A09C0, 0x62C390, 0x51D5A0 (two sites), 0x44B690,
  0x49E660, 0x52A360, 0x66D090 and one site at 0x521BDC outside a function the analysis knows; 0x678FF0 from 0x61ECE0
  (from 0x4A3130); 0x6B5A50 from 0x6A74D0 (from 0x630B70 and three sites in 0x7620B0).
- Correction of 14.1's [I] ("under D33 that holds for few of the load's adds"): the pointer is set and cleared by the
  same code in both configurations; see the live part.

**Live [L]** (`run_0367.py`, `load_watch.py`: ReadProcessMemory only, no debugger: every 0.05 s on Tiny / 0.25 s on
huge the pointer, the renderer's frame counter `[0x3010570]+0x33F74` (the value 0x4387D0 stamps into +0x1CC), the decal
counter signal of 5.2 and, while the pointer is set, every tile's +0x300 and +0x1CC; a decal census whenever the signal
moved, at most every 0.2 s / 1 s; at the end `layer_probe` and `decal_probe`'s census). Instance c5h-f (Goldberg, F:,
VT present), the pinned shell test DLL `i2-shell-test-26234371` (eb8d26c0..., the DLL that applies D33's viewmask
marker; 4111's D33 run used it), CP/VP/EUI, no composite; copies `C5H-0367-TINY_0008` (b2ceb663..., = 4106's and 4111's)
and `C5H-0367-HUGE_0350` (c499d3c6...); stock = the instance's own config, D33 = `darkred.py config --arm d33`; files
restored by hash at the end. 2026-11-15:

| | Tiny stock (12:55-12:59) | Tiny D33 (12:59-13:03) | huge D33 (13:05-13:09) | huge stock (13:10-13:14) |
|---|---|---|---|---|
| pointer set (s after the watch's start; frame) | 137.2; 20 | 136.4; 20 | 141.5; 20 | 150.9; 20 |
| decals live then | **61 = every river and flood-plain decal** | 61, the same | **326 = every river and flood-plain decal** | 326, the same |
| bits at the first sample with the grid | 0 (every +0x1CC = 19) | 0 (19) | 0 (19) | 0 (19) |
| added after it / removed after it (census) | 471 / 51 (13 Tree_Shadow, 38 city ground) | 299 / 46 (city ground) | 17,410 / 115 (route curves) | 18,511 / 1,194 (592 Tree_Shadow, 329 city ground, 144 route curves, model grounds) |
| final bits | 455 | 55 | 2,719 | 3,020 |
| **"tiles of the decals added or removed after the pointer was set"** against the bits: both / EXE only / rule only | **455 / 0 / 0** | **55 / 0 / 0** | 2,700 / 19 / 0 | 3,017 / 3 / 0 |

The huge EXE-only tiles are, by inference, decals added and removed between two censuses (a huge census takes 1-2 s;
route curves are laid out again during the load) [I]; on Tiny, where a census takes under 0.25 s, nothing is left. The
pointer never went back to 0 in any run.

So [C, L]:

1. **Stock's rule**: tile+0x300 = 1 exactly on the tiles (square floor(min / 64) .. floor(max / 64)) of the decals added
   or removed after the TerrainRenderer is created; created once per load, at the end of CreateTerrain.
2. **Why the river decals leave it 0 (13.5)**: CreateTerrain lays every river and flood-plain decal before it creates the
   renderer, so those adds never reach 0x4387D0; a river tile carries the bit only when a later decal covers it (Tiny
   stock: 201 of 298 Riverbank tiles, nearly all through tree shadows; D33: 24 of 298).
3. **Why D33 has 55-61 bits on Tiny against 455-460 (14.1)**: **D33 runs every invalidation stock runs.** Stock's extra
   tiles are the Tree_Shadow decals (Forest off in D33, 2.3): 167 shadows cover 412 of stock1's 455 tiles; 13 shadows
   and 38 city-ground decals removed during the load add the rest. On the huge save the gap is 3,020 against 2,719 (100
   final shadows, 592 removed). 4111's "generation 1 (61 decals) carried 0 bits" is the river set before the renderer.

**What the t8 decision loses or gains** (all tiles; the t8 is decided by +0x2FC, so a bit matters only where the list
rule says no). `rule_check.py`: per rule, tiles marked, against the EXE's bits; "extra t8" = rule-only tiles whose
+0x2FC is 0 (the EXE bakes no t8 there, the server would). 4105's rule = every decal, the query square of 10.2 (the
product today); "no river" = every decal but the categories 4-9, the plain square:

| Recording (final records) | EXE bits | 4105's rule: EXE only / rule only (extra t8) | no river: EXE only / rule only |
|---|---|---|---|
| 4106 Tiny stock | 457 | 0 / 99 (29) | 7 / 0 |
| 4111 Tiny stock | 460 | 0 / 98 (29) | 7 / 0 |
| 0367 Tiny stock | 455 | 0 / 99 (29) | 8 / 0 |
| **0367 Tiny D33** | 55 | 0 / **275 (130)** | **0 / 0** |
| 4106 huge stock | 3,015 | 57 / 99 (29) | 147 / 0 |
| 4111 huge stock | 3,021 | 61 / 96 (28) | 153 / 0 |
| 0367 huge stock | 3,020 | 60 / 95 (28) | 148 / 0 |
| **0367 huge D33** | 2,719 | 5 / **285 (110)** | 23 / 0 |

- The no-river rule's EXE-only tiles on stock recordings are tiles of decals removed during the load (stock huge: 145
  such tiles by the watch, 1,194 removed decals; Tiny: 8): the final records cannot show them. On the D33 records it is
  exact on Tiny and 23 short on huge (removed route curves).
- **4105's rule on the product's records (D33) is worse than 4111 reported**: 275 / 285 extra marked tiles, 130 / 110
  extra t8 (4111's 98-99 / 27 were measured on stock records, where tree shadows already mark most river tiles). Every
  extra tile is a river or flood-plain tile.
- **What no rule from D33 records can give**: the t8 that stock bakes only because of a tree shadow's bit
  (`tree_t8.py`: tiles with the bit in stock, not in D33, and +0x2FC 0 under D33): **Tiny 165, huge 104**. That is the
  trees' part of MC answer 7 (intended, with trees).
- 0x678FF0 and 0x6B5A50 (the terrain editor's material brush and the marsh stamp; corrected 2026-11-18 by 0465, was "map-script and terrain edits") mark tiles too; no load of these saves ran them (no bit
  without a decal). A server rule from decals would miss them; the EXE's bit has them.

**Options for the MC** (the code is in `server/src/render/`, held by 0333; asked on 0367's thread):

- **(a1) the EXE's bit in the product**: `ResidencySourceConfig::decalAreaFromExe` on (`srv_residency_source.h:85`;
  the bake's use at `srv_residency_source.cpp:50-69`, the raise at 736-745; built and tested by 4111, 14.1). Under D33
  the bit is stock's own rule applied to the product's decals: exact by construction (it is the EXE's own bit; 4111
  showed the tap's copy equal to the EXE's on every tile, stock Tiny and huge and D33 Tiny), with removed decals and
  terrain edits included. Without a TerrainRenderer (never seen after the load) the tap publishes no block and the
  bake falls back to the rule in force.
- **(a2) a server rule**: areaChanged = the tile is covered (plain square) by a decal of any generation since the load
  that is not a river or flood-plain decal (albedo `riverbank_D*` / `floodplains_D*`), kept once set, including the
  removed rectangles of each `DecalChange`. Exact on the D33 Tiny records, 23 of 2,719 short on huge (decals removed
  between two generations), 0 extra anywhere; misses terrain edits.
- (b) keeping 4105's rule costs 130 / 110 extra t8 tiles under D33 (bake time and memory of one t8 each, and the spec
  draw of 0189 on those river tiles); not measured as pixels here.

Recommendation: **(a1)**: one default changes, nothing new is computed, and it is the only option with 0 / 0 on both
D33 saves. **MC decision 2026-11-15 (0367 thread, entry #1808): (a1).** The flip goes to lead-0169 (it holds
`srv_residency_source.*` now, with 4305): `srv_residency_source.h:85` `decalAreaFromExe = false` -> `true`, and the
comments that call it off by default (h:32-37, cpp:50-51, 69). Tests: the existing ones of 4111 (14.1: the reader's
case `area`, `test_decal_tap.py`, bakecompare `--t8-rule tap`); `test_decal_wiring.py`'s images carry no area block, so
its cases keep 4105's fallback and should pass unchanged; the missing wiring test of 14.5 becomes the switch's test: an
image with the block where a decal tile has bit 0 and a decal-free tile bit 1 bakes areaChanged 0 and 1, and a
bits-only generation re-bakes its tile (`decal_area_raises`).

**The tree shadows are a picture difference** [L for the records, C/L 4.4 for the draw]: stock bakes the Tree_Shadow
decals into each forest and jungle tile's macro-shadow target (dark blobs in the colour cache, 4.4) and owes 165 Tiny /
104 huge t8 decisions to their bits alone; under D33 neither the EXE nor the server draws them, and no rule from D33's
records can give them. Today this is MC answer 7's named intended difference ("with trees"). It must come back with the
trees: whoever draws trees in the server (or turns Forest on) also needs the shadow decals (pass 1, priority 0.1,
`Tree_Shadow_2.dds`, one quad per tree from the tree placement) and their +0x300 marks. Named follow-up, no id yet. *Taken by epic 0431 (ticket 4552, 2026-11-16, `trees.md` 12).* Not verified: a joined product session with the switch on (the server's t8 set against a D33 EXE's bits in
the same session); the pixel effect of the 130 / 110 tiles under (b).

## 15. Parity, regression, failure route and suites (4112)

Task 4112, 2026-11-15 05:19- EST, branch `task-4112-decal-parity-regression-failure` (from `epic-0166-decals` 61cb6bf6,
main 86838c52 merged). The epic's done D, E, F and G. Drivers in `shell/evidence/0166/4112/`; data on
`F:/c5-64bit-work/0166/4112/` (D141; not committed); builds of the suites on `C:/c5-64bit-work/0166/4112/<pass>` (removed
at the hand-back). Marks as above.

### 15.0 Method (written before the results)

- **D, the parity pair** (0178's method, `proxy-design.md` 24.5): `live_4112.py parity` on a Goldberg instance on the
  product path of 12.6 (run_3308's prep: `ServerRenderer = 1`, the composite beside the proxy with its manifest, D33 by
  `darkred.py` minus Overlay, ring + fence, lockstep, depth; the composite's shipped ini; WhiteFogMask at its default 1,
  D136), save `C5H-0166-4112-TINY` (a copy of `C5H-0166-TINY_SETUP`, 8ce90063..., turn 18), the same build in every arm.
  Arms: **on** (`decal_tap=1`, `render_decals` at its default 1), **rd0** (`render_decals=0` through vpproxy.ini
  `server_cfg_extra=`, TEST ONLY, 0307), **tap0** (the composite's `decal_tap=0`). Each: `eq_stats.py --dump` after the
  load, then the player's workers get a road and a farm the way the UI gives them (`UI.SelectUnit` +
  `Game.SelectionListGameNetMessage` PUSH_MISSION `MISSION_MOVE_TO`, then `MISSION_BUILD` appended; units and plots chosen
  by a fixed rule from the game state: the lowest-id worker and the nearest own plot without route or improvement where
  it can build a road; the second worker (or the same, appended) and the nearest other such plot for a farm), 30 AI
  turns (`turns.py end` / `wait --answer 8`), the plots read back, `eq_stats.py --dump` at the last turn; `eq_stats.py`
  on against rd0 and on against tap0 at both points. Expected: identical, 0 differing keys (the decals are drawn by the
  server and read by the composite; neither writes game state).
- **E, the regression run**: `live_4112.py turns` = run_3308's run (30 AI turns, memory after the load and per turn,
  the composite's lockstep slices per turn) on `C5H-0166-HUGE_0350` (a copy of `C5H-VP8P-HUGE_0350`, c499d3c6...),
  decals on against decals off (`decal_tap=0` and `render_decals=0`: no tap and no reader), then `esum`: LOUD lines by
  kind, gray frames after the bake-ahead (render.log's `gray_after_ahead`, 0139), the per-generation re-bake latency
  against D92's 0.6 s (the stale-tile bound: a generation whose land tiles were re-baked later than that), re-bakes per
  turn, lockstep, memory, the white mask's lines of vpproxy.log, crash lines. `quietlock.py check --quiet` before each
  arm (recorded in the arm's json); costs are indications (COMMON 23).
- **F, the failure route**: `neg_decal_job.py --fault decal` over `tools/testbed/jobs/release_d125.py` unchanged (0167's
  and 0168's pattern) on oldpc's testbed, the shipped pack layout (pack by `tools/release/pack.py`, `pack=1`, config.ini
  in the A'' form by `vp64helper on`): the composite's test key `decal_test=garbage` (the tap's synthetic decal system
  with its batch vector in reserved, inaccessible memory; no EXE byte) makes the tap's first read fault (FAILED SEH), an
  install failure (12.4). Expected: the server's LOUD `decals cannot install: the decal tap is FAILED SEH`, exit code 5,
  the report folder naming it, `vp64helper failed` restoring config.ini (SHA-256 = the player's), `failed=1`, the popup,
  the EXE ended by the proxy. OFF (`decal_tap=0`) is not fatal: shown by D's **tap0** arm (30 turns played, one LOUD
  line, the game goes on).
- **G, the suites**: `suites.sh <pass>`: every suite of the epic built and run plain and once under MSVC
  `/fsanitize=address` through `quietlock.py run` (render `c5r_decal_test`, `c5r_bakepost_test`, `c5r_bake_test`,
  `c5r_bakeresidency_test`; server `server/tests/render/decal/`, `test_decal_wiring.py`, `test_residency_source.py`,
  `test_render_host.py`, the scene tests of the overlay, the fog and the water; shell `test_decal_tap.py`,
  `test_bake_tap.py`, `test_water_tap.py`, `test_overlay_tap.py`); a skip counts as a failure (COMMON 9). Pass 1 on the
  team branch as 4112 found it, pass 2 after 4111's merge.

### 15.1 F: the install-failure route on the shipped pack layout [L, 2026-11-15 05:41-05:42]

oldpc testbed job `20261115-053613-048b` (instance tb2, headed), `shell/evidence/0166/4112/neg_decal_job.py --fault decal`
submitted by `submit_d125.sh`; pack `4112-d125` by `tools/release/pack.py` (x86 = Release-5.4.8's committed DLL
262aee88..., x64 74588135..., purity 3721cafb...), `pack=1`, config.ini in the A'' form by `vp64helper on`; server.exe
15049e9d..., proxy 8740335f..., vp64helper 81a93870... (`build_server.sh s1`, this branch at 69549546), composite
21bf9161... (`shell/build.cmd`); save `TB-TINY_0008` (b2ceb663..., a copy). Results `F:/c5-64bit-work/0166/4112/d125`.

- The composite: `decal_tap: TEST MODE decal_test=garbage: a synthetic decal system in this DLL, the EXE is not read`
  at its first Present, then at the first map frame `decal_tap: LOUD: FAILED SEH: fault reading 2 batches at 0x0A380000 -
  DISABLED for the life of the process; the last good image stays`.
- The server, +34.70 s: `render: LOUD: decals cannot install: the decal tap is FAILED SEH (an access fault while reading
  the EXE: the tap is off for the life of the EXE); roads, railroads, farm fields and river beds cannot be drawn
  (decision 6 of epic 0166, D118, D125)`; +34.71 s `decals: the 64-bit path cannot go on: ... the server ends (exit code
  5)` (`DecalFatalStep` -> `HostConfig::fatal(5)`), +34.74 s `remote: exit code 5 (...)`.
- The proxy wrote one report folder (no `.dmp`); **report.txt names the reason**: `server pid 15604: exit code 5` and
  `server.log, last FAILED or LOUD line: [ 34.736] remote: exit code 5 (decals cannot install: the decal tap is FAILED
  SEH ...)`; `server.log.tail.txt` names it too. The EXE was ended by the proxy 87.6 s after the launch, no "Game
  Crash" box. `vp64helper failed` restored config.ini (`restored Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons,
  Particle, Combat, Projectile`; SHA-256 7b53b17c... = the player's before the run and at the end), wrote `failed=1`,
  left no side file, and showed the popup (Visual Checks `vc-20261115-054204-oldpc-3838`, DIAGNOSTIC: the fault is a
  test key). The popup's own text says `server-death: PeerDied` and the report's path; the reason is in the report.
- **15 of 15 checks pass** (release_d125.py's ten and five of the decals: the composite's FAILED SEH, the server's
  LOUD line, exit code 5 in server.log and in report.txt, the report folder naming the reason). The instance restored
  by hash (`backup_restore_ok`, provision verify rc 0); job exit 0.
- OFF is not fatal: D's tap-off arm (15.3).

### 15.2 G: the suites, pass 1 (the team branch before 4111) [C, 2026-11-15 05:24-06:38]

`suites.sh pass1` at 69549546 (`epic-0166-decals` 61cb6bf6 + main 86838c52), workstation with 8-9 heavy jobs of other
teams running (quietlock check, 05:37). Counts as the runner printed them; 0 skipped in every suite; **0 ASan report
files** (shell, render, decal, scene folders).

| Suite | Plain | MSVC `/fsanitize=address` |
|---|---|---|
| render `c5r_decal_test`, `c5r_bakepost_test`, `c5r_bake_test`, `c5r_bakeresidency_test` | 4/4 ctest, exit 0 | 4/4, exit 0, 0 reports |
| shell `test_decal_tap.py` + `test_bake_tap.py` + `test_water_tap.py` + `test_overlay_tap.py` | 114 passed, exit 0 | 113 passed, **1 failed** (exit 1): `test_overlay_tap.py::test_late_query_is_counted_stale` |
| server `server/tests/render/decal/` (`C5_DECAL_BUILD`) | 10 passed | 10 passed |
| server `test_decal_wiring.py` + `test_residency_source.py` + `test_render_host.py` | 91 passed, **1 failed** (exit 1): `test_render_host.py::test_device_lost_rebuilt_in_process` | 90 passed, **2 failed** (exit 1): `test_residency_source.py::test_idle_pacing_off_keeps_the_old_poll`, `::test_pacing_stays_on_events_when_presents_stop_too` |
| overlay scene `test_scene_overlay.py` | 3 passed | 8 of 8 cases PASS (the binary) |
| fow scene `test_fow_scene.py` | 3 passed | 3 passed |
| water scene `test_water_scene.py` | 19 passed | 17 of 17 cases PASS (`water_scene_asan.sh`) |

The four failures are timing assertions on the loaded machine, in tests outside 0166's code, each rerun alone
(`rerun_one.sh`, `rerun_host.sh`, 06:24-06:50):
- `test_late_query_is_counted_stale` (0194's latequery window: `accepted 2` against 1, then `accepted 113, stale 0`):
  ASan reruns 2 of 3 pass, plain reruns 3 of 3 pass.
- `test_device_lost_rebuilt_in_process` (3 draws in 8 s, the forced loss at draw 6 never reached): plain reruns 3 of 3
  pass.
- `test_pacing_stays_on_events_when_presents_stop_too`: ASan reruns 3 of 3 pass.
- `test_idle_pacing_off_keeps_the_old_poll` (3308's: more than 500 of the 0.5 ms legacy polls in 2 s): ASan reruns
  **0 of 3 pass** (251-372 polls); plain passes. A count of polls per second under ASan on a machine with 8-9 heavy
  jobs; 4105 saw it fail once and pass on a second try (12.5). Not a decal fault; it fails done G's "0 failed" as run.

### 15.3 D: the parity pair [L, 2026-11-15]

Instance c5h-g (Goldberg, F:, VT installed, D94), the product path of 15.0; save `C5H-0166-4112-TINY` (8ce90063...,
turn 18); 30 AI turns, 18 -> 48; machine with 8-9 heavy jobs of other teams (quietlock not quiet; no timing is claimed).
Driver `live_4112.py parity --orders --grant-wheel`, chain `parity_chain.sh`, comparison `eq_pair.sh`; results
`F:/c5-64bit-work/0166/4112/tiny/<arm>/` (`arm.json`, `stats-load.txt`, `stats-end.txt`, logs) and
`.../tiny/eq-<a>-vs-<b>-<load|end>.txt`.

**The player's road and farm.** The save's player has no worker and lacks The Wheel; how the driver gets the two builds
[L, dry runs 1-11, 05:37-06:52]:
- a worker is bought in the capital with gold (`Game.CityPurchaseUnit`, the city screen's call) and a second one put in
  production; end-of-turn blocks the player must answer get fixed choices (a city without production: the first of
  Worker, Warrior, Scout it can train; research without a target: the lowest-id tech);
- The Wheel: its research, chosen by the tech tree's call at turn 18, had not finished by turn 47; **granted by a Lua
  setter** (`Teams[t]:SetHasTech`, a save copy, diagnostic, the same in every arm). Granted during the player's turn it
  left that turn's end blocked (ENDTURN_BLOCKING_PRODUCTION on a notification of the save, every city with a queue;
  `UI.RemoveNotification` / `DismissNotification` did not clear it) [L]; granted while the AI plays turn 18 (after the
  player's end of turn) it blocks nothing. It is granted after the load dump, so the load dumps hold the save's state;
- **the harness's end of turn zeroes every unit's moves before it forces the end** (`tools/harness/turns.py`
  `END_TURN_CHUNK`: `if u:CanMove() then u:SetMoves(0) end`) [C, L]: a mission queued for a worker never ran in the
  first arms (pre-on, pre-rd0: both workers still in the capital at turn 48 with full moves). The driver therefore
  re-issues each job at the start of every player turn (`UI.SelectUnit` + `Game.SelectionListGameNetMessage`
  PUSH_MISSION `MISSION_MOVE_TO`, then `MISSION_BUILD`, no modifier keys) and reads the unit back 2 s later. A note for
  every driver that wants player missions over AI turns with this harness.
- Result in every final arm: farm on (9,23) ordered at turn 18 and done at turn 27 (the worker moved at turn 19 and added 90 build progress a turn from turn 20);
  road on (10,22) chosen at turn 28 (the same worker, once the farm was done) and done at turn 33. With decals on, the
  farm's generation re-baked its 4 land tiles 204 ms after the publication and the road's its 7 tiles 83 ms after.

| Arms (build) | Keys after the load | Keys at turn 48 | Differing | Notes |
|---|---|---|---|---|
| fin-on against fin-rd0 (4111 merged: server.exe af7d2e26..., composite be9a5989...) | 378 = 378 | 465 = 465 | **0, 0** | on: 23 generations, 0 LOUD, gray after the bake-ahead 0; rd0: no reader, the proxy's two `TEST ONLY` lines of `server_cfg_extra` |
| fin-on against fin-tap0 (the same) | 378 = 378 | 465 = 465 | **0, 0** | tap0: `LOUD: decals: the tap is OFF (the composite's decal_tap=0, a test switch): no decals are drawn` once in render.log, 30 turns played, stop rc 0: **OFF is not fatal** (F) |
| pre-on against pre-rd0 (before 4111: server.exe 15049e9d..., composite 21bf9161...; missions never ran, above) | 378 = 378 | 465 = 465 | 0, 0 | on: 19 generations, 0 LOUD |
| fin-on against pre-tap0 (the build before 4111 with the tap off, the final driver) | 378 = 378 | 465 = 465 | 0, 0 | across builds |

**Done D holds**: `eq_stats.py` keys after the load and at the last turn identical, 0 differing, `render_decals=1`
against `0` and `decal_tap` on against off, the same build, with the player's worker building a road and a farm.
Arms 07:16-07:39 (fin), 06:50-07:15 (pre); every arm rc 0, the harness's stop rc 0.

### 15.4 E: 30 AI turns on the huge save, decals on against off [L, 2026-11-15]

Instance c5h-e (Goldberg, F:, VT installed), the product path of 15.0, save `C5H-0166-HUGE_0350` (c499d3c6...), turns
350 -> 380 (30 AI turns), driver `live_4112.py turns` (run_3308's run: memory after the load and per turn, the
composite's lockstep per turn; `esum` and `e_table.py` for the figures, `gen_timeline.py` for the per-generation
latency); results `F:/c5-64bit-work/0166/4112/huge/<arm>/` (`out/run.json`, `esum.json`, the logs) and
`.../huge/e_table_*.txt`. **Load**: every arm ran while 5-6 game instances of other teams ran (quietlock check before
each arm, in its `arm.log`: NOT QUIET, 5-6 heavy jobs) and, for the 4111 arms, G pass 2 ran on the same machine:
**every time, frame-rate, lockstep and GPU figure below is an indication (COMMON 23)**; a long run over 30 min names
the load instead of taking the lock.

| Figure (30 AI turns) | fin-on (4111 build, 07:19-07:42) | fin-off (4111 build, 07:43-08:04) | pre-on (before 4111, 06:52-07:18) |
|---|---|---|---|
| rc, turns, the harness's stop | 0, 30, 0 | 0, 30, 0 | 0, 30, 0 |
| EXE crashes / server crashes (crash lines of every log) | **0 / 0** | 0 / 0 | 0 / 0 |
| LOUD lines (render, server, composite, proxy) | **0, 0, 0, 0** | 0, 0, 0, 2 (`TEST ONLY` of `server_cfg_extra`) | 0, 0, 0, 0 |
| Gray frames after the bake-ahead (0139) / before it | **0** / 48 | 0 / 13 | 0 / 31 |
| Bake-ahead: time after the field, bakes, baker GPU ms | 76.1 s, 5,293, 16,776 | 19.9 s, 4,922, 1,268 | 22.0 s, 5,010, 1,461 |
| Decal generations / sets refused / decal bakes failed | 238 / 0 / 0 | 0 (no tap, no reader) | 225 / 0 / 0 |
| Re-bakes per turn after the bake-ahead, mean / median / max | 463 / 443 / 855 | 380 / 370 / 776 | 488 / 476 / 988 |
| Decal raises per turn (mean), decal changes in 30 turns | 102, 228 | 0, 0 | 109, 221 |
| Generations re-baked later than D92's 0.6 s after publication, in play (of all in play); max | 108 of 227; 5.36 s | - | 27 of 222; 4.66 s |
| Lockstep exact / older / giveups | 11,195 / 84,988 / 1,851 | 92,890 / 5,059 / 23 | 82,440 / 6,680 / 187 |
| Server terrain GPU ms per drawn second, mean / max | 4.39 / 10.0 | 0.20 / 0.9 | 0.25 / 2.8 |
| Turn wall s, median / max | 35.4 / 51.2 | 33.3 / 47.6 | 43.2 / 53.6 |
| EXE private / largest free block after the load (MB) | 1,440.5 / 1,823.9 | 1,422.4 / 1,822.5 | 1,421.7 / 1,893.0 |
| EXE private / largest free block after the last turn (MB) | 1,562.7 / 1,676.1 | 1,553.7 / 1,694.9 | 1,563.9 / 1,655.3 |
| Server private after the load / after the last turn (MB) | 1,401.4 / 1,934.6 | 1,703.4 / 1,792.1 | 1,751.1 / 1,845.1 |
| Server video (render.log) after the load / after the last turn (MB) | 738.6 / 785.1 | 1,088.5 / 677.3 | 951.4 / 809.2 |
| The white mask's vpproxy.log lines (numbers as N) | 5 lines | the same 5 | the same 5 |

"After the load" is run_3308's sample 20 s after in game; on huge the bake-ahead was still running then in fin-on (76 s),
so its server figures after the load are not comparable; the ones after the last turn are.

**fin-on was a shared GPU, not the 4111 build** [L]: fin-on2 (the same build, save, instance and driver, 08:05-08:27,
right after fin-off; GPU utilisation sampled every 5 s in `huge/gpu_util_ab.csv`): rc 0, 30 turns, 0 crashes, 0 LOUD,
gray after the bake-ahead 0 (17 before), bake-ahead 23.0 s / 4,964 bakes / 1,636 ms of baker GPU time, terrain GPU
0.21 ms (max 0.8), **lockstep exact 88,761 / older 9,217 / giveups 223** (fin-off 92,890 / 5,059 / 23), turn wall median
31.7 s (off 33.3), re-bakes per turn 490 mean (off 380: the decal raises, 110 a turn), 241 generations, 0 refused, 0
failed bakes, **7 of 237 in-play generations re-baked later than 0.6 s (max 1.39 s), median 149 ms after the
publication**; EXE private / largest free block after the load 1,420.0 / 1,894.5 MB, after the last turn 1,586.4 /
1,650.7 MB; server private after the load / end 1,745.9 / 1,831.6 MB; video after the load / end 1,157.9 / 785.2 MB;
the white mask's lines the same 5. In fin-on every GPU figure was ten times its value in the other three arms (the
baker's 3.2 ms per bake against 0.3, the terrain draw's 4.4 ms against 0.2, which no 0166 code touches): the GPU was
shared with other processes during 07:19-07:42 (other teams' game instances and G pass 2's ASan render tests
07:20-07:26). **pre-on2** (the build before 4111 again, the same instance, 08:27-08:48): rc 0, 30 turns, 0 crashes, 0
LOUD, gray after the bake-ahead 0, bake-ahead 22.2 s / 1,714 ms baker GPU, terrain GPU 0.20 ms, lockstep 89,876 / 7,808
/ 188, 232 generations, **6 of 228 in play later than 0.6 s (max 1.02 s), median 152 ms**, EXE end 1,550.3 / 1,705.1 MB,
server end 1,872.7 MB, video end 811.2 MB: **the 4111 build and the one before it are the same within run-to-run
spread** (fin-on2: 7 of 237, max 1.39 s, median 149 ms).

**Reading** (fin-on2 against fin-off, the like-for-like pair; indications):
- **Nothing regressed in the counts done E names**: 0 EXE crashes, 0 server crashes, 0 LOUD lines (the decals have no
  D132 episode of their own; off's two lines are the test switch announcing itself), 0 gray frames after the
  bake-ahead, the white mask's lines unchanged, 0 decal bakes failed, 0 sets refused.
- **Lockstep**: exact 90.6 % with decals against 94.8 % without (older 9.4 % against 5.2 %), giveups 223 against 23 in 30
  turns; turn times equal within noise. A cost of the decals or run-to-run spread on a shared machine; not separated.
- **Memory** [indications]: the EXE with the tap +0 to +33 MB private (after the load 1,420.0 against 1,422.4; at the
  end 1,586.4 against 1,553.7) and its largest free block -44 MB at the end (1,650.7 against 1,694.9), near the
  launch-to-launch spread (9.4: about 15 MB; the three on arms here: 20 MB after the load, 24 MB at the end); the server +40 MB private at the end (1,831.6 against
  1,792.1; the decal textures, the per-tile lists, the copy of the set), video +108 MB at the end (785.2 against 677.3).
- **Re-bakes**: 490 a turn against 380 (+110 = the decal raises); a generation's land tiles are re-baked a median 149 ms
  after its publication; 7 of 237 in play exceeded D92's 0.6 s (the raise -> re-baked part, 669 ms median of those: the
  residency's rate and budget behind other re-bakes); "0 stale-tile frames longer than the re-bake bound" is **not met
  as a strict count** on the shared machine (7 generations, at most 1.39 s; fin-on under GPU sharing: 108, 5.36 s).
  render.log's `tot_stale` (stale presents) is 0 in every arm.
- **Two findings for the lead** [L]: (1) **at the load on huge the first two or three generations come after the field**
  (on Tiny before it, 12.3): their 2,500-3,000 raised tiles are re-baked only after the bake-ahead, 18-23 s after their
  publication in the quiet arms (64.7 s in fin-on), so tiles the bake-ahead baked before the generation arrived (about
  the first second, 400 tiles at the rate of 400 a second) show without their decals until then; each such tile is
  baked twice. (2) **every generation's apply takes 50-66 ms of the render thread** (`decals: generation N applied: 18,000
  decals, ... 57-62 ms`; median 57 ms in play, also for a generation of 2 changed rectangles): the whole image is
  copied and indexed again; on huge that is one stall of about 60 ms per generation, 8 a turn [L for the times, I for
  the cause].
  *Corrected 2026-11-15 by 0365 (19.1), from these arms' render.log: of the 2,500-3,000 raised tiles almost none were
  baked twice: a raised tile without a bake gets its first bake with the set in force. The tiles baked twice before the
  bake-ahead ended are its bakes minus the land tiles: 42 (fin-on2), 88 (pre-on), 97 (pre-on2), 46 (dry-on), 371 (fin-on,
  GPU shared); the 18-23 s are the time to the generation's last land tile, mostly first bakes in ring order. The re-bakes
  of tiles that had a bake came first anyway (a raise restarts the ring order, whose first rings are the tiles baked
  first). Replaced: "each such tile is baked twice" for all 2,500-3,000.*

### 15.5 G: the suites, pass 2 (after 4111's merge) [C, 2026-11-15 07:05-08:24]

`suites.sh pass2` at 4f8ac048 (this branch with `task-4111-exact-t8-ties-quarry` 5a5d80e3 merged), the same loaded
machine plus my own E arms. 0 skipped in every suite; **0 ASan report files** in all four folders.

| Suite | Plain | MSVC `/fsanitize=address` |
|---|---|---|
| render `c5r_decal_test`, `c5r_bakepost_test`, `c5r_bake_test`, `c5r_bakeresidency_test` | 4/4, exit 0 | 3/4, exit 1: `c5r_bakeresidency_test` `rebake_same_tiles_10000_times_memory_flat` (settled private +23.4 MB against +22.6; from round 3 +2.0 against +6.0); **reruns 2 of 2 pass** (`rerun_render.sh`), 0 reports |
| shell, the four tap suites (118 tests: 4111 added 4) | 117 passed, 1 failed (exit 1): `test_overlay_tap.py::test_late_query_is_counted_stale`; reruns 2 of 3 pass | 116 passed, 2 failed (exit 1): `test_overlay_tap.py::test_leader_screen_frames_are_not_stale`, `::test_late_query_is_counted_stale`; reruns: leader 2 of 3, late query 3 of 3 pass |
| server `server/tests/render/decal/` | 11 passed | 11 passed |
| server decal wiring + residency source + render host | **92 passed**, exit 0 | 87 passed, 5 failed (exit 1); reruns (2): `test_feed_changes_the_bake_ignores[fow-2]` and `test_three_lost_devices_fail_the_renderer_not_the_process` pass both; `test_idle_pacing_off_keeps_the_old_poll`, `test_idle_pacing_before_a_camera_then_legacy_poll`, `test_pacing_stays_on_events_when_presents_stop_too` fail again |
| overlay scene | 3 passed | 8 of 8 cases PASS |
| fow scene | 3 passed | 3 passed |
| water scene | 19 passed | 17 of 17 cases PASS |

**Every decal test passed in both variants in both passes**; every failure is a timing assertion of another epic's
test on the loaded machine (0194's overlay windows, 3308's pacing counts, 0137's memory bound). The pacing counts are
not the decals' [L, 08:28, a temporary probe file, not committed]: `test_idle_pacing_off_keeps_the_old_poll`'s legacy
poll count with the decals on against `render_decals=0`, alternating, plain 1,271 / 1,299 / 1,369 / 1,289 and ASan 149 /
214 / 206 / 280 (the test wants more than 500): plain passes either way, ASan fails either way on this machine. **Done G
is not met as "0 failed" on this machine**: the open timing failures go to their owners (0194, 3308) or to a quiet
machine (the oldpc testbed); none is in 0166's code.
(2026-11-15, ticket 0333: the three 3308 / 3312 pacing counts named here are now counted instead of timed and passed 20
of 20 rounds under ASan and load; `renderer-in-server-verification.md` section 13.)

### 15.6 Summary: done D, E, F, G (4112)

| Done | Result | Evidence |
|---|---|---|
| D, parity pair | **Holds**: on against `render_decals=0` and on against `decal_tap=0`, 30 AI turns on Tiny with the player's worker building a farm and a road: 378 keys after the load, 465 at turn 48, **0 differing** in both pairs (and across builds) | 15.3; `F:/c5-64bit-work/0166/4112/tiny/eq-fin-on-vs-fin-rd0-*.txt`, `eq-fin-on-vs-fin-tap0-*.txt`; every arm rc 0 |
| E, nothing regressed | **Holds for the counts**: 0 EXE / 0 server crashes, 0 LOUD, 0 gray frames after the bake-ahead, white mask lines unchanged, 0 failed decal bakes, 0 sets refused, in all four on arms. **Not strictly**: 7 of 237 in-play generations re-baked later than 0.6 s (max 1.39 s) in the quiet pair; the load's first generations wait for the bake-ahead (18-23 s); lockstep exact 90.6 % against 94.8 % off; memory: EXE +0-33 MB, server +40 MB, video +108 MB at the end (indications on a shared machine) | 15.4; `F:/c5-64bit-work/0166/4112/huge/<arm>/esum.json`, `e_table_2.txt` |
| F, failure route | **Holds**: install failure (FAILED SEH by `decal_test=garbage`) on the shipped pack layout on oldpc: LOUD, exit code 5, report naming the reason, popup, the EXE ended, config.ini restored by hash; 15 of 15 checks; OFF (`decal_tap=0`) not fatal (D's tap0 arm) | 15.1; job `20261115-053613-048b`, `F:/c5-64bit-work/0166/4112/d125`; `vc-20261115-054204-oldpc-3838` |
| G, suites | Every decal suite passes plain and under ASan in both passes, 0 ASan reports, 0 skipped; **not "0 failed" overall**: timing tests of 0194, 3308 and 0137 fail on the loaded machine (named in 15.2 and 15.5, each rerun) | 15.2, 15.5; `F:/c5-64bit-work/0166/4112/suites/pass{1,2}/` (summary.txt and every suite's log; the builds on C: were removed) |

## 16. Live acceptance at frame level (4107)

Task 4107 (worker 4107 of lead-0166), 2026-11-15 from 05:19 EST, branch `task-4107-decal-acceptance` (from
`epic-0166-decals` 61cb6bf6, `main` 86838c52 merged at 0f0f3ea2). The brief's done C (frame level against stock, the
ACCEPTANCE pictures, stock's own two-session ceiling), the comparison-file corrections of done H from 4106's list, done
I (the bundle check named as a follow-up) and the epic's evidence summary (16.7). Marks: [L] = measured in the live
sessions below; [C] = in the drivers and asserted by their exit codes.

### 16.1 Method [C]

- **Saves** (D94: VT present on both instances, folder hash `45b3b433...` (55 files) on c5h-i and c5h-h, workstation,
  RTX 4080): `C5H-0166-TINY_SETUP` (8ce90063..., turn 18) and `C5H-0166-HUGE_0350` (c499d3c6..., turn 350) on c5h-i,
  `C5H-0166-TINY_0008` (b2ceb663..., turn 9) on c5h-h, each arm loading its own copy `C5H-0166-4107-<MAP>`; every arm of a
  save on the one instance (server against EXE on the same install).
- **Arms**: *stock1*, *stock2*: two stock sessions of the same save (3702's stock arm: the 32-bit pin
  `i0ship-26234371-ia` 3c2f47da... in the CP slot, stock config.ini, no proxy, no server, no composite); *joined*: the
  full shipped product (3308's prep through 3702's set_map): proxy + server.exe + composite of this branch beside the
  proxy with the manifest, the x64 DLL of the pin layout `x64-04b1211b-pa803776` (74588135...), config.ini in the shipped
  A'' form of D33 (darkred's D33 set with Overlay, River and Arrows on: D93, D119, D125), VP64Options.ini with
  `[Renderer] ServerRenderer = 1` and every other option at its default (WhiteFogMask = 1 since 0185, D136), the
  repository's `shell_composite.ini` unchanged (`decal_tap=1`, `overlay_tap=1`; no key set by the driver),
  `render_decals` and `render_fow` at their defaults, no test switch.
- **Cameras**: huge = the 20 distinct step plots of 3408's camera script (`shell/evidence/3408/script_huge.json`: the
  start (101,25), the ends of the first 3 pans (the 4th returns to the start), 16 jump plots; revisits once), zoom 0;
  Tiny = the capital at zoom 0, three plots chosen from stock1's census that add the most route, farm, improvement and
  river plots within 4 plots of the camera (at least 3 plots from the cameras already chosen), and last the capital 4
  wheel ticks in (`plan_<map>.json`). Each camera: `UI.LookAt` of its plot (a camera placement, 3702's procedure), the
  wheel ticks by the harness's mouse wheel, popups closed by Esc until the top band is undimmed (D114), the cursor parked
  over the minimap, 8 s, the camera block (gt_reader), the picture; after a zoomed camera the driver wheels back out
  (`UI.LookAt` keeps the zoom: the dry run took the cameras after the zoomed one at zoom 11). Camera blocks are compared
  between the arms on view 0 and the projection (view 1, another view, differs by up to 0.16 between two stock sessions).
- **Revealed land in view**: tiny_setup (56 x 36, turn 18) 26 revealed land plots (6 with routes, 3 railroads, 4 farms,
  3 other improvements: 2 mines, a plantation; 7 river plots); tiny_0008 (turn 9) 23 (1 route: the capital; 9 river
  plots; no farm, no improvement yet); huge (128 x 80, turn 350) 2,689 revealed land plots (784 routes, 363 railroads,
  361 farms, 746 other improvements, 680 river plots; 774 plots visible, the rest fogged). Railroads and the improvement
  variety come from the huge save; rivers from all three.
- **Pictures**: stock = a harness shot (PrintWindow); joined = the presented frame (the composite's back-buffer dump,
  `shell_composite.shot` trigger; 3706 12.2 found PrintWindow of the joined path unreliable), then the composite's ini
  switched to `mode=passthrough` for one more dump (the EXE's own frame: its models, overlays and interface over the
  clear colour 84, 84, 84 where the server's world goes) and back to `mode=composite`.
- **Plot classes** (terrain-colour-reference.md 3's method, `render/evidence/3019/clean_mask.py`): per pixel the
  terrain point by a ray cast against the EXE's drawn height field (gt_reader of stock1), the plot = the nearest plot
  centre; the plot's state from stock1's census (Lua, read only, as the active team sees it: revealed route and
  improvement). Classes, revealed land only: **route** (railroad and road also apart), **farm**, **improvement** (every
  other improvement), **river** (a river on an edge), **bare** (the control: no route, improvement, river, feature,
  resource, city or unit); city (information).
- **Mask**: the class's plots, minus 16 px around every pixel the EXE draws itself in the joined arm's passthrough frame
  of the same camera (models, overlays, interface), minus 16 px around the screen columns trees can cover (FOREST,
  JUNGLE, MARSH, OASIS plots: `clean_mask.model_cover`, Forest is off under D33), minus the interface's rectangles
  (`clean_mask.ui_mask`), minus a 16 px frame. **Fogged plots stay in**: epic 0168 is merged into `main` (172a715a) and
  into this branch, so the joined arm draws the fog of war; the figures for visible plots only are given beside them.
  So the route and improvement plots that 3408 left out are in the mask.
- **Figures** (`shell/evidence/0166/4107/compare_4107.py`): per class the largest channel difference |B - A| against
  stock1 (8-bit): median / p95 / max and the share within 16; per camera and pooled over the cameras; difference images
  (x4, masked pixels dimmed blue) and a class map per camera on `F:/c5-64bit-work/0166/4107/compare/<map>/diff/`.

### 16.2 Runs [L]

| Arm | Instance | When (2026-11-15, EST) | Builds | Result |
|---|---|---|---|---|
| stock1 | c5h-i (tiny_setup, huge), c5h-h (tiny_0008) | tiny_setup 05:32-05:37, huge 05:41-05:51, tiny_0008 05:36-05:40; the cameras added to the Tiny plans later in extra sessions 05:44-05:49 (tiny_0008) and 06:01-06:05 (tiny_setup) | 32-bit pin `i0ship-26234371-ia` 3c2f47da... | rc 0 each; every file restored by hash (`backup_verify` equal / absent) |
| stock2 | as stock1 | tiny_setup 05:37-05:42, huge 05:51-06:01, tiny_0008 05:40-05:44; Tiny cameras c3-c5 taken again at the default zoom 06:05-06:09 (tiny_setup) and 06:16-06:20 (tiny_0008) | the same | rc 0 each, restored by hash |
| dry (drivers' dry run) | c5h-h | tiny_0008 05:49-05:55, huge 05:55-06:09, tiny_setup 06:09-06:16 | this branch 0f0f3ea2 (team branch before 4111): server.exe e5ba5d99..., proxy 67d3cc11..., composite 89b2e2e3... | rc 0, 0 LOUD lines; DIAGNOSTIC pictures; its Tiny cameras after the zoomed one were at zoom 11 (fixed before the final arm) |
| **final (ACCEPTANCE)** | c5h-h (tiny_0008, huge), c5h-i (tiny_setup) | tiny_0008 07:03-07:11, tiny_setup 07:03-07:12, huge 07:11-07:28 | 32445f66 (team branch 61cb6bf6 + 4111 done 5a5d80e3 + `main` 86838c52): **server.exe 0d9f0469..., proxy b1d29b49..., composite 770fe868...**, x64 DLL 74588135... | rc 0 each; **0 LOUD lines** in render.log, server.log, vpproxy.log, shell_composite.log; vpproxy.log `white mask: [Renderer] WhiteFogMask = 1: on`; huge bake-ahead 4,922 tiles, 0 failed; restored by hash, provision verify rc 0 |
| stock3 (diagnostic) | c5h-i | 06:20-06:28 | stock | the stale-cache check of 16.4 |

The huge joined arm ran on c5h-h against stock arms of c5h-i: crossed installs with the same VT hash 45b3b433 (D94:
valid). Camera blocks (view 0 and the projection) against stock1: largest difference 0.11 (tiny_setup), 0.25
(tiny_0008), 0.19 (huge) world units, from the view's height over the drawn terrain (the heights differ by 1 u16 in a few
samples between sessions, records 2.4); stock2 against stock1 differs by as much. Visual Checks records them as `same`
below 0.01 and `approximate` above.

### 16.3 Figures: the joined product and stock's own ceiling, per plot class [L]

Largest channel difference against stock session 1 (8-bit): median / p95 / max, share within 16. "Inner": plots at
least 2 plots from every unexplored plot (outside the reach of the clouds' edges; on the huge save every class plot in
view is inner, so the inner columns repeat). Fogged plots are in (0168 merged); the figures for visible plots only are in
the JSON (`final_visible`, `stock2_visible`). `improvement_split` on the huge save = the 23 plots of
`IMPROVEMENT_OBELISK` (id 40, 16.4); `improvement_rest` = every other improvement but farms. Generated by
`tables_4107.py` from `F:/c5-64bit-work/0166/4107/compare/<map>/compare_stock2_final.json`.

**tiny_setup** (5 cameras; camera view 0 + projection against stock1: largest |delta| 0.1112 world units)

| Class | pixels (inner) | joined vs stock1 | stock2 vs stock1 (ceiling) | joined, inner | stock2, inner |
|---|---|---|---|---|---|
| route | 687962 (687962) | 16 / 101 / 205, 51.0 % | 12 / 95 / 223, 55.2 % | 16 / 101 / 205, 51.0 % | 12 / 95 / 223, 55.2 % |
| route_railroad | 401375 (401375) | 31 / 112 / 191, 24.7 % | 27 / 109 / 201, 30.8 % | 31 / 112 / 191, 24.7 % | 27 / 109 / 201, 30.8 % |
| route_road | 286587 (286587) | 4 / 59 / 205, 87.8 % | 0 / 54 / 223, 89.5 % | 4 / 59 / 205, 87.8 % | 0 / 54 / 223, 89.5 % |
| farm | 502210 (502210) | 31 / 109 / 191, 25.1 % | 29 / 112 / 201, 29.4 % | 31 / 109 / 191, 25.1 % | 29 / 112 / 201, 29.4 % |
| improvement | 310602 (34183) | 6 / 87 / 170, 78.2 % | 0 / 43 / 173, 89.5 % | 8 / 83 / 166, 63.4 % | 0 / 53 / 162, 86.7 % |
| improvement_rest | 310602 (34183) | 6 / 87 / 170, 78.2 % | 0 / 43 / 173, 89.5 % | 8 / 83 / 166, 63.4 % | 0 / 53 / 162, 86.7 % |
| river | 1275007 (829050) | 4 / 27 / 174, 91.8 % | 0 / 16 / 177, 95.0 % | 3 / 13 / 174, 96.1 % | 0 / 6 / 176, 96.7 % |
| bare | 1129194 (0) | 6 / 87 / 195, 70.9 % | 1 / 42 / 164, 86.8 % | - | - |
| city | 19686 (19686) | 11 / 140 / 205, 56.5 % | 1 / 126 / 223, 65.5 % | 11 / 140 / 205, 56.5 % | 1 / 126 / 223, 65.5 % |

**tiny_0008** (5 cameras; camera view 0 + projection against stock1: largest |delta| 0.2479 world units)

| Class | pixels (inner) | joined vs stock1 | stock2 vs stock1 (ceiling) | joined, inner | stock2, inner |
|---|---|---|---|---|---|
| route | 68110 (68110) | 4 / 78 / 186, 89.1 % | 0 / 82 / 186, 88.5 % | 4 / 78 / 186, 89.1 % | 0 / 82 / 186, 88.5 % |
| route_road | 68110 (68110) | 4 / 78 / 186, 89.1 % | 0 / 82 / 186, 88.5 % | 4 / 78 / 186, 89.1 % | 0 / 82 / 186, 88.5 % |
| river | 1733646 (706382) | 5 / 36 / 178, 86.4 % | 0 / 17 / 193, 95.0 % | 4 / 15 / 167, 95.7 % | 0 / 3 / 166, 97.6 % |
| bare | 1498462 (288476) | 6 / 90 / 211, 73.9 % | 0 / 44 / 149, 85.5 % | 5 / 20 / 180, 92.1 % | 0 / 5 / 60, 98.5 % |
| city | 68110 (68110) | 4 / 78 / 186, 89.1 % | 0 / 82 / 186, 88.5 % | 4 / 78 / 186, 89.1 % | 0 / 82 / 186, 88.5 % |

**huge** (20 cameras; camera view 0 + projection against stock1: largest |delta| 0.1936 world units)

| Class | pixels (inner) | joined vs stock1 | stock2 vs stock1 (ceiling) | joined, inner | stock2, inner |
|---|---|---|---|---|---|
| route | 4250801 (4250801) | 7 / 62 / 216, 67.0 % | 5 / 65 / 223, 66.5 % | 7 / 62 / 216, 67.0 % | 5 / 65 / 223, 66.5 % |
| route_railroad | 2466886 (2466886) | 8 / 67 / 216, 63.5 % | 6 / 70 / 221, 63.2 % | 8 / 67 / 216, 63.5 % | 6 / 70 / 221, 63.2 % |
| route_road | 1783915 (1783915) | 6 / 54 / 181, 72.0 % | 3 / 55 / 223, 71.1 % | 6 / 54 / 181, 72.0 % | 3 / 55 / 223, 71.1 % |
| farm | 3309819 (3309819) | 15 / 64 / 225, 54.2 % | 15 / 63 / 227, 54.3 % | 15 / 64 / 225, 54.2 % | 15 / 63 / 227, 54.3 % |
| improvement | 2440918 (2440918) | 5 / 60 / 216, 74.5 % | 0 / 51 / 221, 84.0 % | 5 / 60 / 216, 74.5 % | 0 / 51 / 221, 84.0 % |
| improvement_rest | 2084794 (2084794) | 4 / 56 / 216, 80.3 % | 0 / 50 / 216, 83.1 % | 4 / 56 / 216, 80.3 % | 0 / 50 / 216, 83.1 % |
| improvement_split | 356124 (356124) | 22 / 74 / 206, 40.2 % | 0 / 59 / 221, 88.9 % | 22 / 74 / 206, 40.2 % | 0 / 59 / 221, 88.9 % |
| river | 4539482 (4539482) | 4 / 59 / 225, 74.5 % | 1 / 52 / 227, 76.2 % | 4 / 59 / 225, 74.5 % | 1 / 52 / 227, 76.2 % |
| bare | 4368335 (4368335) | 3 / 21 / 193, 93.5 % | 0 / 22 / 197, 93.7 % | 3 / 21 / 193, 93.5 % | 0 / 22 / 197, 93.7 % |
| city | 27462 (27462) | 3 / 55 / 182, 85.3 % | 1 / 51 / 184, 85.7 % | 3 / 55 / 182, 85.3 % | 1 / 51 / 184, 85.7 % |

### 16.4 Reading, and every class of remaining difference [L numbers, I where marked]

- **Routes, railroads, farms, bare ground, rivers, cities: at stock's own ceiling on the huge save.** Joined against
  stock differs from stock as much as two stock sessions differ from each other: route 67.0 % within 16 against 66.5 %,
  railroad 63.5 / 63.2 %, road 72.0 / 71.1 %, farm 54.2 / 54.3 %, bare 93.5 / 93.7 %, river 74.5 / 76.2 %, city 85.3 / 85.7
  %; medians 3-15 against 0-15; p95 within a few levels. The ceiling itself is low on route and farm plots: their
  layout is random per session (P6, 3.1), so a session's picture cannot be matched closer than another session's.
  On the Tiny saves route, railroad and farm plots likewise sit 2-6 points under the ceiling (the same random classes).
- **Rivers and bare ground on Tiny**: inner plots river 96.1 / 96.7 % (tiny_setup) and 95.7 / 97.6 % (tiny_0008), bare
  92.1 / 98.5 % (tiny_0008); medians 3-5 against 0; mean signed difference about 0 (-0.5 .. +1.2 per channel on tiny_0008's
  inner bare; `signed_bare.py`), mean |d| 2.5-5: an unbiased small difference on every textured pixel, the size of 0306's
  **near-camera caches**: stock draws the tiles near the camera from its uncompressed RGBA8 near pair, the server from
  BC1 (terrain-surface.md 10: MAD about 2, p95 7-8, no colour shift) [I: by its size and sign; not separated per pixel].
  On all plots (with the cloud edges) river 91.8 / 95.0 % and 86.4 / 95.0 %, bare 70.9 / 86.8 % and 73.9 / 85.5 %: the
  difference sits on the plots next to unexplored ones, where the server's clouds (0168) lie in other places than
  stock's (the cloud phase and edge of 0168's intended differences; `c3_plot_10_21` of tiny_setup shows it).
- **Improvement plots other than the obelisks**: huge 80.3 / 83.1 %, Tiny (two mines and a plantation) 78.2 / 89.5 %. Seen
  in the pictures: the mines' glow and smoke (particles: `Particle` is off under D33, intended with D33), the stone
  quarry ground (30 quarries on huge; 4111 14.3: the A8R8G8B8 height map read with R and B swapped by
  `decal_pass.cpp`, not fixed in this build), the near-cache class above, and the farm fields' random layout reaching
  into neighbouring improvement plots.
- **Trees and their shadows**: the tree plots are masked; next to forests the joined ground is up to 3.7 levels brighter
  in R and G (huge `s26_jump_39_37` bare: mean signed +3.75 / +3.42 / +1.34): stock's Tree_Shadow decals in the macro
  shadow and the trees' own shadows, absent under D33 (P10, MC answer 7: intended with trees).
- **Models under the white mask and the interface** are outside the mask (the passthrough frame + 16 px); the EXE draws
  them in both arms.
- **Fog**: 0168 is merged, so fogged plots are compared; the huge figures for visible plots only (route 54.2 / 55.2 %,
  farm 34.3 / 34.6 %, bare 88.9 / 90.5 %, river 58.2 / 63.7 %) sit at the ceiling as the all-plot figures do; the Tiny
  differences at the cloud edges are 0168's cloud phase.
- **Finding (not an intended class): the `IMPROVEMENT_OBELISK` ground.** On the huge save the server draws the decal
  `Landmark_Decal_AFR_D/H/S.dds` (`Resource/DX9/ImprovementTextures.fpk`, DXT5 128 x 128 albedo with an opaque round
  plaza in its alpha; rectangle 63 x 84 samples, priority 0, pass 0) on the 22 obelisk plots that hold one (around
  Thebes and Pi-Ramesses, (91..105, 17..40)); stock shows no such ground at the same camera. Joined 40.2 % within 16
  (median 22) against stock's own 88.9 % (median 0) on those plots. Checked: (a) the decal is in the EXE's records in
  stock (4106's `.dtap`, 23 Landmark decals: 22 AFR, 1 EURO on the one `IMPROVEMENT_LANDMARK` plot) and under D33
  (4101's census, the same rectangle and vertex hash), so the tap reads what the EXE holds; (b) it is not a stale stock
  cache: a third stock session (`stale_check.sh`) at (101,25), then (39,37), back, again, back, never shows it
  (vc-20261115-062603 .. -062715, DIAGNOSTIC); (c) none of 4106's cached tiles covered an AFR landmark (the cached set
  missed x 79..103), so the cache comparison never saw these tiles; the one EURO landmark tile pair was equal / 0.987. So
  the EXE holds the decal but does not show it, and the server draws it. Cause open [I, two candidates]: the EXE does not
  draw these records (a state the tap does not read), or it draws them with another texture than the name the tap gives
  for their handles. Settles it: a RenderDoc capture of a stock bake of such a tile (4109's method; RenderDoc on Tiny
  only, so a Tiny save with an obelisk, or the MC's word for one huge capture). Picture: vc-20261115-063737 (stock1,
  stock3 after the returns, joined, passthrough; DIAGNOSTIC, result fail).
  *Settled 2026-11-15 by 4113 (17.3): the EXE does draw these records, with exactly `Landmark_Decal_AFR_D/H/S.dds`
  (a stock Tiny RenderDoc capture of three bakes over an obelisk); stock's picture lacks the plaza because its tiles under
  the landmarks are stale (huge: 79 of 112 (decal, tile) pairs baked by the load's pre-bake at frames 30-35, before the landmarks were laid
  out at frame 57; on Tiny a state-3 tile is not re-baked after a decal add, even after the camera leaves and returns,
  until a zoom). Neither candidate above holds; the server needs no fix; copying stock's staleness is the MC's call.*

### 16.5 Pictures (Visual Checks, `--issue 4107`)

- **ACCEPTANCE** (D112, D114): the 30 final frames, one per camera (tiny_0008 5: vc-20261115-070809-121c ..
  -071028-d4aa; tiny_setup 5: -070848-6aa9 .. -071113-6061; huge 20: -071813-ff05 .. -072710-e10b), `--what` starting
  `ACCEPTANCE:` with the build and settings, the stock1 picture of the same camera as `--reference` (`same` or
  `approximate` by the camera delta of 16.2), the difference image (joined vs stock1, x4, outside the class mask dimmed
  blue) as `--comparison` (`attach_diffs.py`). Stimuli: the camera placed by `UI.LookAt` (3702's procedure, as 0167's
  and 0168's acceptance pictures) and the player's mouse wheel; popups closed by Esc first; no game state changed. No
  road built by the player's worker is shown (the brief's optional stimulus; 4105 12.6 measured a road's re-bake).
- DIAGNOSTIC: the passthrough frames of every joined camera (the model and interface mask), the dry run's frames, the
  stock references (stock1, stock2), the stale-cache check (stock3) and the obelisk finding. `visualcheck.py list --issue
  4107 --result undescribed`: empty.
- Game-derived data on `F:/c5-64bit-work/0166/4107/` (D141; never committed): `captures/<map>/<arm>/` (pictures, camera
  blocks, census, heights, grid), `compare/<map>/` (JSON, difference images, class maps), `compare/tables_final.md`,
  `summary_final.json`, `findings/`, `runs/` (each arm's records and logs: render.log, server.log, vpproxy.log,
  shell_composite.log, run.json, prep/restore records; the build and chain logs; the diagnostic scripts of 16.4). Drivers in `shell/evidence/0166/4107/` (`run_4107.py`, `compare_4107.py`,
  `tables_4107.py`, `attach_diffs.py`, `build.sh`, `stock_chain.sh`, `stock_extra.sh`, `joined_chain.sh`,
  `final_runs.sh`, `stale_check.sh`, `vthash.py`).

### 16.6 Done I: the offline bundle check is a named follow-up

(Closed 2026-11-16 by task 0364: the bundle member decal_tap, decals/load<NNNN>.dtap, is in format version 3, offline-scene-harness.md 2.10; a live Tiny span with decals, shadow and ice replays byte-equal in 8 of 8 frames, section 19; a huge span shows live tiles left with the 4111 tie order after the walk block appeared, section 19; ticket 0393.
Fixed 2026-11-16 by 0393: such a change now raises the tiles whose draw list changes; the huge span recorded again is
byte-equal 8 of 8, section 20.)

Epic 0301 (the scene recorder and player) was not in `main` when this task's branch took `main` (86838c52, 05:19) and
merged into `main` at 6653c096 on 2026-11-15 06:46 EST, after this task's series was built on the team branch; its
contract on `main` (`offline-scene-harness.md`) keeps section 2.10 reserved for 0166's decal member, and `main`'s
`srv_scene_bundle.*` / `srv_scene_recorder.*` have no decal member yet [C, `git grep` of `main` 975a36c9 at 06:58].
So no recorded bundle with a decal member exists to play. As the brief's done I allows: the file form's round trip is
shown by 4103's tests (10.5: `section_and_file_equal`, the writer's bytes equal the test's image, the five file-form
refusals), 4106 read its recorded `.dtap` through the server's own reader for every cached tile (13.2), and **the bundle
check is carried as a follow-up**: the decal member in the recorder and the player (`srv_scene_bundle.*` kind
`decal_tap`, the recorder writing `decals/load<NNNN>.dtap` at the span's first frame with the reader's
`writeCurrentImage`, 10.4 and 12.7; the contract's 2.10), then a Tiny span recorded with it and played offline against
the live frames of the same span. Owner: the MC's choice at 0166's merge (proposed: lead-0166's follow-up list, as the
member's files are 0301's and now on `main`).

### 16.7 The epic's evidence summary (done A-I), 2026-11-15

| Done | What it asks | Evidence (sections, paths, figures, entry ids) | State at 4107's hand-back |
|---|---|---|---|
| A | design: P1-P11, P15 answered, records, type table, passes, producers, route; the MC's review before code | section 0 (premises table P1-P16 with marks), 1-8; the MC's review answer #1357 (01:49, C1 accepted) recorded in the epic issue's log (01:53) before any wave-2 code | met (lead's record) |
| B (1) | records: the server's decals equal the EXE's by the PID census, per class, both saves; tap and file form | 9.3: image = census on every arm (Tiny 503 = 503 and 500 = 500; huge 17,709 / 17,657 / 17,679 equal), every key the same count; 4110's split 9.6 (image byte-equal); 14.1 (4111: the area block = layer_probe's +0x300 on every tile, Tiny 460, huge 3,021); file form 10.5 (`section_and_file_equal`, 10 / 10 plain and ASan) | met [L] |
| B (2) | pass: 4101's captured bake, all mips, bit for bit | 11.4: the four captured tiles 100 % of texels every mip, also with every texture loaded by name through gamefs, a second device identical; `render/evidence/0166/4104/c5r_decal_test_release.log` | met [L] (BC1 by stage 1's exact pass) |
| B (3) | caches: same session, every cached tile; causes named; 0130's river tiles; t8 set (0305) | 13.3-13.5 (4106): Tiny 2,543 / 2,568, huge 5,330 / 6,136 bit-equal, every decal-free tile bit-equal, causes in 13.4; 0130: 39 of 39 river t0 bit-equal; 14 (4111): tie order, tile+0x300 per tile through the tap (P14 equal in stock; the residency's use a switch, off, since D33's EXE sets it on 61 of 460 Tiny tiles), the stone quarry's A8R8G8B8 height map named (the fix in `decal_pass.cpp` not made) | met with the open points of 13.7 / 14 |
| C | frame level against stock per plot class, stock's two-session ceiling, ACCEPTANCE pictures | this section, 16.2-16.5; tables and JSON `F:/c5-64bit-work/0166/4107/compare/` (`tables_final.md`, `summary_final.json`, `<map>/compare_stock2_final.json`, difference images `<map>/diff/`); ACCEPTANCE entries vc-20261115-070809-workstation-121c .. vc-20261115-072710-workstation-e10b (30, 16.5) | met, with one finding (the obelisk ground, 16.4) reported first and open |
| D | parity pair, 30 AI turns, render_decals 1 / 0 and decal_tap 1 / 0: eq_stats identical | 4112, section 15 | 4112's |
| E | nothing regressed: 30 huge AI turns with decals on and off | 4112, section 15 | 4112's |
| F | failure behaviour: D125 live on the shipped pack layout; in-play faults | 4112, section 15 (oldpc, `neg_decal_job.py`); 12.4 and 12.5 for the stand-in tests | 4112's |
| G | tests: a negative test per check, the native suites plain and under ASan, 0 skipped | 4112, section 15; earlier rounds 9.2, 10.5, 11.5, 12.5, 13.2, 14 | 4112's |
| H | research and tracker: the file, its README line, the corrections in place | `terrain-decals.md` listed in `research/05-graphics-composition/README.md`; corrections by 4110 (terrain-rendering-inventory.md 1, 2.2, 2.3, row D; terrain-bake-passes.md 2, 4, 8, 9; asset-pipeline.md 3.2, 3.3, 3.6; world-systems-and-stubs.md; landmarks-and-models.md 3; water-system.md 4, 11; renderer-in-server-design.md 2.2, 6; terrain-bake-residency.md 3) and by 4107 (terrain-bake-comparison.md 6 and 7, terrain-bake-records.md 2.4, terrain-bake-verification.md 4.2 and 9, commit 84a76d20); 0301's contract: section 2.10 reserved for the decal member on `main`; bug 0130 and issue 0305 closing, actual hours: the lead's | met for the research files; tracker items the lead's |
| I | offline harness: file form with its loader; a recorded bundle plays offline equal to live once 0301 is in | 10.4-10.5 (file form, loader, round trip); 16.6: 0301 merged into `main` at 06:46, no decal member in its recorder yet: the bundle check is a named follow-up | follow-up named (16.6) |

### 16.8 Not done, open (4107)

- The obelisk ground (16.4): the cause, by a RenderDoc capture of a stock bake of such a tile; until then the product
  draws a ground stock does not show on `IMPROVEMENT_OBELISK` plots (22 on the huge save).
  *Settled by 4113 (17.3), 2026-11-15: stock's stale caches, not a skipped record.*
- The stone quarry's R/B swap in `decal_pass.cpp` (4111 14.3) is not in the measured build.
- The near-camera class (16.4) is 0306's (BC1 against the EXE's RGBA8 near pair), not separated per pixel here.
- The bundle check of done I (16.6).
- No ACCEPTANCE picture shows a road built by the player's worker (optional in the brief).
- Workstation indication only (COMMON 23): the final huge bake-ahead took 72.7 s after the field with 17.5 s of baker
  GPU time (the dry run of the same save: 22.2 s, 1.4 s) on a machine with several other games running; not compared
  under the quiet lock, no claim.

## 17. Integration, quarry and obelisk (4113)

Task 4113 (worker of lead-0166), 2026-11-15 08:53- EST, branch `task-4113-decal-integration` from `epic-0166-decals`
1358b8e0 (the team branch with `main` (epics 0140, 0301) merged by the lead at 51d996e3). Marks as above. Drivers in
`shell/evidence/0166/4113/` and `render/evidence/0166/4113/`; data on `F:/c5-64bit-work/0166/4113/` (D141; never
committed); suite builds on `C:/c5-64bit-work/0166/4113/<pass>` (removed at the hand-back).

### 17.1 Integration after the main merge [C, 2026-11-15]

- **server.exe builds** after the merge (`shell/evidence/0166/4113/build_server.sh s1`, 4105's command:
  `server/build_server.py --src D:/c5-64bit-work/9045/src-x64f --deps D:/c5-64bit-work/9045/bF/deps --config release
  --offline`, through `quietlock.py run`): exit 0 in 103 s, server.exe `6e5d80bd...`, proxy `1c491b1e...`, vp64helper
  `5fec51fa...`, x64 DLL `74588135...` (the pin's) on `F:/c5-64bit-work/0166/4113/builds/s1`.
- **The suites** (`shell/evidence/0166/4113/suites.sh`: 4112's list of 15.0 G, plus epic 0301's scene-bundle programs
  (`build_scenebundle_tests.py --run`, plain and ASan) and its pytest wrappers `test_sources.py`, `test_recorder_host.py`,
  `test_scenerun.py`, and epic 0140's `test_bake_yield.py`, plain and ASan): figures in 17.4.

### 17.2 The stone quarry: A8R8G8B8 files with their channels in their meaning [C, L offline, 2026-11-15]

- **Change** (`render/src/decal/decal_pass.cpp` `createFromDds`, commit 267d8f63): a legacy A8R8G8B8 DDS is created in
  the format gamefs gives it, B8G8R8A8 (typeless family, `_SRGB` view for t0, UNORM for t1 / t2), no longer as
  R8G8B8A8 over the same bytes. 11.3 corrected in place.
- **Test** (`render/tests/decal/decal_test.cpp`, `game_a8r8g8b8_decal_texture_has_its_channels_in_their_meaning`): a
  16 x 16 A8R8G8B8 file written by the test (R 220, G 120, B 40, A 200; no game bytes) into an install folder of its own,
  drawn by name through gamefs with the EXE's shaders, gives the targets **bit for bit** equal to the same colour given
  as R8G8B8A8 in channel order, and **different** from the file's bytes given as R8G8B8A8 (the old reading); gamefs's
  `sourceFormat` A8R8G8B8 and `B8G8R8A8_UNORM` asserted. `c5r_decal_test` Release 11 of 11, 0 skipped (09:15;
  `render/evidence/0166/4113/c5r_decal_test_release.log`): **the four reference tiles stay 100 % of texels, every mip**,
  with the captured textures and with every texture loaded from the install (`tree_shadow_2.dds`, A8R8G8B8 and grey,
  now B8G8R8A8: unchanged).
- **Comparator** on 4111's huge recording (`render/evidence/0166/4113/cmp.sh 4111 huge decals|tap|exe`, 4111's
  `cmp.sh` with this branch's `bakecompare.exe`, no `--decal-bgra`; summary `render/evidence/0166/4113/quarry_cmp.py`,
  output `quarry_cmp.txt`), 09:20-09:22:

  | Run (t8 rule) | 4111 (before) all | 4113 (fix) all | made equal / broke |
  |---|---|---|---|
  | `decals` (4105's rule) | 5,335 / 6,127 | **5,372** (t0 2,470 / 3,083, t8 2,902 / 3,044) | 37 / 0 |
  | `tap` (the EXE's +0x300) | 5,342 / 6,134 | **5,379** (t0 2,470, t8 2,909 / 3,051) | 37 / 0 |
  | `exe` (+0x2FC) | 5,342 / 6,134 | **5,379** | 37 / 0 |

  The fix's `tap` run equals 4111's `--decal-bgra all` probe texture for texture (0 / 0); 4111's Tiny recording `tap`
  unchanged (0 / 0: no quarry, the grey files sample the same). So 14.3's +37 is in the product.

### 17.3 The obelisk ground (16.4): the stock record, and when stock re-bakes its tiles [L, 2026-11-15]

Set-up: Goldberg instance c5h-g (F:, VT installed, D94), the pin `i0ship-26234371-ia` (3C2F47DA...), CP/VP/EUI, stock
`config.ini` (terrain pass on, `darkred.py config --arm stock`, no viewmask marker), RenderDoc 1.47 preloaded through
the harness's wrapper (0302's, built in this worktree), frame cap 30, 2554 x 1411; save `C5H-0166-4113-TINY` (a copy of
`C5H-0166-TINY_SETUP`, 8ce90063..., turn 18, 56 x 36). Instance set-up and restore: `setup_inst.sh` / `restore_inst.sh`
(4109's, by hash). The obelisk is placed by a Lua setter on the copy (DIAGNOSTIC: `obelisk_place.py`):
`IMPROVEMENT_OBELISK` (id 40) on plot **(7, 20)**, unowned grassland next to the capital (8, 23) (the player owns 7
plots, all with an improvement, resource or route).

- **The record** (`decal_probe.py --names --index --hash-verts` before and after, `census_diff.py`): one new decal,
  **`Landmark_Decal_AFR_D/H/S.dds`** (as on huge, 16.4), priority 0, rectangle (448.677, 1365.907)-(511.935, 1450.245)
  (63 x 84 samples), 2 triangles, **flags 1** (bit 0 "uploaded" set, bit 1 = pass 0), its batch with no upload pending,
  **in the small-decal spatial index** (cell (1, 5), point = the rectangle's centre (480.3, 1408.1); the index walk of
  `--index` found every small decal of the set, 641 items, 0 missing). The setter also re-laid three Tree_Shadow decals
  nearby. So **none of the record-side filters of 1.4 / 14.2 skips it** [L]; the same holds on 4111's huge recording:
  all 23 AFR landmark records have flags & 3 = 1 (bits above 1 are uninitialised there, 1.2).
- **The texture names are unique**: `fpkscan lookup` (c5h-g, VP's folder) finds one file each, all in
  `Resource/DX9/ImprovementTextures.fpk` (D 22,000 B, H 43,832 B, S 87,536 B); the EURO landmark (same pack) is drawn
  by the server on huge and equals the EXE's cache there (16.4: 0.987).
- **Stock does not re-bake every stale tile** (`layer_probe.py`, +0x1CC / +0x1D0 = the frame counter at the change and
  at the bake; `tile_gens.py`): the RenderDoc frame number of a capture is the EXE's counter - 1 (three bakes matched by
  their blend grid, `tile_of_bake.py`). After the add (counter 14440), the obelisk's tiles (6..8, 21) and (7, 22) were
  **stale, in view (+0x1D4 recent), state 3, drawn from a 64-texel cache**, and stayed so through 12 s with the camera
  38 plots away and the return (10 tiles of rows 28 and 23 re-baked on the return, none of the obelisk's), and through
  a second return to another plot: **a stale tile in state 3 is not re-baked** (3023's road, `terrain-bake-residency.md`
  3.3, "re-baked on re-entry" did not happen here). A zoom (4 wheel ticks at the plot) re-bakes them (size class 64 ->
  512, state 5). **In state 5 an add or remove re-bakes the decal's tiles at once**: the add's +0x1CC (counter k) and the
  bakes at k .. k + 2 (three tries, 10:09, 10:15, 10:21). The decal itself is laid out **about 7 frames after the
  DLL's setter** (the Lua call) [L: two tries; the frames of P13's 0.4 s].
- **Capturing that bake** needs the request to start between the setter and the add (the first frame of a request is
  the one after the trigger; RenderDoc then captures every second frame, 10.3 of `renderdoc-capture.md`; 4-5 frames per
  process before 0302's guard stops it). Tries (each a fresh process after the load's bake sweep, which ends about 90 s
  after the load on this save: 1,439 of 2,240 tiles cached): zoom only (6 frames: one bake, another tile), pan with the
  add away (3 + 4 frames: other tiles, rows above the obelisk), the add under the request (5 + 5 frames: the add came
  after the series), a per-frame re-add handler (3 frames: no bake: each add resets the tile's layer job, 5.1), the add
  0.15 s before the request (4 frames: the bakes 3 frames before the first captured frame).
- **The capture that settles it** (10:31, `zoom_watch_capture.py`: the obelisk placed after the load's sweep, a 4-tick
  zoom in at (7, 20), then a 4-tick zoom out with the request started the moment tile (9, 23)'s +0x1D0 changed, i.e. at
  the start of the row-by-row re-bake): `c5h-g_20261115-102704_frame3739/3741/3743/3745.rdc` (F:, never committed).
  Frames 3739, 3741 and 3743 hold the bakes of tiles (6, 22), (8, 22) and **(7, 22)** (EXE counter 3740, 3742, 3744; the
  layer probe after it), and **each draws the obelisk record first** (TerrainDecalShader pass 1 into the blend's three
  targets, SRC_ALPHA / INV_SRC_ALPHA, register 5 = (448.677, 1365.91, 511.935, 1450.24), priority 0, 2 triangles)
  with **t0 = BC3 128 x 128 (8 mips, `_SRGB` view), t1 = BC1 256 x 256, t2 = BC3 256 x 256 whose bytes equal the
  payloads of `Landmark_Decal_AFR_D/H/S.dds` of ImprovementTextures.fpk** (cmp of each texture export with the file
  minus its 128-byte header) [L]. **So the EXE draws the record, with the textures the tap names; it skips nothing.**
- **Why stock shows no plaza** [L on Tiny; I on huge]: pictures of one stock session at (7, 20) (DIAGNOSTIC,
  `--issue 4113`): `vc-20261115-103739-workstation-7866` right after the setter (the tiles stale, state 3, 64-texel
  cache: no plaza under the obelisk) and `vc-20261115-103816-workstation-abb8` after a zoom in and out (the tiles
  re-baked, state 5: the plaza's diamond pattern under the obelisk; zoom level not identical). On huge, 4111's stock
  recording (`layers_at_bake.json` with its census): of the 112 (decal, tile) pairs under the 23 AFR landmarks (82 distinct tiles), **79 are stale
  with +0x1CC 57 > +0x1D0 30-35**: baked by the load's pre-bake before the landmark decals were laid out (frame 57) and
  not re-baked since; 33 fresh. The EXE's picture there is its cache from before the decal (the "stale" class of 13.4).
  The server bakes the current records, so it shows the plaza where stock's caches are older than their records.
- **Server: no change.** The decal pass and the reader already follow the EXE's draw (same record, same filters, same
  textures; the pass is bit-exact on the four captured tiles, 11.4). Matching stock's picture on these plots would mean
  copying the EXE's stale caches (not re-baking a state-3 tile after a decal arrives): a product decision for the MC,
  not a decal-pass fix. Not done: the bit-for-bit check of the captured obelisk tile (7, 22) against the server's bake
  (4104's `mkref_decal.py` + a fifth reference tile; the capture frame3743 holds the full bake).

### 17.4 The suites after the merge (pass 1) [C, 2026-11-15 08:56-10:33]

`shell/evidence/0166/4113/suites.sh pass1` at 1358b8e0 (the team branch after the main merge, before the quarry fix),
workstation with 6 heavy jobs of other teams (quietlock check 08:54); 0 skipped in every suite; **0 ASan report files**
in the four folders. Logs and summary: `C:/c5-64bit-work/0166/4113/pass1/` (copied to `F:/c5-64bit-work/0166/4113/suites/pass1-logs/` and `pass1-summary.txt`).
**No integration break of the merge was found**: every build succeeded (shell, overlay reader, host tests, water,
decal, overlay / fow scenes, render, scene bundle, server.exe), so no merge fix was committed.

| Suite | Plain | MSVC `/fsanitize=address` |
|---|---|---|
| render `c5r_decal_test`, `c5r_bakepost_test`, `c5r_bake_test`, `c5r_bakeresidency_test` (0140's residency changes included) | 4/4, exit 0 | 4/4, exit 0, 0 reports |
| shell, the four tap suites | 118 passed | 118 passed |
| server `server/tests/render/decal/` | 11 passed | 11 passed |
| server decal wiring + residency source + render host | **92 passed** | 89 passed, 3 failed: `test_feed_changes_the_bake_ignores[fow-2]`, `test_idle_pacing_before_a_camera_then_legacy_poll`, `test_idle_pacing_off_keeps_the_old_poll`; rerun once (`rerun_host.sh`): the first passes, **the two 3308 pacing counts fail again** (as 4112's 15.5: ASan poll counts on a loaded machine) |
| overlay / fow / water scenes | 3 / 3 / 19 passed | 8 cases PASS / 3 passed / 17 cases PASS |
| 0301 scene bundle programs (`build_scenebundle_tests.py --run`: bundle_format, recorder, sceneshot, sources) | 24 + 413 + 138 + 137 checks, exit 0 | bundle_format 413, recorder 138, sources 24 passed; **`sceneshot_test.exe` timed out at the runner's 600 s** (0301 measured it under ASan in 9 min on a quiet machine); rerun alone without a timeout: **137 passed**, 0 reports (17.4a) |
| 0301 pytest wrappers (`test_sources.py`, `test_recorder_host.py`, `test_scenerun.py`) | 21 passed | 10 passed (sources, recorder host) |
| 0140 `test_bake_yield.py` | 9 passed | 9 passed |

After the quarry fix (267d8f63) `c5r_decal_test` was run again, Release: 11 of 11 (17.2).

17.4a: `sceneshot_test.exe` (MSVC `/fsanitize=address`, the pass's build) rerun alone without the runner's timeout,
10:34:02-10:47:14 (13 min on the loaded machine): `RESULT: 137 passed, 0 failed, 0 skipped`, exit 0, no AddressSanitizer
report (log `F:/c5-64bit-work/0166/4113/suites/pass1-logs/sceneshot-asan-rerun.log`). The 600 s timeout of
`build_scenebundle_tests.py --run` is short for this program under ASan on a loaded workstation (0301's).

## 18. The EXE's exact order for decals of equal priority (ticket 0366)

Ticket 0366, 2026-11-15 13:25- EST (account B implementer, branch `task-0366-decal-tie-walk`). Static pass: headless
Ghidra 12.1.4 read-only on a scratch copy of the `civ5` project (`shell/evidence/0366/gh.sh`, the 3950 script renamed)
and `llvm-objdump` of `%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe` (b2ce96ae..., build 4390913);
decompiler output on `C:/c5-64bit-work/0366/out/` (D83, never committed; removed at the hand-back). Below is behaviour only, [C] unless marked.

### 18.1 The spatial index of the small decals [C]

- **The index is TDS+0x6C itself** (a uniform 2D bin grid, `FBin2D.h` in the EXE's assert path), not an object at
  +0x4DC: AddDecal passes ECX = TDS+0x6C to the insert 0x69CD30 (0x49B8B0 `lea eax, [ebp+0x6C]`, 0x49BC20). Its members:
  +0x00 / +0x04 the minimum (= the world rectangle's min, 1.1), +0x08 / +0x0C the maximum (the world rectangle's max),
  **+0x10 / +0x14 the inverse cell size per axis** (cells / (max - min), float, set by 0x5A3980 when the grid is
  allocated), **+0x18 the cell heads** (one u32 item index per cell, 0x0FFFFFFF = empty, cell c = cy x cols + cx), **+0x1C
  cols, +0x20 rows**, **+0x24 a pointer to the item pool** (= TDS+0x90; the pool is embedded at TDS+0x4DC: +0x00 free
  head, +0x04 live items, +0x08 the item array, +0x0C items used, +0x10 capacity). So TDS+0x6C..+0x90 is the index
  (*1.1's "+0x90 pointer to the spatial index (an embedded object at +0x4DC)" corrected here: +0x90 is the index's item
  pool pointer*).
- **An item** is 24 bytes: +0x00 (next item << 1) | bit 0 "free", +0x04 the previous item (0x0FFFFFFF none), +0x08 /
  +0x0C the point, +0x10 the batch, +0x14 the slot. 0x798A00 takes a free item (or appends one) and clears bit 0.
- **The point** is the centre of the decal's clamped rectangle: ((min + max) x 0.5) per axis, single precision (SSE);
  the decal is small when neither side (max - min, single precision) exceeds 256 (0xA039B0), else it goes to the large
  list (18.2).
- **A point's cell** (0x53F0C0): per axis trunc((p - min) x inverse) in single precision (SSE `addss` of the negated
  minimum, `mulss`), clamped to 0 .. cols - 1 (rows - 1).
- **Insert (0x69CD30)** puts the item **at the head of its cell's list** (next = old head, previous = none, the old head's
  previous = the item). **Remove (0x5E7E30, from RemoveDecal 0x68F1B0)** unlinks it (its neighbours joined; the head moves
  to the next item when it was the head) and frees it. Nothing else links an item: the only caller of 0x69CD30 is
  AddDecal. So **a cell's list holds its live small decals newest add first**, whatever was removed in between; this is the
  age of the AddDecal call, not the batch list order (a removal frees a slot that a later add reuses, 1.2).

### 18.2 The large list [C]

TDS+0x9C the node array (16-byte nodes {next, previous (bit 31 a flag), batch, slot}), +0xA8 the count, +0xAC the head,
+0xB0 the tail. AddDecal appends a large decal after the tail (0x49BC2C..: node.previous = old tail, old tail.next = the
node); RemoveDecal unlinks it (0x59F290). So **the large list is in add order of the live large decals**. The handle of
a large decal is its node index with bit 31 set.

### 18.3 The draw's selection and walk (0x742EF0, called by the tile bake 0x694A60) [C]

- **The tile rectangle** the bake passes (0x694C9E..0x694CD5): min = (tile+0xDC - 1, tile+0xE0 - 1), max = (tile+0xDC +
  tile+0xD4, tile+0xE0 + tile+0xD4), integers converted to float; tile+0xDC / +0xE0 the origin (64 x, 64 y), tile+0xD4 the
  x extent (65; 64 in the last column). **Both axes use the x extent +0xD4.** So for an origin (ox, oy): [ox - 1, ox + e] x
  [oy - 1, oy + e], e = 65, or 64 in the last column (both axes). (In the last row e = 65 changes nothing: every rectangle
  is clamped to the world, whose max is below oy + 64 there.)
- **The query** (0x742EF0 -> 0x6D7EC0 -> 0x724CA0): the tile rectangle grown by 256 (0xA039B0) on every side, q = [min -
  256, max + 256). Its cells: cx0 = max(0, trunc((qmin.x - min.x) x inverse.x)), cx1 = min(cols, max(0, trunc((qmax.x -
  min.x) x inverse.x) + 1)) (exclusive), the same in y; single precision, `cvttss2si` (an out-of-range value gives
  0x80000000, so 0).
- **The walk** (0x724CA0 begin, 0x53A630 next, 0x645E70 next cell): starting at cell (cx0, cy0) (read without a range
  check), each cell's list from its head; when a list ends, x + 1; at x >= cx1, x = cx0 and y + 1; at y >= cy1 the walk
  ends. **Rows outer, columns inner.** An item is taken when its point p satisfies qmin <= p < qmax on both axes.
- **The tests per taken item**, in the walk's order: the record's +0x08 bit 0 (uploaded), the pass bit = the wanted pass,
  the batch's +0x30 = 0 (no upload pending), and the rectangle overlapping the tile rectangle inclusively (rec.min <=
  tile.max and tile.min <= rec.max per axis). Kept as a 12-byte entry {batch, slot, priority} appended to TDS+0x4D0.
- **Then the large list** from its head (TDS+0xAC, next at node +0x00): flag bit 0, the pass bit and the same overlap
  test (no upload test), appended in list order.
- **The sort** 0x648890 (called with the entry count as the depth limit) is the library's introsort in its 2008-era
  Dinkumware form, comparing only the priority (float `<`):
  - more than 32 entries and depth left: partition (0x520B60), depth = depth / 2 + (depth / 2) / 2, recurse into the
    side with fewer entries, loop on the other (the left side when it is shorter, else the right);
  - depth exhausted with more than 32: heap sort (make-heap 0x469B10: sift-down of every inner node from n / 2 - 1 to 0;
    then for n down to 2: the last entry taken out, the root moved to the last place, the taken entry sifted down from
    the root over n - 1 entries, 0x6D6100). Sift-down 0x6E9F50: from the hole take the right child (2h + 2) unless it is
    less than the left one (2h + 1), down while the child exists, a lone left child at the bottom last; then the value
    sifts up (0x40FA20) while the parent is less than it;
  - 32 or fewer: insertion sort (0x581410): each next entry, when less than the first, goes to the front (the others
    shift right); else it moves left past every entry it is less than (equal entries keep their order).
  - The partition: mid = first + n / 2; median (0x53D750): when n - 1 > 40, step = n / 8 and the median of three is taken
    of (first, first + step, first + 2 step), (mid - step, mid, mid + step), (last - 2 step, last - step, last), then of
    (first + step, mid, last - step) (last = the final entry); else of (first, mid, last). Median of three (0x5C1460):
    swap mid and first when mid < first; swap last and mid when last < mid; swap mid and first when mid < first. Then
    the "fat" partition: the pivot run [pf, pl) starts at mid and grows down while the entry before it equals the pivot
    and up while the entry after it equals it; gf = pl scans right: an entry greater than the pivot is passed, an equal
    one is swapped to pl (pl + 1), a less one stops the scan; gl = pf scans left: an entry less than the pivot is passed,
    an equal one is swapped to pf - 1 (pf - 1), a greater one stops it. Both scans at the ends: the result is [pf, pl).
    Only gl at the start: (pf, pl) swapped when pl != gf, pl + 1, (pf, gf) swapped, pf + 1, gf + 1. Only gf at the end:
    gl - 1, pf - 1, the two swapped when they differ, then (pf, pl - 1) swapped, pl - 1. Else (gf, gl - 1) swapped, gf +
    1, gl - 1. Then the scans go on.
- So the draw order of a tile depends on (a) every candidate's cell and its position in the cell's list, (b) the large
  list's order, and (c) for more than 32 selected decals the whole selection of that pass (decals that draw nothing
  included), since the partitions move ties across each other. Each pass (0, 1) is selected and sorted on its own.
- The EXE's selection is [ox - 1, ox + 65] (inclusive), narrower than the server's query square [ox - 2, ox + 66]
  (10.2): a decal ending between ox - 2 and ox - 1 (or starting between ox + 65 and ox + 66) is in the server's list but
  not the EXE's [I: likely the cause of 13.4's "a decal reaching under 4 samples over the tile's edge, absent from the
  EXE's cache"; checked in 18.6].

### 18.4 What was built [C]

**Corrected 2026-11-16 (ticket 0406):** "any miss: this generation goes out without the block" no longer holds in play: a miss with an intact snapshot holds the build and re-reads the batch (counter `walkHolds` at +260), see [terrain-decals-walk.md](terrain-decals-walk.md); a build that hits the restart limit still publishes without the block.

- **The walk block** (`shell/common/shell_decal_tap.h`, image version 1 kept, the field `VPShellDecalImage::walkOffset`
  at +100 was reserved): a 32-byte header (the index's origin TDS+0x6C / +0x70, inverse +0x7C / +0x80, cols +0x88, rows
  +0x8C, small and large counts) and 16 bytes per record in record order: the cell (cy x cols + cx) whose list holds it
  and its rank from the head, and the item's point; or `VPSHELL_DECAL_WALK_LARGE` and its rank in the large list.
  Control block: `walkSmall`, `walkLarge`, `walkMisses` (+248..+256, were reserved).
- **The tap** (`shell/composite/decal_tap.cpp`): a build step `B_WALK` right after `B_AREA` reads TDS+0x6C..+0x93, the
  item pool header, every cell head, the used items (one guarded read each) and the large list node by node, all in one
  Present (`WalkBegin`, a consistent copy), maps each item's (batch, slot) to the snapshot's record (the tap now keeps
  each record's slot), then checks and ranks the copied cells' items 512 per step within `decal_budget_ms` over later
  Presents (`WalkStep`) and builds the block (`WalkEnd`). *(The first version did all of it in one step: 6-13 ms in one
  Present under ASan on the huge synthetic world, over 4110's per-Present limit; the recordings of 18.5 ran that
  version; the staged one computes the same block (the tests check every walk entry against the generator, and the
  huge world's spread build against its one-shot build byte for byte, 18.7), and was checked live once more on Tiny,
  18.5.)* Every item must name a live
  record of the snapshot exactly once, carry the point AddDecal stores (the rectangle's centre, bit-equal), lie in the
  cell 0x53F0C0 gives that point, and be small (a large-list node large); the large list's length must equal its count;
  every record must be found. Any miss: this generation goes out **without** the block (walkMisses + 1, one log line
  with the reason; the server then keeps the 4111 rule); a fault: no walk for the life of the process (one LOUD line),
  the decals go on. A change of the walk alone (equal content) makes a new generation (B_SAME compares the block too).
  16 more byte patterns before the first read (52 in all): AddDecal's index TDS+0x6C (0x49B8B0), its large test against
  0xA039B0 (0x49BBDE) and the small path (0x49BC09), the item pool (0x798A13), the insert at the head (0x69CD38,
  0x69CD5D), the cell (0x53F0C9), the walk (0x645E7C), the query's range and point test (0x724DD0, 0x724E5F), the draw's
  grown query and large list (0x742F0D, 0x742F3D, 0x74305B, 0x743182), the sort's threshold (0x6488A3) and the bake's
  tile rectangle (0x694C9E). No call, no write.
- **The reader** (`server/src/render/srv_decal_tap.*`): the block inside the image after the header, aligned, not
  overlapping (`DREF_OFFSET` / `DREF_ALIGN`); 1 .. 2^20 cells, finite origin, finite positive inverse, small + large =
  decalCount, each small record in a cell with a finite point, the ranks of each cell and of the large list exactly 0 ..
  count - 1 (`DREF_WALK`, BadLayout, ackResult 5); counter `walkSets`; the apply log line names the tie order in use.
- **The order** (`render/src/decal/decal_set.cpp`, `c5r::decal::DecalWalk` in `decal.h`): `buildIndex` sorts each cell's
  decals and the large list by rank; `exeSelection(set, ox, oy, pass)` is 18.3's selection (the tile rectangle [o - 1, o
  + e], e = 65 or 64 in the world's last column; the grown query's cells in single precision, `cvttss2si` semantics; rows
  outer, columns inner; the point test; the overlap test; the large list); `exeSortByPriority` is 18.3's introsort entry
  for entry; `decalsForTile` with a walk returns pass 0's and pass 1's sorted selections merged by priority (pass 0 first
  on equal priority; each pass keeps its own order, which is all the decal pass uses). Without a walk (an older image, a
  miss) the 4111 rule as before. With a walk the list holds exactly the EXE's selection, so the decals of the 4111 square
  that the EXE does not select (between ox - 2 and ox - 1, or ox + 65 and ox + 66) are no longer drawn.
- **bakecompare**: with a walk block `--tie-order list` (the default) is the EXE's order; `--tie-order rule4111` drops the
  walk (the 4111 rule on the same recording); the report's decal summary carries `walk_cols`, `walk_rows`, `walk_large`.

### 18.5 Live: fresh stock recordings, the walk block against the EXE's index [L, 2026-11-15 13:59-14:14]

4106's method (13.1): c5h-f (Goldberg, F:, VT present, D94), claimed by 0366; the pinned shell test DLL
`i2-shell-test-26234371` (eb8d26c0...), stock game config (no darkred, no viewmask marker), CP/VP/EUI; the composite of
this branch (`shell/build.cmd`, the tap of d0190ece, unchanged since; shell_composite.dll `0c690232...`) in passthrough with `tap=1
tap_byte_layer=1 bake_tap=1 decal_tap=1`, the other taps off, `lockstep=0`, main section `Local\VPShellSection_0366` made
by `tap_live.py consumer` (no server). Saves `C5H-0366-TINY_0008` (b2ceb663..., = 4106's, 4111's, 0367's) and
`C5H-0366-HUGE_0350` (c499d3c6...), copies in c5h-f's saves folder. Driver `shell/evidence/0366/live_0366.py`
(`run.sh chain`: 4106's arm plus a walk check: right after the census, in the same process with the decal generation
unchanged, the EXE's index TDS+0x6C and large list read by ReadProcessMemory and every record's (cell, rank) compared
with the recorded image's walk block as multisets of (content key, cell, rank)). Files restored by hash at the end
(DLL, front-end Lua, shell.helper removed; `backup verify` OK). Data `F:/c5-64bit-work/0366/ref/<save>/armS/` (never
committed).

| Save | In game | Caches t0 + t8 | Changed during the copy | Decals (image = census) | Generation | Index grid | Small + large | **Walk block = the EXE's index** |
|---|---|---|---|---|---|---|---|---|
| Tiny t0008 | 14:01:17 | 1,318 + 1,250 | 0 | 482 = 482 | 6 | 14 x 10 cells (inverse 0.0039073, 0.0039078: 255.9 samples) | 462 + 20 | **482 of 482**, the same grid |
| huge t350 | 14:06:08 | 3,084 + 3,051 | 62 tiles | 17,744 = 17,744 | 2 | 32 x 21 cells (inverse 0.0039067, 0.0039070: 256.0 samples) | 17,609 + 135 | **17,744 of 17,744**, the same grid |

So the EXE's index has about one cell per 4 x 4 plots (256 samples), and a tile's grown query (578 samples) spans 3 x 3
or 4 x 4 cells: ties across cells are common [L].

### 18.6 Results: bakecompare on the fresh recordings [L, 2026-11-15 14:14-14:30]

`render/evidence/0366/cmp.sh` (bakecompare of this branch, Release clang-cl, `C:/c5-64bit-work/0366/render-build`; EXE
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe` b2ce96ae..., shaders read from it; install c5h-f with VT
and VP's folder; `--t8-rule tap`, the tile flags, 3406's plot art), runs `walk` (decalsForTile in the EXE's order) and
`rule4111` (the same set without the walk: 4111's rule, the same recording, the like-for-like baseline); figures
`render/evidence/0366/figures.py` -> `F:/c5-64bit-work/0366/figures.txt`. Bit-equal = every mip of the BC1 texture
byte-identical. The stone-quarry fix of 4113 is in the pass on both runs (14.4's last column needed `--decal-bgra`).

| Recording, run | t0 | t8 | All | Differing, by cause |
|---|---|---|---|---|
| 14.4: 4111 Tiny, tap t8 (4111's rule) | 1,299 / 1,320 | 1,251 / 1,254 | 2,550 / 2,574 (0.9907) | ties 13 + 1, edge column 6 + 2, tree-shadow border 2 + 0 |
| 0366 Tiny, rule4111 | 1,298 / 1,318 | 1,248 / 1,250 | 2,546 / 2,568 (0.9914) | |
| **0366 Tiny, walk** | **1,312 / 1,318** | **1,248 / 1,250** | **2,560 / 2,568 (0.9969)** | **edge column 6 + 2 only** |
| 14.4: 4111 huge, tap t8 + quarry probe | 2,470 / 3,083 | 2,909 / 3,051 | 5,379 / 6,134 (0.8770) | ties 601 + 135, stale 11 + 7, other 1 |
| 0366 huge, rule4111 | 2,446 / 3,053 | 2,873 / 3,020 | 5,319 / 6,073 (0.8758) | |
| **0366 huge, walk** | **3,038 / 3,053** | **3,012 / 3,020** | **6,050 / 6,073 (0.9962)** | **stale 11 + 7, a decal the EXE's cache lacks 4 + 1** |

- **Per texture, walk against rule4111 on the same recording: Tiny 14 made bit-equal, 0 broken; huge 731 made
  bit-equal, 0 broken** [L]. No texture is made worse.
- **The tie class is 0 on both saves.** Tiny's 14 include 4111's 13 + 1 tie textures and both tree-shadow-border
  textures of 13.4 / 14.4 (order or selection; not separated). The decal pass draws fewer decals with the walk (Tiny
  3,008 against 3,050, huge 30,034 against 31,130): the EXE's narrower selection (18.3) [L].
- **Every remaining texture's cause** [L by drop probes, `render/evidence/0366/probes.sh`, `tile_decals.py`; mechanism
  [I]]:
  - Tiny: the east edge column x = 55 with t8 (6 t0 + 2 t8): stage 1's wrap-column rule (13.4), not decals.
  - huge, **stale** 11 t0 + 7 t8: the EXE's +0x1CC > +0x1D0 (13.4's class: the EXE changed the tile after its bake).
  - huge, **a decal the EXE's cache lacks** 4 t0 + 1 t8: (106, 26) t0 bit-equal when the HB modern-farm ground (list
    order 15,708) is left out; (106, 27) t0 and t8 when the modern-farm grounds 15,705 and 15,708 and the aluminium-mine
    ground 17,667 are left out (17,667 alone: t8 equal, t0 0.979); (107, 29) t0 when the stone ground 15,969 is left out
    (13.4's "decal sliver"); (109, 28) t0 when the city ground 9,540 is left out (`--tie-order reverse` changes nothing:
    not the order). All are camera-near tiles beside 13.4's stale (106, 28), their +0x1CC <= +0x1D0 (so the stale rule
    does not flag them), and the decals are late load adds (5 of the 6 in batch 18, the last one, census of the session); [I] the EXE baked these
    tiles before those decals were drawable (added in the frame of the bake, or not uploaded yet: 18.3's flag and
    upload tests), and did not bake them again. Not a rule the server can follow from the records; the product's
    server bakes from the records it has.
- 62 huge textures (31 tiles) changed in the EXE during the cache copy (`changed_during_copy` in the recording; their
  records at copy time are freed slots) and are skipped by bakecompare as "bake failed: BadLayout" in both runs (6,135
  copied, 6,073 compared).
- t8 set (`--t8-rule tap`): Tiny 0 / 0 differing decisions; huge 0 EXE only, 1 server only (14.1's kind: the EXE's own
  state at copy time).
- **A second Tiny session** (`C5H-0366-TINYB_0008`, the same save bytes, 15:12-15:14, the staged walk of d8c076b5 in the
  composite): the walk block equal to the EXE's index again (**481 of 481**); bakecompare walk **2,560 / 2,569**
  (rule4111 2,546; per texture 14 made equal, 0 broken): edge column 6 + 2 and one more t0, (49, 19): mip 0 bit-equal,
  one block of a lower mip differs (5,459 of 5,460 blocks); its 4 decals are pass-1 tree shadows, `--tie-order reverse`
  leaves it as it is (not the order) and the first session's (49, 19) is bit-equal; the EXE re-baked that tile late in
  this session (+0x1D0 = frame 1,225, against the load's 20-60): the EXE's state of this session [L, I mechanism]. So
  the order figures hold across two sessions (13.7's open point).

### 18.7 Tests [C, 2026-11-15]

`shell/evidence/0366/suites.sh` (every step through the quiet lock's gate; logs kept on `F:/c5-64bit-work/0366/logs/`):

- `server/tests/render/decal` case **`walk`** (the decal_tap_test program; an emulator of 18.1 / 18.2 written as linked
  lists, independent of the reader's arrays): the sort on a hand-worked case of 33 entries (a0..a31 at 0.5, b at 0.1 ->
  b, a1..a15, a0, a17..a31, a16; a stable sort would keep a0..a31) and on 4,000 random inputs (sorted, a permutation,
  equal to a stable sort at 32 entries or fewer, unchanged when all are equal); ties across cells (rows outer: P in cell
  (1, 0) before Q in cell (0, 1), where the 4111 rule gives Q, P); more than 32 decals on a tile (the hand-worked case
  through the walk); an add after a removal (D, C, A by the cell's list where list order gives C, A, D); large decals
  after the small ones in add order, against their list order; a pass-1 decal sorted on its own and merged; the EXE's
  tile rectangle (a decal ending at o - 1.5 not drawn, at o - 1 drawn; the last column's e = 64 on both axes);
  6 random sets of 400 adds and removes (random grids 1..9 x 1..7, some large, some pass 1, image order shuffled):
  `exeSelection` equal to the emulator's walk on every tile and pass, `decalsForTile` = each pass's selection sorted,
  merged by priority; 15 refusals of the block (each code, counter and the set kept; through the section ackResult 5);
  an image without the block after one with it (the 4111 rule again); 3,000 images with random bytes in the block
  (735 accepted, 2,265 refused, no crash, every index inside its set). The suite: plain **12 passed**, MSVC
  `/fsanitize=address` **12 passed, 0 ASan reports**.
- `render` `c5r_decal_test`: Release passed, ASan passed (0 ASan reports, 0 skipped).
- `render/tools/bakecompare/test_bakecompare_4106.py`: 20 cases, 0 failed (two new: a recording with the walk block
  accepted, with and without `--tie-order rule4111`; the refusal text of `--tie-order` names `rule4111`).
- `shell/tests/test_decal_tap.py` (29 tests now): the walk block exact in the synthetic world (every entry against
  TestIndexBuild recomputed in Python, in every image the suite checks), `decal_test=walk` (a large decal in the large
  list, a tie at the head of a cell's list; after a change the new road at its cell's head), `walkmiss` (an item naming a
  slot without a live record: published without the block, walkMisses, the log line, not FAILED), `walkfault` (the item
  pool pointer unmapped: one LOUD line, no walk for the life of the process, the decals still published), the huge world
  with its index (every entry checked), the 52 patterns against the EXE on disk, the header offsets. Results (shell builds
  of d8c076b5, 2026-11-15 15:01-15:12): plain **29 passed**; MSVC `/fsanitize=address` **29 passed, 0 ASan reports**.
  On the way: the one-step walk failed the 4110 per-Present limit under ASan (6.2 and 12.9 ms against 5 ms; 18.4), and
  after the split one more failure came from the test world itself (a rebuild of its 17.8k-item index inside the test's
  mid-build change); the world now unlinks and re-inserts that one item as the EXE does.
- With `test_bake_tap.py`, `test_water_tap.py`, `test_overlay_tap.py` (plain, before the split, 14:20-14:40): 120
  passed, 1 failed: `test_overlay_tap.py::test_late_query_is_counted_stale` (its reader saw no stale episode under load;
  4111 saw the same, 14.1; no decal code in it); rerun alone three times (15:20, d8c076b5's plain build, 7 heavy jobs of
  other teams on the machine): failed, failed, passed: a timing case of the overlay tap under load.

### 18.8 Cost, not done, open

- **Cost** [L, indication, COMMON 23: the workstation loaded by other teams' games and suites]: the live composite's
  publication lines (`F:/c5-64bit-work/0366/logs/shell_composite_live.log`, the one-step walk of 18.5): the walk step 0.015 -
  0.041 ms on Tiny, 0.73 - 0.98 ms on huge (17.7k decals, one step); every publication's largest Present 1.9 - 2.8 ms
  at `decal_budget_ms` 2, as before (4110). The staged walk splits the huge one into about 35 steps. The server's
  `decalsForTile` with a walk: per tile two selections over 3 x 3 to 4 x 4 cells and two sorts; bakecompare's wall time
  (whole runs: load, bakes, compare) Tiny 11 s both, huge 40 s with the walk against 26 s without [I: one run each, the
  walk run first, on a cold file cache of the install's textures; the query itself (about 1,000 list operations per
  tile) was not timed on its own].
- **Not done**: a product-path session (proxy + server.exe, `ServerRenderer = 1`, D33) with the walk (the reader and
  `decalsForTile` are the server's own code, run here by bakecompare; server.exe was not built in this ticket, its
  sources compile in the decal test and bakecompare builds with the server's toolchain); the server wiring suites
  (`test_decal_wiring.py`, residency) were not run (their images carry no walk block: the 4111 rule, unchanged code
  paths); the heap-sort path of 18.3 (depth exhausted) is in the code and unit-checked only through the properties, no
  live tile reaches it [I]; why the EXE's caches lack the 5 late grounds of 18.6 (the frame order of AddDecal, the
  upload 0x480A30 and the bake) is not traced.
- **Open**: none for the tie order (7.2 closed). The D33 product does not change the walk (the index holds every live
  small decal under either configuration); not recorded under D33 here.

## 19. A decal update applied from the set in force, and the load's order (ticket 0365)

Ticket 0365, 2026-11-15 22:24 EST on, branch `task-0365-decal-delta-load-order` (from main 3710b71b). Code:
`server/src/render/srv_decal_tap.{h,cpp}` (`TryDelta`, `CheckDecalImage` with a base set, `DecalDeltaInfo`, the counters
and the log line), `render/src/decal/decal_set.cpp` and `render/include/c5r/decal/decal.h` (`buildIndexFrom`),
`render/src/bakeresidency/residency.cpp` and `render/include/c5r/bake/residency.h` (`ResidencyOptions::staleFirst`,
`tileHasBake`), `server/src/render/srv_residency_source.{h,cpp}` (`decalAheadWaitMs`, the re-bake line). Tests:
`server/tests/render/decal/` (case `delta`, mode `--recorded`), `server/tests/render/test_decal_wiring.py` (two tests),
`render/tests/bakeresidency/residency_test.cpp` (`stale_first_rebakes_before_the_bake_ahead_goes_on`). Live job and
statistics: `shell/evidence/0365/`. Marks as in the file.

### 19.1 Where the 57 ms went, and what was baked twice (before 0365)

- **The apply's parts** [L, indication, workstation (COMMON 23: other teams' games and suites running), 2026-11-15
  22:30, clang-cl /O2, scratch bench `C:/c5-64bit-work/0365/bench/` (not committed) on 0366's recorded huge `.dtap`
  (17,744 decals, 876,732 vertices, 8.6 MB, walk and area blocks), means of 15 repetitions]: the copy of the image out of
  the section 2.4 ms; **the FNV-1a 64 of the whole image 12.0 ms** (byte by byte: one multiply's latency per byte, 8.6 M
  bytes; quiet repetitions 10.5-10.9 ms); the checks and the build 19.7 ms, of which the content hash of every decal
  (`DecalContentHash`, FNV-1a over the vertex bytes) about 9.8 ms and `buildIndex` (walk lists and grid) about 2.5 ms;
  then, outside the logged time, the change queue's diff of the two sets (two sorts of 17.7k hashes) about 4 ms. Sum about
  38 ms here against 57 ms median live on huge (15.4; the game and the server's other threads share the machine).
- **Tiles baked twice at the load** [L, 4112's render.log, every arm of 15.4]: the bake-ahead's bakes minus the land
  tiles at "bake-ahead complete": fin-on2 42, pre-on 88, pre-on2 97, dry-on 46, fin-on 371 (GPU shared; its generations 2
  and 3 came 11.3 and 12.8 s after the field). Not 2,500-3,000: that is the count of tiles generation 2 raised, almost all
  without a bake yet (15.4 corrected). The load's generations against the field: generation 1 4.2-10.2 s before it in five
  arms (0.84 s after in dry-on), generation 2 0.45-1.3 s after, generation 3 2.4-4.4 s after (5780-7079 and 1673-3180
  changed rectangles: whole images, above the delta's share). The 18-23 s of their D92 line are the time to their
  last land tile, mostly first bakes in ring order.

### 19.2 The delta (acceptance 1)

- **Rule.** A generation is applied from the set in force when the image is of the same load (`loadSeq`), the same
  world rectangle and area grid, newer than the set, its `changedSince` is in (0, the set's generation] (the changed
  block then lists every decal added or removed since the set, 9.1) and it lists at most max(64, 25 % of its decals)
  rectangles (`kDecalDeltaMaxShare`; above it the whole build is cheaper, and a load's layout is that). A decal is
  *touched* when its rectangle is, bit for bit, one of the changed rectangles. Every untouched decal of the image must
  equal one untouched decal of the set (rectangle, priority, pass, texture names, vertex bytes: what `DecalContentHash`
  covers), one to one: in record order first (the image keeps the EXE's batch and list order), by a search on a key of
  rectangle, priority, pass and vertex count when the order breaks (`reordered` counts those), then the order resumes.
  A kept decal takes the set's hash and its grid cells (`buildIndexFrom`: each cell's list renumbered, the removed
  decals dropped, the added ones inserted, a cell sorted again only when one joined it or the renumbering moved a tie;
  the span kept unless a removed decal reached the grid's edge or an added one reaches past it, then `buildIndex`'s grid);
  only the touched decals are hashed. The walk's lists are built from the image's walk block as before (O(n)). Then the
  change queue skips its diff: every decal added or removed is touched, so its rectangle is listed (`rectsNotListed` 0).
- **Whole path** (unchanged code): no set in force, a new load, another world or area grid, `changedSince` 0 or newer
  than the set, too many changes, or a refused delta (an untouched decal with no equal in the set, or one of the set's
  untouched decals gone: a changed list that does not name a change). The result is the same set either way.
- **What still runs per generation**: the image copy and its FNV-1a 64 (the tap's integrity check; a faster hash needs a
  layout version step in the tap, `shell/`, not this ticket), every record's checks, the vertex block's copy, the walk
  block. So the delta removes the per-decal hashing, the grid's sort and fill and the diff: about 22 ms of the bench's 38.
- **Log** (render.log): the generation line keeps its text up to `..., X ms` (the same measure as before: copy, checks,
  build) and adds ` | total T ms, delta: P of the set in force and N new decals in the changed rectangles, R out of
  order, grid from the set in force, C cells sorted` or ` | total T ms, whole: <why>`. T includes the change queue.
  Counters `deltaSets`, `wholeSets`, `deltaRefused`, `deltaGridWhole`, `lastTotalMs`; `setDelta(false)` switches it off.
- **Tests** [C, 2026-11-15]: case `delta` of `decal_tap_test.exe`: every pair is applied (a) with the delta after its base,
  (b) whole by a fresh reader, (c) with the delta off after its base, and (a) must equal (b) field for field (decals with
  hashes, names, vertices, the grid's origin and lists, the walk's vectors and lists, the area) and in `decalsForTile` of
  every tile of the world and one ring around it, and the queued change of (a) must raise the same tiles and area tiles as
  (c)'s. Pairs: one removed, one added, a road (one removed and two added), an added decal over a kept decal's rectangle,
  two kept decals swapped, the name table renumbered, a decal at the grid's edge removed (the grid whole); refused or not
  tried, each with its reason asserted: an unlisted add, an unlisted remove, a kept decal's vertex changed unlisted, a new
  load, another world, `changedSince` 0, `changedSince` past the set, too many changes. Images: the small standard one,
  random huge-sized ones (17,800 decals, ties, a tenth large) with and without walk and area blocks, and a chain of 40
  generations (a road each) on one reader: 40 by the delta, all equal. `--recorded` runs the same scenarios on the
  recorded `.dtap` files (game data, not committed): 4106's Tiny and huge, 4111's huge, 0366's huge and Tiny: every pair
  equal, every tile's list equal (results in 19.5).

### 19.3 The load's order (acceptance 2)

- **Stale first** (`ResidencyOptions::staleFirst`, **default false** since set 1 of 19.4; it was true in the build of set
  1): `idle()` takes the tiles whose bake is older than their content before the tiles without a bake, each pass in ring
  order (a second cursor, reset with the first). Without it one pass in ring order does both; the stale tiles come first
  as long as they are the first rings (the visible tiles are ring 0, so their re-bakes come first either way; at a load
  the tiles baked first are the inner rings), not after a view change. Test
  `stale_first_rebakes_before_the_bake_ahead_goes_on` (both arms: with the switch a tile baked around view A and raised
  after the view moved to B is re-baked in the next 4 bakes; without it, those go to B's unbaked tiles).
- **The bounded wait** (`ResidencySourceConfig::decalAheadWaitMs`, 2,000 ms, no server.cfg key: `srv_render_host.cpp` is
  not this ticket's): after a field with nothing resident and no set of the field's load in force, the bake-ahead
  (`idle()`, not the visible tiles of `beforeDraw`) waits until a set arrives or the bound passes. A set that arrives
  while nothing is resident marks its generation as drawn by every bake (its change re-bakes nothing, as a set before
  the field, 12.3). render.log: `residency: bake-ahead of load L waits up to 2000 ms for the load's first decal set
  (...)` and `residency: bake-ahead of load L resumed after X ms: <a set arrived | the bound passed without a set>`.
  `settle()` (the offline harness, no tap) ends the wait. Why 2,000 ms: 19.1's arrivals; a later set costs what it cost
  before (its tiles baked twice), a wait to the bound costs 2 s of bake-ahead. The visible tiles are not held, so a
  player sees no gray frame for it; they are baked at once and, if the set names them, again.
- **The re-bake line**: per generation also `decals: generation N: its K re-baked land tiles (a bake before the raise)
  done, the last X ms after the publication (Y ms after the raise)`: the latency of the tiles that showed without the
  change, apart from the generation's tiles that had no bake yet (the old line, unchanged, counts both).
- **Tests** [C, 2026-11-15, `test_decal_wiring.py`, stand-in baker, 6 x 6 tiles]: set before the field (4105's test, now
  also asserting no wait): 36 bakes, 0 re-bakes; set 0.3 s after the field, no camera before it:
  the wait ends on the set, 36 bakes, 0 re-bakes, the generation's change skipped; no set within the bound: the wait ends
  at 2,0xx ms, the set after the bake-ahead re-bakes its one tile (37 bakes, 1 re-bake, the re-bake line for 1 tile).
  With a camera before the set (first run of the second test), the visible tile (1, 1) was baked during the wait and
  again with the set: 37 bakes, 1 re-bake, as the rule says.

### 19.4 Live A/B on oldpc (acceptance 3)

**Method** (written before the results): the product path of 0156 (`tools/testbed/jobs/product_path.py`: proxy +
server.exe with `ServerRenderer = 1`, the composite, D33 with Overlay on) in tb1 on oldpc (the machine's own Steam EXE,
VT installed), the huge save copy `TB-HUGE_0350` (c499d3c6...), 30 AI turns per arm, one testbed job per set with
`--headed --quiet` (0349's job script with the arms by build: `shell/evidence/0365/ab_0365.py`, `pp_0365.py`), arms in
the order before, after, after, before. Builds (workstation, `shell/evidence/0365/build_0365.sh`, release, the 9045
release deps and fork source copy): server.exe **before** = the branch base 3710b71b (`41b51f57...`); the four x64
libraries of both arms = the pin layout `x64-04b1211b-pfdee656` (DLL `29f58e17...`, COMMON 18); the proxy and the
composite (`aa4d27ce...`) from this branch (0365 touches neither), one file for both arms of a set. Statistics
(`shell/evidence/0365/process_0365.py`, results `results_set1.json`, `results_set2.json`): per generation the render.log
figures of 19.2's log line (the text up to `X ms` is the same in both builds), in play = after "bake-ahead complete";
both runs of a build pooled; the median difference with a percentile bootstrap 95 % (20,000 resamples); the lockstep
exact share per AI turn (summarize_3311.py, as 0349), Welch 95 %. Grade **B** (2 interleaved runs per arm on the quiet
machine; PERFORMANCE.md 2.6).

**Set 1** [L, job `20261115-231028-7cb3`, 2026-11-15 23:10 - 2026-11-16 00:36, rc 0; after = `75cd7f30...` (d0dbb17d,
`staleFirst` on); quiet proof: alone, foreign load 0.103 cores (limit 0.1: svchost 166 s, MsMpEng 112 s, audiodg 60 s of
the 5,129 s), CPU mean 15.7 %]:

| Figure | 1-before | 2-after | 3-after | 4-before |
|---|---|---|---|---|
| rc, turns, LOUD lines (server, render, proxy, composite), crashes | 0, 30, 0, 0 | 0, 30, 0, 0 | 0, 30, 0, 0 | 0, 30, 0, 0 |
| Gray frames after the bake-ahead | 0 | 0 | 0 | 0 |
| Generations (in play) | 238 (234) | 241 (237) | 238 (234) | 234 (230) |
| **Apply per generation in play, ms: median (p90, max)** | **25.2** (26.7, 28.3) | **18.5** (19.9, 22.4) | **18.5** (19.9, 22.2) | **25.1** (26.5, 30.8) |
| ... by the delta / whole | 0 / 234 | 237 / 0 | 234 / 0 | 0 / 230 |
| Load generations 1-4: changed rectangles; apply ms | 9106, 4622, 2846, 1280; 18.2, 20.6, 23.6, 24.9 | 9066, 4516, 2855, 1383; 18.4, 20.8, 18.6, 19.9 | 9286, 4366, 2949, 1433; 18.1, 21.1, 18.2, 20.5 | 9125, 4416, 2909, 1433; 18.2, 21.8, 23.2, 24.4 |
| The residency's set per generation, ms median | 2.4 | 2.2 | 2.2 | 2.4 |
| **Tiles baked twice before the bake-ahead ended** | **88** | **191** | **200** | **156** |
| Bake-ahead after the field, ms | 15,003 | 15,472 | 15,450 | 15,031 |
| Generations in play re-baked later than 0.6 s; median / max ms | 0; 105.6 / 502.3 | 0; 100.3 / 556.7 | 0; 105.5 / 585.4 | 0; 105.6 / 532.8 |
| Load generations 3, 4: their tiles that had a bake, the last re-baked after the publication (ms) | (no line) | 209.6, 193.7 | 210.5, 227.7 | (no line) |
| Lockstep exact share over the 30 turns | 97.68 % | 97.36 % | 97.48 % | 97.48 % |

Pooled (before 2 runs, after 2 runs): apply in play median 25.2 -> 18.5 ms, **-6.7 ms (bootstrap 95 % -6.8 .. -6.5),
-27 %**, sd 0.96 / 1.16 ms; the residency's set 2.4 -> 2.2 ms (-0.2); generation latency in play median 105.6 -> 103.1 ms,
-2.5 (-9.0 .. +3.4: no difference), none above 0.6 s in either build (4112's 6-7 of about 230 on the loaded workstation;
none on the quiet oldpc in either build); lockstep exact share per turn 97.58 -> 97.42 %, -0.16 pt (Welch 95 % -0.45 ..
+0.12, detectable 0.41 pt: no difference).
**Why the numbers moved**: every in-play generation went by the delta (2 to a few hundred touched decals of about
17,600): the per-decal content hashes (FNV over about 7 MB of vertex bytes), the grid's sort and fill and the change
queue's diff are gone (19.1: about 22 of the bench's 38 ms on the workstation; 6.7 of 25.2 ms here); what is left (18.5
ms on oldpc) is the image copy, the FNV-1a 64 of the whole image, the record checks, the vertex block's copy and the walk
block, all O(image). Load generations 3 and 4 (17 % and 8 % of the decals changed) went by the delta too (18.2-18.6
against 23.2-23.6 ms; 19.9-20.5 against 24.4-24.9 ms).
~~**staleFirst cost more than it gave**: the tiles baked twice rose from 88 / 156 to 191 / 200 and the bake-ahead took
0.42-0.47 s longer, because each load generation's re-bakes now ran at once instead of coalescing behind the ring
order.~~ *Corrected 2026-11-16 01:40 by set 2: with `staleFirst` off the after build baked 191 tiles twice and took
15,609 ms, and set 2's first before arm 196 and 15,266 ms: the set-1 difference was run-to-run spread of the load (when
generations 3 and 4 arrive against the bake-ahead's progress), not the option. With it off the load generations 3 and
4 also had their baked tiles (59, 51) re-baked 0.20-0.26 s after the publication (the inner rings come first in one
pass). So the option showed neither a measured cost nor a measured gain on huge; it stays off by default (the behaviour
before 0365), available for a radius-limited or moving-camera case.* Set 2 measured the final build (off).

**Set 2: the final build** [L, job `20261116-004232-f799`, 2026-11-16 00:42 - 02:08, rc 0; after = `3ee01a0f...`
(719e6fbc, `staleFirst` off by default; later commits change comments, research and evidence only); the same before
build; proxy `475578fa...` in both arms; quiet proof: alone, foreign load 0.113 cores (MsMpEng 178 s, svchost 157 s,
audiodg 61 s), CPU mean 15.9 %]:

| Figure | 1-before | 2-after | 3-after | 4-before |
|---|---|---|---|---|
| rc, turns, LOUD lines, crashes | 0, 30, 0, 0 | 0, 30, 0, 0 | 0, 30, 0, 0 | 0, 30, 0, 0 |
| Gray frames after the bake-ahead | 0 | 0 | 0 | 0 |
| Generations (in play) | 241 (237) | 240 (236) | 242 (238) | 242 (238) |
| **Apply per generation in play, ms: median (p90, max)** | **25.0** (26.6, 30.0) | **18.4** (19.9, 28.5) | **18.3** (20.0, 22.0) | **25.1** (26.6, 30.0) |
| ... by the delta / whole | 0 / 237 | 236 / 0 | 238 / 0 | 0 / 238 |
| Load generations 1-4: apply ms | 18.1, 21.8, 23.1, 25.6 | 17.3, 21.8, 21.7, 20.0 | 17.3, 22.1, 20.6, 21.0 | 17.1, 21.1, 22.4, 25.4 |
| The residency's set per generation, ms median | 2.4 | 2.2 | 2.2 | 2.4 |
| **Tiles baked twice before the bake-ahead ended** | **196** | **191** | **207** | **204** |
| Bake-ahead after the field, ms (bakes; baker GPU ms; bake CPU mean ms) | 15,266 (5,118; 2,745; 1.92) | 15,609 (5,113; 2,824; 1.92) | 15,886 (5,129; 2,777; 1.96) | 15,102 (5,126; 2,776; 1.89) |
| Generations in play re-baked later than 0.6 s; median / max ms | 0; 105.5 / 539.3 | 0; 106.2 / 499.8 | 0; 102.8 / 525.4 | 0; 107.4 / 575.1 |
| Load generations: their tiles that had a bake, the last re-baked after the publication (ms) | (no line) | 263.0, 198.4 | 233.7, 204.6, 199.5 | (no line) |
| Lockstep exact share over the 30 turns | 97.52 % | 97.40 % | 97.45 % | 97.40 % |

Pooled: apply in play median 25.0 -> 18.4 ms, **-6.6 ms (bootstrap 95 % -6.8 .. -6.5), -26 %** (the same as set 1);
the residency's set -0.2 ms; latency in play median 107.1 -> 104.3 ms, -2.8 (-7.7 .. +1.5: no difference), none above
0.6 s in either build; lockstep exact share 97.46 -> 97.42 %, -0.04 pt (Welch 95 % -0.33 .. +0.26, detectable 0.42 pt:
no difference); tiles baked twice 196 / 204 against 191 / 207 (no difference). Seconds per AI turn (wall, the arms'
medians) 35.8-36.2 in all eight arms.

**Not explained at the time (explained by 0402, below)** [L, both sets]: the bake-ahead took longer with the after builds in all four of their arms
(15,450-15,886 ms against 15,003-15,266 ms in the four before arms, about +0.5 s, +3 %), with `staleFirst` on (set 1)
and off (set 2), with the same number of bakes and the same CPU and GPU per bake; the after arms ran fewer idle slices
per second during it (1,867-1,878 slices against 1,888-1,898, in a longer time) and saw fewer yields (155-168 against
193-200). The residency's idle path is the same code with the option off, no wait was taken (generation 1 came 7.0-8.9 s
before the field in every arm) and the load generations' applies were shorter. Gray frames after the bake-ahead: 0
everywhere. A profile of the render thread during a load (the two builds) would settle it; not done here.
- **Corrected 2026-11-16 (ticket 0402):** explained, and not caused by 0365. Two grade-A quiet jobs on oldpc with 0365's code give +86 ms (95 % -161..+333) and -25 ms (-221..+171). The bake-ahead's level drifts by up to about 0.4 s and holds over consecutive arms (it follows the render loop's pass length, which moves with the 1-3 ms pacing wait); 0365's ABBA put both after arms on such a shift. Set 1 also carried about 0.3 s of real cost from staleFirst's extra bakes (staleFirst is off). See [bake-ahead-after-0365.md](../08-performance/bake-ahead-after-0365.md). (Replaced: "Not explained".)

### 19.5 Suites (workstation) [C, 2026-11-15 22:50 - 2026-11-16 00:00]

- `server/tests/render/decal/test_decal_tap.py` (11 cases with `delta`, the runner's negatives): plain 13 passed; MSVC
  `/fsanitize=address` 13 passed, 0 ASan reports (`log_path` folder empty). `--recorded` on the five recorded `.dtap`
  (4106 Tiny and huge, 4111 huge, 0366 huge and Tiny): 75 pairs (15 scenarios each), every set and every tile list equal,
  plain and ASan, 0 reports. Indications from the same run (file form: no FNV; workstation loaded): a road's apply
  on the huge recordings 8.3-11.7 ms by the delta against 19-34 ms whole.
- `render/build.py --test -R "bakeresidency|decal"`: plain passed (`c5r_bakeresidency_test` 32 cases with
  `stale_first_rebakes_before_the_bake_ahead_goes_on`, `c5r_decal_test`); `--asan`: passed, 0 ASan reports, peak commit
  1,088 MB.
- `server/tests/render/test_decal_wiring.py` + `test_residency_source.py`: plain 48 passed; ASan build 48 passed, 0
  reports. Every top-level suite of `server/tests/render` plain passed (the subfolders need builds of their own and were
  not run, except scenebundle). Scenebundle: `build_scenebundle_tests.py --run` 4 of 4 programs passed (the `settle`
  cases of `sources_test` included); `test_offline_equals_live.py` 7 failed with `NewerVersion` (the scene library's
  bundles were rewritten on 2026-11-15 23:04-23:35 as format_version 3, newer than this branch's reader; not 0365's).
- First run of the within-the-wait wiring test with a camera block before the set: 37 bakes (the visible tile baked
  during the wait and again with the set): the test now has no camera before the set (19.3).

## 20. A change of the tie rule re-bakes the tiles whose draw list changes (ticket 0393)

Ticket 0393, 2026-11-16 03:58 EST on, account B implementer, branch `task-0393-decal-tie-rule-stale-tiles` (from main
71e00af8). Code: `render/src/decal/decal_set.cpp` and `render/include/c5r/decal/decal.h` (`tieRuleTiles`,
`TieRuleStats`), `server/src/render/srv_decal_tap.{h,cpp}` (`DecalChange::tieTiles`, the counters `tieRuleChanges`,
`tieRuleTiles`, `lastTieRuleMs`, the apply line). Test: `render/tests/decal/decal_test.cpp` (`tie_rule_change_*`).

### 20.1 The fault (0364) and the rule [C]

- **Before 0393** a generation's `DecalChange` raised the tiles of its changed rectangles (the image's list plus every
  added or removed decal, 12.1) and the tiles whose area state changed (4111, `areaTiles`), nothing else. When the walk
  block (18.4) appears or goes between two applied sets, `decalsForTile` changes from the 4111 rule to the EXE's walk
  (or back) on every tile, also where no decal changed; a tile baked under the old rule kept its order of equal
  priorities, and its selection ([o - 2, o + 66] against the EXE's [o - 1, o + e], 18.3), until something else re-baked
  it. 0364 met it on the huge load (`offline-scene-harness.md` 19): generations 2 and 3 came without the walk block
  (the tap's walk check missed while the load still added decals, 18.4's `walkMisses`), 4 and 5 with it; the live tiles of
  the view kept generation 2-3's order, about 5.7k pixels in one tile strip, and did not heal by waiting.
- **The rule (0393).** When `hasWalk(prev) != hasWalk(next)` the reader's apply calls `c5r::decal::tieRuleTiles(prev,
  next)`: every tile of next's world with a decal in either set's grid (the 4111 square's lists, a superset of the walk's
  selection) is a candidate; a candidate is returned when its draw list differs: `decalsForTile` under each set, each
  pass's entries in list order (pass 0 goes to the blend's targets and pass 1 to the macro-shadow target, each drawn in
  the list's order, so the interleaving of the two passes changes nothing drawn), compared entry by entry by content
  (hash, rectangle, priority, pass, vertex count). Each returned tile goes into the change as a point rectangle at its
  centre (origin + 32): `tilesForRects` gives exactly that tile (the query square [o - 2, o + 66] of no neighbour reaches
  o + 32), so the residency (`srv_residency_source.cpp`, unchanged) raises it as any other changed tile; a tile without a
  bake yet costs nothing (its first bake reads the newest set). `DecalChange::tieTiles` says how many of `rects` are such
  points. With the same rule on both sides nothing is added (the changed rectangles already cover content changes).
- **Exact, not "every decal tile"**: a tile whose lists are equal draws the same, so it is left alone; every tile whose
  lists differ is raised, including those that differ only by the selection (a decal ending between o - 2 and o - 1 can
  draw texels, 18.3 and 18.6). Not refined further (for example by ignoring the order of equal-priority decals whose
  rectangles do not overlap): the decal's vertices are not checked to lie inside its rectangle, so that would rest on an
  assumption [I].
- **Log**: the apply line keeps its text of 0365 and, after it, `; tie rule changed (A -> B): K of C tiles with decals
  raised for it, X ms`; its "changed rectangles" count leaves the tie points out (as before 0393).

### 20.2 Tests [C, 2026-11-16]

`render/tests/decal/decal_test.cpp` (no game data; a model of the residency: every tile baked under the first set, then
the tiles a change raises baked again under the set in force, per tile the list it was last baked with; a tile is stale
when that list differs from what the set in force draws):
- `tie_rule_change_leaves_no_stale_tile`: a 4 x 4-tile world, the EXE's index one cell per tile: a tie across two cells
  (the 4111 rule Y, X; the walk X, Y), a decal inside the 4111 square but outside the EXE's tile rectangle, a lone
  decal, two decals of different priority, a large decal, a pass-0 and a pass-1 decal of equal priority (merged lists
  differ, each pass's equal: not raised). Without the block, then with it: **0 stale tiles**, re-bakes = the tie tiles,
  more than 0 and fewer than all; the returned tiles are exactly the tiles whose lists differ; each point rectangle names
  its tile alone through `tilesForRects`; back without the block (a walk miss): 0 stale; the same rule on both sides:
  nothing; a content change in the same generation (an added decal, its rectangle listed): 0 stale. **Teeth**: the same
  changes without the tie rule (the server before 0393) leave stale tiles (and with the added decal's rectangle alone).
  A world rectangle with a NaN or inverted: nothing, no crash.
- `tie_rule_change_random_sets`: 24 random worlds (3-8 x 2-6 tiles, 20-219 decals of 3 priorities, one in 20 large, one
  in 7 pass 1, cells of 64-256 samples), each: without the walk, then with it plus random removes and adds (listed),
  then without again: 0 stale tiles every time; without the rule 22 of the 24 had a stale tile after the first step.
- Runs: 20.6.

### 20.3 Cost [L indication, workstation]

Bench `bench_tie.exe` (source and outputs kept in `F:/c5-64bit-work/0393/work-archive/bench/`, not committed: the reader's and the query's sources of
this branch, clang-cl /O2 as `build_decal_tests.py`): each recorded `.dtap` loaded through the reader's file form with
its walk block switched off (walkOffset = 0), then as recorded, then off again (the reader's change and counters), and
`tieRuleTiles` alone 15 times each way.

| Recording | Decals | Candidate tiles | **Tiles raised for the rule** | tieRuleTiles ms (median, min-max), off->on / on->off |
|---|---|---|---|---|
| 0366 huge t350 (`F:/c5-64bit-work/0366/ref/C5H-0366-HUGE_0350/armS/decals/load0003.dtap`) | 17,744 | 3,060 | **2,239** | 38.2 (29.7-48.3) / 41.1 (34.9-59.6) |
| 0364 huge library bundle's member (generation 5; now in `retired/`) | 17,756 | 2,999 | **2,214** | 42.8 (39.4-47.2) / 51.2 (39.9-63.8) [a] |
| 0366 Tiny t8 | 482 | 556 | **69** | 1.3 (1.0-2.1) / 1.4 (1.3-1.7) |

(2026-11-16 05:21, no other job of this ticket running, the workstation loaded by 11 registered heavy jobs of other teams:
indications only, COMMON 23. [a] 04:14, with this ticket's server build running beside it. The first version, comparing
the merged lists, raised 2 more tiles on each huge recording: 2,241 and 2,216.) So on huge about 73 % of the tiles with decals change their list when the rule changes,
and the comparison takes about 40-50 ms on the render thread, once per rule change.
**The re-bake cost** is that of the raised tiles that already had a bake: at most 2,239 x the per-bake render-thread
time; with 0365's oldpc figure (19.4 set 2: bake CPU mean 1.92 ms) at most about 4.3 s of render-thread time, spread
over the residency's idle slices in ring order (visible tiles first); a raised tile without a bake costs nothing extra.
How many had a bake on the live load: 20.4.

### 20.4 Live: the huge library bundle recorded again, offline equals live [L, 2026-11-16 04:10-04:45]

- **Build** (`build.sh` in `F:/c5-64bit-work/0393/work-archive/`, after 0364's; the copies of the pin's fork source 04b1211b8 and
  9045's release deps taken from 0401's scratch, DLL 74588135...): `server/build_server.py --config release --offline`
  (OK in 160 s), the layout = the pinned `x64-04b1211b-pfdee656/bin` (DLL 29F58E17...) with this server.exe, the
  composite `shell\build.cmd`, c5render, the checker (`recorder_test`) and the player (`sceneshot`, smoke OK). SHA-256:
  server.exe e3efadcd..., proxy 1ee4b4bb..., composite 8fd17453..., recorder_test 87b7de60..., sceneshot 6aca999b....
- **Recording** (c5h-g, Goldberg on F:, VT present, claimed by 0393): 3904's `recorder_live.py` on the product path
  (ServerRenderer = 1, D33 with Overlay on, `render_decals` / `render_shadow` / `render_ice` / `render_fow` 1), save copy
  C5H-0301-HUGE_0350 of c5h-g's C5H-VP8P-HUGE_0350 (c499d3c6...), `--after 900 --frames 8 --dump all`. In game 04:28:24;
  bundle `C5H-0301-HUGE_0350_20261116-042357_workstation`: format version 3, frames 1659-1788, 8 dumped at 2554 x 1411,
  `decals/load0003.dtap` 7,707,280 B, 8 shadow texel files, one ice mesh, the only version note the bake-cache gap;
  checker `scenebundle: OK`; restore by hash, `provision_verify_rc` 0, the save copy removed. LOUD lines: 0 in
  render.log and server.log, the proxy's 5 TEST ONLY lines of the recorder's keys. Logs kept in
  `F:/c5-64bit-work/0393/live-logs/`.
- **The load's generations** (render.log): generation 1 (9,132 decals) **with** the walk, 2 (15,721) **without** it, 3
  (17,712) with it again: two rule changes, as on 0364's load (there 2-3 without, 4-5 with). Generation 2: `tie rule
  changed (the EXE's walk -> the 4111 rule): 2861 of 2976 tiles with decals raised for it, 25.2 ms`; generation 3:
  `(the 4111 rule -> the EXE's walk): 2236 of 2998 ..., 39.8 ms` (generation 2's figure includes the tiles whose decals
  changed: 6,589 decals were added). The residency: generation 2 raised 2,861 tiles, of which **38 had a bake** (re-baked
  1.8 s after the raise); generation 3 raised 2,237, **48 had a bake** (2.4 s). So the rule's real re-bakes at this load
  were at most 86 tiles (with the content changes' own), about 0.17 s of render-thread time at 0365's 1.92 ms per bake.
- **Offline equals live**: this branch's sceneshot, `--dumped --compare --tolerance 0:0`: **colour and depth
  byte-equal in 8 of 8 frames (0 pixels differ)**, exit 0. **Teeth**: a diagnostic copy with the decal member's walk
  block switched off (0364's `nowalk.py`, a copy removed at the hand-back, never in the library) differs in 6,113-6,170
  pixels per frame (largest channel difference 72), exit 10: the live tiles in view carry the walk's order (in 0364's
  bundle the walk-off copy was the one equal to live).
- **The library**: 0364's huge bundle `C5H-0301-HUGE_0350_20261115-230946_workstation` moved to `retired/` (one
  RETIRED.log line, 04:41:09), the new one copied in. The standing check (`test_offline_equals_live.py`, 1:1) on the
  whole library and scenerun on the new bundle: 20.5.

### 20.5 The rule in play: how often the walk block goes, and the cost [L counts from 0365's logs, I cost]

**Corrected 2026-11-16 (ticket 0406):** the in-play flips below are gone. When the walk check misses while the snapshot looks intact and the published generation has the walk, the tap now holds the build and reads the batch again (at most `decal_walk_holds` = 4 times) instead of publishing without the block; two 30-turn huge runs had 0 tie-rule changes in play. The 2 changes at the load remain (option (b) below would remove them). See [terrain-decals-walk.md](terrain-decals-walk.md).

0365's eight 30-turn product-path arms on oldpc (huge t350, 19.4; `F:/c5-64bit-work/0365/oldpc-*/out/*/render.log`,
counted by `rule_changes.py` in `F:/c5-64bit-work/0393/work-archive/`): every arm had the load's two changes (generations 2 and 4); **in
play, 4 of the 8 arms had 1 or 2 generations without the walk block** (234-238 generations in play each), each followed by
one with it: 2 or 4 rule changes in play per 30 turns. Each in-play change now raises about 2,200 tiles, nearly all with
a bake (radius -1: every land tile is baked at the load), and compares for about 40-50 ms (workstation indication): a
pair of changes is about 4,400 re-bakes, **about 8.5 s of render-thread time at 1.92 ms per bake** [I: not measured in
play], spread over the idle slices (visible tiles first). Before 0393 those generations re-baked only their changed tiles
under the 4111 rule and left them so (stale against the walk that came back).
**Above the MC's marks** (1 ms per generation, 50 tiles per change; MC on 0393's thread 2026-11-16): the numbers go to the
MC for a quiet A/B ticket. What would cut the cost [I, not built]: (a) the tap keeps a generation's walk when only the
walk check missed (the tap's `walkMisses`, 18.4; why the index and the records disagree in those generations is not
traced, `shell/`), so the rule never changes in play; (b) the server keeps the walk's order through a walk-less generation (raise nothing on walk ->
none, and on none -> walk raise only the tiles baked meanwhile, which the residency, not the reader, knows).

### 20.6 Suites (workstation) [C, 2026-11-16 04:49-05:22]

`suites.sh`, `rerun.sh` (in `F:/c5-64bit-work/0393/work-archive/` with the drivers `live.sh`, `check.sh`, `exact.sh`, `swap.py`; every heavy step through a gate or `quietlock.py run`; logs
`work-archive/logs/`; the acceptance output `F:/c5-64bit-work/0393/acceptance.txt`):
- `render/build.py --test -R "decal|bakeresidency"`: plain **2/2 passed, 0 skipped** (`c5r_decal_test` with
  `tie_rule_change_leaves_no_stale_tile`, `tie_rule_change_random_sets`; its first run failed on a check of the test
  itself, the per-pass key list's length, fixed); `--asan`: `c5r_decal_test` **passed**, `c5r_bakeresidency_test` failed
  `rebake_same_tiles_10000_times_memory_flat` (settled private +24.9 MB against +22.6, the load-dependent reading of 15.5
  and `renderer-in-server-design.md` 4.15.1); rerun 1 failed `radius_zero_evicts_after_a_move` (resident against visible,
  gray frames: a timing check under load), rerun 2 **passed, 0 ASan reports**. The residency's code is not changed by 0393.
- `server/tests/render/decal/test_decal_tap.py` (`decal_tap_test.exe` built from this branch's reader and query): plain
  **13 passed**; MSVC `/fsanitize=address` **13 passed**, 0 ASan reports.
- `server/tests/render/test_decal_wiring.py` + `test_residency_source.py` (render_host_test of this branch, stand-in
  baker): **47 passed, 1 failed**: `test_unresolvable_name_is_an_install_failure[no_farm_types]` timed out waiting for
  `bake-ahead complete` (a loaded machine); the test alone twice: **3 passed** both times.
- Scenebundle: the standing check `test_offline_equals_live.py` on the library after the swap **8 passed** (7 bundles and
  the missing-bundle negative); scenerun of the new huge bundle exit 0, 8 shots byte-equal, 8 pictures in Visual Checks
  (issue 0393, DIAGNOSTIC, info).

## 21. A decal record past the map's east edge: left out, not a refusal (ticket 0649)

Ticket 0649 (2026-11-21, impl-0649, branch `task-0649-decal-seam-rect`). Found by ticket 0643 (queue #9417/#9418): the large
43-civ save `HI-0580-LARGE-M43-SCENE` (Large Communitu, 87 x 59 plots, T200) did not load on the installed 43-civ layout on
EXE build 26577625, twice: the composite's `decal_tap: LOUD: FAILED LAYOUT: batch 2 slot 22: rectangle (5578.25, 1505.4,
5567, 1564.42) or priority 0.7 not valid` (second run `(5586.6, 1499.88, 5567, 1568.62)`), so no first decal set, so the
server's install bound (12.x: no set within 20,000 ms after the load) ended the game with the D118 popup. No crash. The
same form was seen once before, in play, on a 180-column map (0491 run b2, `fog-of-war.md`: `(11551.7, 313.892, 11519,
327.875)`, 11519 = 180 x 64 - 1), where the last good set stayed and nothing was lost.

### 21.1 What the record is [C]

Read from a read-only headless decompile of build 4390913 (scratch copy of the analysed project, 2026-11-21; nothing of it
committed, D83), behaviour only:

- **AddDecal (0x49B5A0) makes the rectangle from the producer's vertices and clamps each side on its own.** It takes the
  minimum and maximum x and y over all the vertices the producer passed (1.2), then raises min x / min y to the world
  rectangle's min (TDS+0x6C / +0x70) when below it, and lowers max x / max y to the world's max (TDS+0x74 / +0x78) when
  above it. There is no clamp of min to the max side or of max to the min side. So a decal whose vertices **all** lie past
  the east edge keeps its own min x (past the edge) and gets max x = the world's max: **min x > max x, max x exactly the
  world's last sample** (87 x 64 - 1 = 5567 here). Past the west edge the mirror image: min x = the world's min (0), max x
  below it. The same holds in y for a decal wholly past the north or south edge (not seen in any census, 21.3). The
  rectangle with min > max is stored as is (+0x24..+0x30), passed as is to the area invalidation 0x4387D0, and the
  vertices are normalised against it (width and height negative: the positions still decode back to the producer's
  points). Its "large" test (a side > 256) sees a negative width, so such a decal is a small one in the spatial index,
  at its centre ((min + max) / 2, past the edge; the cell clamps to the last column).
- **The draw (0x742EF0) never takes it.** A taken item must have a rectangle that overlaps the tile's: rec.min x <= tile.max
  x and tile.min x <= rec.max x (18.3). The tile rectangle of the last column is [ox - 1, ox + 64], ox = (cols - 1) x 64, so
  no tile's max x passes world max + 1 (5568 here). A record whose min x lies more than one sample past the east edge
  (5578.25, 5586.6 and 11551.7 in the three sightings) fails that test for every tile and is never drawn; the same for one
  whose max x lies more than one sample before the west edge. Wrap: the picture past the seam is the wrap copy of column
  0's tiles (`terrain-bake-residency.md` "own frustum test per tile and wrap copy"), which the decals of column 0's own
  plots draw; a record past the east edge is not in any tile's bake.
- **Which decal.** Priority 0.7 with its own textures: a route curve (2.2, RoadLayout -> 0x5EC600 -> AddDecal): a road piece
  laid toward a neighbour across the wrap seam lies wholly past the east edge. The route layout is random per load (2.2;
  the two runs' rectangles differ: 5578.25 and 5586.6), which matters for 21.3.

So **leaving the record out equals the stock picture** [C for the draw's test; the picture check in 21.4]. Limit [I]: a
record whose min x lies in (world max, world max + 1] would pass the last column's overlap test and the EXE would draw its
few vertices in that one-sample band; it is left out too (no such record was seen).

### 21.2 The fix as built (shell/composite/decal_tap.cpp) [C]

- `OutsideX(rect, world min x, world max x)`: finite, y in order, min x > max x, and either (max x == world max x and min x >
  world max x) or (min x == world min x and max x < world min x), compared exactly against TDS+0x6C / +0x74 as read in the
  same poll. Such a live record is **left out of the set**: not in the records, the vertices, the content hashes or the
  changed rectangles; its slot is kept per batch (`Batch::outSlots`) and it still enters the safety net's pool hash (the
  net's walk of the list hashes every live record).
- **The walk** (18.4): an index item or large-list node that names a left-out record is passed over (no rank; the large
  list's ranks count only kept nodes), so the walk block stays exact for the rest; before, such an item would have been "not
  a record of the snapshot" and the generation would have gone out without the walk block.
- **The log**: one line per generation that leaves records out, `decal_tap: generation G leaves out N decal record(s)
  outside the world's x range 0 .. MAX (the EXE's draw never takes them); the first: batch B slot S, rectangle (...)`. Never a
  LOUD line, never per Present.
- **Unchanged**: every other invalid record still fails the layout check (FAILED LAYOUT, retried, the last good set kept):
  not finite, min > max in y (also the clamped form in y: no census has one, and the ticket keeps it a refusal), min > max
  in x without the clamped side exactly at the world's edge. The server's reader (`srv_decal_tap.cpp`) is not changed: the
  tap never publishes such a record, and an image that carries one is still refused there (`DREF_RECT`; test row
  `rect_seam_form`). The two readers agree on the rule "a published set never holds the clamped seam form". Accepting it in
  the server instead was tried and dropped (2026-11-21): `render/src/decal/decal_set.cpp` builds its per-tile grid only
  when every rectangle is in order (an inverted one leaves the set without a grid, so every `decalsForTile` would scan the
  whole set), and the delta path treats an inverted added rectangle as "rebuild whole". The install bound and its game end
  are unchanged.

### 21.3 The record on build 4390913, and why 0580 loaded the same save [L census; I for 0580's run]

**Census method** (2026-11-21, impl-0649, scratch `C:/c5-64bit-work/0649/census_inv.py` over `shell/tools/decal_probe.py`,
ReadProcessMemory only): a STOCK load (no patch: the instance's game DLL swapped by `provision.py set-dll` for a 32-bit
instance-aware DLL, restored afterwards) through `frontend.py prepare` / `cli.py launch` / `frontend.py dismiss`, then
censuses of every live record, each inverted rectangle with its vertices decoded (positions times the rectangle's sides
give the producer's points back).

- **Build 4390913, `HI-0580-LARGE-M43-SCENE`** (copy `C5H-0649-L43`, SHA-256 `96b54368...`; c5h-c, the 43-civ take's
  instance-aware DLL `i0ship-f33fc46a-m43-ia` `ba520010...`; 14:56-14:57 EST, three censuses 20 s apart, all equal):
  10,184 live decals in 8 batches, world (0, 0, 5567, 4031); **one** inverted record, **batch 2 slot 22** (the same slot
  as 0643's two runs on build 26577625), rectangle (5580.835, 1503.498, 5567, 1569.958), priority 0.7, 54 triangles,
  textures `RoadsandRails_new_D/H/SREF.dds` (a route curve), uploaded, pass 0, small (in the spatial index). Its 162
  vertices lie at x 5580.84 .. 5616.34, y 1503.50 .. 1569.96: **all past the east edge** (5567). Through the grid's
  h2w (x = 64 hx + 32 hy + 32, y = 64 hy + 128) that is row 22, the column after the last (87 = column 0 across the seam).
  Four other records end exactly at x = 5567 (min x inside the world, clamped normally: among them the neighbouring road
  piece (5535.4 .. 5567, 1471.2 .. 1506.2) and a tree shadow); **no record starts at x = 0**: nothing is drawn on the
  west side of the seam at that row, so stock shows that road ending at the seam, and the left-out record adds nothing
  there.
- So the record is **not new in build 26577625**: the old build makes it too, in the same slot, its x position random
  per load (5578.25, 5586.6 on 26577625; 5580.835 on 4390913).

**Why 0580 passed on 4390913 (2026-11-19, c5h-c, rc0b-m43, development layout)** [I; 0580's composite and server logs no
longer exist: the instance's `Logs` keep no run of 2026-11-19 19:21-19:28]: the tap's first set is read as soon as the
camera's grid is valid and the change signal settles, **while the EXE is still adding decals**. On build 4390913 the first
generation is regularly a partial one: every composite log on c5h-c (28 runs, 2026-11-14..21) has generation 1 at 57-85
decals on the Tiny saves (then 138, 301, ... as the rest arrive), 9,056 on a huge one; the first set installs and the 20 s
bound is met before the route curves are laid. A later read that meets the seam record fails the layout check **in play**,
which keeps the last good set (not fatal: 0491 run b2), so the decals laid after that read (here: every road and farm
added after the first partial set) were silently missing from the server's picture for the rest of such a game. In
0643's runs on build 26577625 the first read came 17-18 s after the grid became valid (run2: grid valid 14:00:19.9, first
read about 14:00:37, the whole set of the load already there), so the very first set held the seam record and failed:
no set at all, the bound ended the load. Whether the first read catches the seam record is a race between the tap's first
read and RoadLayout; the record itself is the same on both builds. (In 21.5's fixed run on build 26577625 the first set was
partial too, 3,110 of 10,170 decals, and already held the record: which decals a partial first set holds varies by load.) What would confirm it: one product-route load of this
save on build 4390913 with the unfixed composite (expected: a partial generation 1, then `FAILED LAYOUT ... batch 2 slot
22` in play). The fix removes the race's consequence on both builds.

### 21.4 How often: the saves at hand [L, 2026-11-21]

Stock loads, census in game (method 21.3), every inverted record counted; and the decal images of the offline scene
library.

| Build | Save (copy name) | Map | Live decals | Inverted records | Seam form |
|---|---|---|---|---|---|
| 4390913 | HI-0580-LARGE-M43-SCENE (C5H-0649-L43), 43-civ DLL | Large Communitu 87 x 59, wrap | 10,184 | 1 | 1 (east, batch 2 slot 22) |
| 4390913 | HI-4506-LARGE-SCENE (C5H-0649-L22, `c01bb2bf...`), the 22-civ release-large journey's save | 87 x 59, wrap | 7,899 | 0 | 0 |
| 4390913 | C5H-3310-HUGE_0350 (C5H-0649-HUGE_0350, `c499d3c6...`) | Huge 128 x 80 (world 8191 x 5375) | 17,741 | 0 | 0 |
| 4390913 | C5H-0599-TINY_0018 (C5H-0649-TINY_0018, `e337244c...`) | Tiny 56 x 36 (world 3583 x 2559) | 530 | 0 | 0 |
| 26577625 | HI-0580-LARGE-M43-SCENE (C5H-0649-L43), 43-civ DLL, c5h-new-b 16:02 | Large 87 x 59, wrap | 10,200 | 1 | 1 (east, batch 2 slot 22, (5577.478, 1505.724, 5567, 1570.193), 54 triangles) |
| 26577625 | 0643 runs 2 and 3 (composite log, installed 43-civ layout) | Large 87 x 59 | (refused) | 1 each | 1 each (batch 2 slot 22) |
| 4390913 | 0491 run b2, T1100 (composite log, in play) | 180 columns | | 1 | 1 (east) |

The 4390913 rows used c5h-c (14:56-15:22 EST), the instance-aware DLLs `i0ship-f33fc46a-m43-ia` (43-civ) and
`i0ship-e96457fd-ia` (the others). The scene library (`F:/c5-64bit-work/scene-library/bundles`, 16 decal images) holds no
inverted record (scratch `scan_dtap.py`): no bundle carries the form, none needs a rebuild (rule A). So it is a class (any
wrapped map where a route runs to the last column), seen on two maps, rare per save.

### 21.5 Live: the save loads on build 26577625 with the fix [L, 2026-11-21 16:06-16:15 EST]

Instance c5h-new-b (C:, Goldberg, mods form, Steam build 26577625, Vibrant Textures), player's state = the 5.4.8 tag's
43-civ DLL `01451bd7...` (`set-dll`), the rehearsal's 43-civ TEST installer (`VP64-Patch-0.1b-rehearsal-for-VP5.4.8-TEST.exe`,
`42497629...`; "install 0.1b-rehearsal (m43, i3, mods form)", exit 0), then the installed `shell_composite.dll`
(`db4bcd44...`) replaced by this branch's build (`9714fb42...`) and the `composite` line of `shell_composite.sha256`
pointed at it (both backed up by hash and put back before the removal). `HI-0580-LARGE-M43-SCENE` (copy) loaded through
`frontend.py`; in game after 159 s.

- Composite: `READY generation 1 (load 3, first copy): 3110 decals ...; walk block 2984 small + 126 large`, then
  `generation 1 leaves out 1 decal record(s) outside the world's x range 0 .. 5567 (the EXE's draw never takes them); the
  first: batch 2 slot 22, rectangle (5582.87, 1502.48, 5567, 1565.12)`; generation 2 (8,538 decals, walk none: a batch moved
  during the load, 0406's rule) and generation 3 (10,170 decals, walk block 10,044 + 126) each with the same one line. 0
  LOUD lines in the composite's log.
- Server: `decals: generation 1 applied: 3110 decals ...`, `set generation 1 (load 3) in use` 74.2 s into its log, within
  the install bound; the game went on (no D118 popup). Its two LOUD lines are unit pose programs (a swordsman, a warrior:
  `no pose program`), not decals.
- Pictures (Visual Checks, issue 0649, not in git): `vc-20261121-162511-workstation-38ab` (ACCEPTANCE, the installed layout
  with this composite, the wrap seam at plot (0, 22)) against `vc-20261121-162509-workstation-4ff5` (stock, the same camera,
  a stock load at 16:22 on the same instance). **The decals at the seam are equal**: the road along the north coast of the
  island at the seam, its curve down the coast and the road through the farms are drawn alike; no road piece is missing or
  extra where the left-out record would lie (farm field styles differ, random per load, 2.4) [L]. **Not decals, reported to
  the MC as a separate finding** [L, two product frames agree]: the city model of Salem (the city on the island at the seam)
  and several moai on the neighbouring island are absent from the product frame and present in stock.
  Notes for drivers [L]: `UI.LookAt(plot, 0)` (two arguments) did not move the camera on this save, `UI.LookAt(plot)` did,
  and in the stock session only after `cli.py pretend-active on` (before it the stock EXE presented no new frame while
  hidden, so every shot repeated the old view); a shot can come back without the interface or half rendered (no models,
  green water): look at every picture before recording it; `PushMission(MISSION_SLEEP, -1, -1, 0, 0, 1)` takes numbers
  only (booleans: "number expected"), as overlay-system.md 9.1; `shell/tools/decal_probe.py` reads the category-name array
  by an anchor that has no catalogue symbol (refused on build 26577625: run it there without the names).
- Removal: exit 0; the DLC tree identical to before (8,623 rows), My Games identical outside the run outputs but for the
  player DLL the test had set (put back by `set-dll --previous` to the instance's `ff0f6e86...`); `verify --quick` ok.
- A first try on c5h-new-mpk (the modpack form) could not load the save: it is a mods-form save ("CanLoadSavedGame = 4,
  the save needs mods that are not installed" in Lua.log); nothing was installed there.


