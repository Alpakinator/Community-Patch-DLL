# The ranged-attack arrow and the move-order path in the EXE: behaviour, shader, art, and the path calls

Written 2026-11-14 (19:28-) by the 3802 worker (epic 0179, task C1b, the static half of C1; branch
`task-3802-arrows-static`). Method: read-only headless Ghidra 12.1.4 on a copy of the analysed project
(`research/06-tooling-and-workflows/reverse-engineering-setup.md` section 2), the existing llvm-objdump listing of the DX11
EXE (SHA-256 `b2ce96ae...c88e`, build 4390913), constants read from the EXE file's `.rdata`/`.data`, the shader blobs
disassembled with the system `d3dcompiler_47.dll` (D3DDisassemble), a small symbolic evaluator of my own over the
listing's instruction stream for the mesh builder (`shell/evidence/0179/3802/symhead.py`), the DLL fork's source and Lua, the server's source,
and the Steam install's files on the workstation. **No game was run.** Everything here is described as behaviour in my own
words (D83): no decompiler output, listing or EXE bytes are in this file or in the repository. The script outputs stayed
in `C:/c5-64bit-work/0179/3802/` and were deleted at the end. Addresses are VAs at ImageBase 0x400000.

Marks: **[C]** read in the decompile, the disassembly, the EXE's data or the files; **[I]** inferred; **[L]** live, here
always 3801's stock RenderDoc capture of 2026-11-14 (c5h-c, 32-bit pin `i0ship-26234371-ia`, stock config, Tiny save copy
`C5H-0179-TINY_0009V` turn 9; VT present, workstation instance). The live half (stock RenderDoc capture, bridge records, P9) is 3801's, in
`research/05-graphics-composition/arrows-and-path-line.md`.

## 0. The premises, code side

| # | Premise (epic 0179 brief) | Answer | Mark |
|---|---|---|---|
| P2 | The arrow draws into the back buffer in the world pass with depth | **Holds.** Code: two passes called "Arrow Visualization", with no render target named in any of the four target slots, like the world pass's "Clear" pass. **3801's stock capture** (2026-11-14, section 1.7) shows both draws into the back buffer with D24S8 depth (test LESS_EQUAL, write on). | C + L (3801) |
| P3 | Under the shipped A'' form the EXE draws no arrow | **Fails, by 3801's live finding** (2026-11-14, `arrows-and-path-line.md`): under the shipped A'' form the Arrows view bit is kept, so pass B calls the arrow render and the EXE draws the arc. The
evidence is `vpproxy.log` on c5h-c at 19:47: "config 0xff640767 keep 0 clear 0x00200001: view 0xffffffff -> 0xff440766".
The proxy's rule is view &= config | 0xFF000000, minus Terrain/Waves (`proxy_shell.cpp` ClearBits). Arrows is on in
config.ini under A'', and `NEVER_RESTORE` only stops the proxy setting back bits it cleared itself. That led to the MC's route (b) for arrows (#1030). This row first assumed the brief's "view bit cleared by the proxy". Code side [C]: pass B 0x7492E0 calls the arrow render only under the Arrows view bit 0x400000. Pass A (0x5893F0) calls the arrow render **and both MovementPath calls** under the Overlay bit 4, which is on under D33. Pass A is picked only when `[GameView+0x1B8] & 0xF` is non-zero (0x66CE00). That field is copied from `[app+0x690]` only when that value's low two bits are set (0x4B011A). The app constructor sets `[app+0x690]` to 0xE0 (low nibble 0); the only other writer is a settings read at 0x4CBD15 (a "%s:%d" radio-group key, meaning unknown). Pass A also has a countdown of 950 frames and three frame counters by mode (+0x884..+0x894), which looks like a benchmark or test-render mode [I]. So in normal play only pass B draws: one arrow, not two (risk 5 does not bite). If pass A ever ran with Overlay on, it would draw the arrow and, if MovementPath were started, the path models. | L (3801) for the pass-B arc; C code, I pass A's trigger |
| P4 | The disc and chevrons are drawn by MovementPath's render, not by UI Lua | **Holds.** MovementPath's render 0x519B90 is the last draw of world pass B ("Movement Path", plus an optional "God Ray"). The markers are its model instances (section 2). The Lua files only raise the events. | C |
| P5 | The disc and chevrons are flat textured geometry, not Granny models | **FAILS (risk 1).** MovementPath loads 18 `.fxsxml` assets from `assets/UI/Overlays/`. Each names a Granny mesh (`.gr2`) and five textures; six of them add a Granny animation. It also loads `assets/Effects/Level_Up/FXM_God_Ray.fxsxml`. It poses every instance through the shared Granny pose pool 0x5F63958 and draws it through the engine's model path. Reported at once to the WMC and the lead, 2026-11-14 19:3x. **Softening, from 3801's capture:** the drawn meshes are **rigid**. Each is an immutable 32-byte-stride VB of POSITION, NORMAL and TEXCOORD0 with no bone weights, drawn with **UIShader (material 81)**: VS `0x067FC9E8`, PS `0x067FEBA0`, matched by SHA-256 to the captured blobs (`7ce2cf44...`, `7d85ec9f...`). Its constants are a per-object world matrix (cb0) and view-projection (cb1), with three textures. So the server needs a `.gr2` mesh reader (D34) and each instance's world matrix (section 2.3), not a skinning path. The SelectedAni animation plays as a transform per instance (section 2.4). | C + L (3801) |
| P6 | Under D33 the EXE still calls the path functions when the player previews a path | **Holds in code.** The calls come from the EXE's interface object (InterfaceBuddy), not from MovementPath. MovementPath only listens to the result (section 3). So turning MovementPath off removes no path call. | C (I: live counts are 3801's) |
| P7 | The node answers are served on a server thread where a copy reads no DLL object beyond the answers | **Holds in code**, and is not used by epic 0179 since the path took route (b) (#1009): a pointer for ticket 0300 is in section 4. | C (I: channel) |
| P10 | `ArrowSettings.xml`, the arrow art and the path art resolve from the install | `ArrowSettings.xml` and the path's 18 `.fxsxml` + `.gr2` files are **loose only** (`Assets/UI/Overlays/`, in no `.fpk` of `Resource/`). The path's textures are in `Resource/DX9/UITextures.fpk` (`CubeLight_Generic_32.dds` is also in several model packs), so the pack-over-loose rule of `asset-pipeline.md` 1.2 decides those. **The arrow has no texture at all.** Vibrant Textures touches none of these files. | C (files) |

**The MC's rulings during this task (2026-11-14):** the path's models take route (b) until increment 3 (#1009), and the
arrows take route (b) too (#1030). The EXE keeps drawing both in increment 2. The server arc is follow-up ticket **0199**;
the path models drawn by the server are ticket **0300** (increment 3, with 0041) (MC #1038). Sections 1 and 2 are written as
the starting point for those two tickets.

Decision 6 (arc builder: re-implement or call the EXE's), for 0199: see section 1.8. Proposal: **(i) re-implement**. The
builder needs only the two plot positions, the ground height at the start plot, nine settings numbers, the view frustum
and the frame time. The server has all of them.

## 1. The Arrow system (`Arrow_System`, object 0xB9C418, `.\App\Graphics\ArrowVisSystem.cpp`)

### 1.1 App start-up (0x526850, gated by config bit 0x400000 at 0x476359) [C]

- Reserves room for 1024 vertices of 40 bytes (object +0x2A8, capacity +0x2B0) and 1024 32-bit indices (+0x2B4,
  capacity +0x2BC). The vertices are a CPU-side vector rebuilt by the mesh builder.
- Creates a render batch (+0x74) and the "ArrowShader" program (+0x78, 0x705440). The program has material 0 and one
  shared constant buffer (id 38, 0x60 bytes, `programs.txt` of 2903).
- Subscribes three engine events (0x671150): spawn 0x79AF20, remove-all 0x69A840 and a no-op 0x553BE0.
- Reads `assets/UI/Overlays/ArrowSettings.xml` (0x681470; table 1.5). If the file cannot be read, the EXE shows a message
  box "Could not load arrow xml file". The same reader runs again from the per-frame update when a file-change watcher
  (+0x7C) is armed. That is the developer reload, which normal play never arms.
- Appends **one arrow record** to the list (+0x34 data, +0x38 count; records of 0x30 bytes). It is created hidden. It
  owns a dynamic vertex buffer of 4000 vertices (vertex format 5) and a dynamic index buffer of 3000 **32-bit** indices
  (`R32_UINT`). This is the only arrow the game ever has: the spawn handler overwrites it (1.2).

Arrow record (0x30 bytes): +0x00 start (x, y, z), +0x0C end (x, y, z), +0x18 "rebuild" flag, +0x19 "growing" flag, +0x1A
"hidden" flag, +0x1C progress (0..0.9999), +0x20 growth rate, +0x24 the draw object, +0x28 the vertex buffer, +0x2C the
index buffer.

### 1.2 Spawn (0x79AF20; Lua `Events.SpawnArrowEvent(x0, y0, x1, y1)`, raised by CP `Bombardment.lua`) [C]

The engine event carries two integer triples, one per plot. The handler does this:

1. Turns each triple into a world position with the EXE's plot-to-world function 0x6851C0, the one plot centres use. It
   transforms the triple by the grid matrix at 0xD91030, then scales by the grid's (sx, sy) and subtracts its offsets. The
   z of that position is the **height field at the plot centre** (HeightAtGrid 0x6D8B60; `world-systems-and-stubs.md`
   7). The triple is the plot's hex coordinate (x - (y >> 1), y), and the height is read at the sample under the centre
   with both sample coordinates truncated [C, corrected 2026-11-16 by 0199: plots (50, 14) and (49, 16) through the Tiny
   map's grid give 0179's captured start and end to 1e-5, z 17.95294 = the truncated sample, the rounded one gives 17.57;
   `research/05-graphics-composition/arrows-and-path-line.md` 12.3. Replaced: "[I: the triple is the plot's hex
   coordinate ...; the binding was not traced]"].
2. **Wrap copy of the start:** of the start's x, x − W and x + W (W = the world width, `[0xD90FE4]`), it keeps the last
   candidate in the order −W, 0, +W that lies inside all six view-frustum planes (0xDA1510, 16 bytes each), with a margin
   of 64 world units (each plane distance ≥ −64). If none does, it keeps x.
3. **Wrap copy of the end:** it compares the squared distance in the xy plane from the chosen start to the end at
   x1 − W, x1 and x1 + W. It keeps x1 if x1 is nearer than x1 − W, then x1 + W if that is nearer still. Quirk: x1 − W itself
   is never chosen. When it is the nearest, x1 stays.
4. Writes the one arrow record (0x5FE500). The start is (chosen x, y, ground height). The end is (chosen x, y,
   **`target_height`**): the end's own ground height is not used, and the arrow ends at the fixed height 20 from the
   settings, an absolute world z [C; corrected 2026-11-16 by 0199: the end fitted from 0179's captured ring centres
   has z 19.99999, arrows-and-path-line.md 12.3. Replaced: "I: an absolute world z; C3 checks it ..."]. The handler sets rebuild = 1, growing = 1, hidden = 0, progress = 0 and rate = 0,
   and keeps the record's buffers and draw object.

### 1.3 Remove all (0x69A840; Lua `Events.RemoveAllArrowsEvent()`) [C]

Sets the hidden flag of the first (only) record. Nothing is freed; the next spawn reuses the buffers.

### 1.4 Per frame (update 0x643A60, called from the per-frame update 0x6FBCA3 under view bit 0x400000, with the frame time dt in seconds) [C]

For every record that is not hidden:

- if the rebuild flag is set, the mesh builder runs;
- if the growing flag is set: rate = 1 / `anim_speed`; progress += rate × dt while progress < 0.9999, clamped to
  0.9999. When progress reaches 0.9999 growing stops. The mesh builder runs again, so it can run twice in one frame.

So the arc **grows from the attacker to the target in `anim_speed` = 0.5 s**, then stays still until the next spawn.
Comparisons taken during the first half second after a spawn depend on the phase (risk 4 of the brief). No random stream
is used. The time base is the frame time the update receives.

### 1.5 `ArrowSettings.xml` (the game's file, read from the install at run time; not committed)

Workstation Steam install, `Assets/UI/Overlays/ArrowSettings.xml`, 433 bytes, SHA-256
`2bcbad95c5b4743e28b3f92282582f381af2b063c3837bf41219a0881110ccdd`. It is one `<Arrow>` element with attributes:

| Field | Value | Read into (object offset) | Used by |
|---|---|---|---|
| `min_width` | 3 | +0x04 | width at the start of the curve |
| `max_width` | 5 | +0x08 | width at the end of the curve |
| `width2` | 1.5 | +0x0C | cross-section (second width) |
| `outline_width` | 0.65 | +0x10 | outline added to the width, and the head's stop distance |
| `outline_width2` | 0.65 | +0x14 | outline added to `width2` |
| `arc_height` | 75 | +0x18 | z added to the two inner control points |
| `target_height` | 20 | +0x1C | z of the end point |
| `anim_speed` | 0.5 | +0x20 | growth time in seconds (rate = 1 / value) |
| `alpha` | 0.5 | +0x24 | shader constant: the output alpha |
| `ambient_light_level` | 0.7 | +0x28 | shader constant: the ambient term |
| `body_color` | 255,0,0 | +0x2C | packed as A=0xFF, then B, G, R from the high byte down (0xFF0000FF for red) |
| `outline_color` | 255,255,255 | +0x30 | packed the same way |

The fields are read by name; a missing field leaves the object's value (0 in the static image) [I]. The colours are
written as 0xAABBGGRR, and the vertex colour format is **R8G8B8A8_UNORM** (3801's capture: COLOR0 255,0,0,255 on body
vertices) [L].

### 1.6 The mesh builder (0x4D9D30) [C unless marked]

Inputs: the record (start P0, end P3, progress t), the settings. The builder clears both CPU vectors and rebuilds the whole
mesh every time it runs.

**The curve.** Four control points:
P0 = start; P1 = 0.75·P0 + 0.25·P3; P2 = 0.25·P0 + 0.75·P3; P3 = end. P1 and P2 get `arc_height` (75) added to z.
The curve is a **clamped (open uniform) quadratic B-spline** over these four points, knots (0, 0, 0, 1, 2, 2, 2),
evaluated at u = 2s for s ∈ [0, 1] (0x751FC0). With f = u − ⌊u⌋:

- u in [0, 1): point = (1−f)²·P0 + (2f − 1.5f²)·P1 + 0.5f²·P2;
- u in [1, 2): point = 0.5(1−f)²·P1 + (0.5 + f − 1.5f²)·P2 + f²·P3; at u = 2 this is P3.

The tangent (0x78EC40) is the derivative of the same pieces with respect to f, normalised:
(2f−2, 2−3f, f) on [0, 1) and (f−1, 1−3f, 2f) on [1, 2). These six weight tables are constants of `.data`
0xB978E8..0xB9799B [C].

**Sampling and width.** The body is swept in steps of **0.04** in s (0xA05838), from s = 0 while s < t + 0.04 − 0.001, with
s clamped to t. The early stop below ends it sooner. At each sample s:

- **width** w = s·(`max_width` − `min_width`) + `min_width` (3 at the attacker, 5 at the target);
- **frame** [C]: T = normalise(tangent at u = 2s); S0 = normalise(T.y, −T.x, 0) (horizontal, = T × up); N =
  normalise(S0 × T) = normalise(−T.x·T.z, −T.y·T.z, T.x² + T.y²);
- **roll of S only** [C]: (dx, dy) = the normalised 2-D (T.x, T.y). If dx + 0·dy > 0.867 or < −0.867 (doubles at
  0xA0B4D0 / 0x9F9078), S = S0 rotated about T by the angle (dx + 0·dy) × 0.549762 rad. Otherwise, if dy + 0·dx > 0,
  by (dx + 0·dy) × (−0.549762). Otherwise S = S0. (D3DXMatrixRotationAxis, then D3DXVec3TransformCoord; N is **not**
  rotated, so S and N need not stay orthogonal.)
- **16 vertices** [C, decoded symbolically from the instruction stream, 2026-11-14; checked against 3801's capture,
  below]. Let C = the curve point, W = w + `outline_width`, H = `width2` + `outline_width2`. The positions are evaluated
  as (C ± w·S) ± `width2`·N; the minus signs are additions of negated products, so the bits are the same.

  | v | position | normal | COLOR0 | COLOR1 |
  |---|---|---|---|---|
  | 0 | C − w·S + width2·N | N | body | 0xFFFFFFFF |
  | 1 | C + w·S + width2·N | N | body | 0xFFFFFFFF |
  | 2 | C + w·S + width2·N | S | body | 0xFFFFFFFF |
  | 3 | C + w·S − width2·N | S | body | 0xFFFFFFFF |
  | 4 | C + w·S − width2·N | −N | body | 0xFFFFFFFF |
  | 5 | C − w·S − width2·N | −N | body | 0xFFFFFFFF |
  | 6 | C − w·S − width2·N | −S | body | 0xFFFFFFFF |
  | 7 | C − w·S + width2·N | −S | body | 0xFFFFFFFF |
  | 8, 15 | C − W·S + H·N | 0 | outline | 0 |
  | 9, 10 | C + W·S + H·N | 0 | outline | 0 |
  | 11, 12 | C + W·S − H·N | 0 | outline | 0 |
  | 13, 14 | C − W·S − H·N | 0 | outline | 0 |

  So the body is a **box of half-width w and half-height `width2`** with one normal per face (top, right, bottom, left).
  The outline is a larger box around it (wider by `outline_width`, higher by `outline_width2`) with its winding reversed:
  an inverted hull. With cull FRONT (1.7) only its far side draws, as a rim around the body [I: the visual reading].
- from the second sample on, **48 indices** join this ring (first vertex q) to the previous one (p), per pair (a, b) =
  (0,1), (2,3), (4,5), (6,7): (p+a, q+a, p+b), (p+b, q+a, q+b); per pair (8,9), (10,11), (12,13), (14,15): (p+a, p+b,
  q+a), (p+b, q+b, q+a) [C]. The capture's index list begins 0, 16, 1, 1, 16, 17 [L].
- **Early stop** [C]: after a ring, if |P3 − C| ≤ 2·√3·w + `outline_width` (the double 3.0 at 0x9F2230, √ by the FPU), the
  body ends at this s, the growing flag is cleared, and s_end = s. In 3801's frame the body stopped at s = 0.96: 25 rings,
  then head width w = 4.92 [L, consistent with the head length below].

**The head** [C, decoded; checked against the capture]. It is built at s_end (t if no early stop) with its own frame from
u = 2·s_end, taken exactly as for a ring: C, T, S (rolled), N, and w = w(s_end). Let L = 2·√3·w (the height of an
equilateral triangle of side 4w), O = `outline_width`, H as above.

- **Block 1, 16 vertices (a collar):** vertices 0-7 = the ring table's body vertices 0-7 at C; vertices 8-15 = the same
  eight moved by O·T. Indices (24): for pairs (a, b) = (0,1), (2,3), (4,5), (6,7): (a, a+8, b), (b, a+8, b+8), counted from
  the block's first vertex.
- **Block 2, 26 vertices (the body's triangular plate**, base at C + O·T, tip at C + (O + L)·T, thickness 2·`width2`).
  Base points B(k) = C + O·T + k·w·S for k = −2, −1, +1, +2; tip A = C + (O + L)·T. ±h means ±`width2`·N:
  0-3 = B(−2), B(−1), B(+1), B(+2) at +h, normal N; 4, 5 = B(+2) at +h and at −h, normal M1; 6-9 = B(+2), B(+1),
  B(−1), B(−2) at −h, normal −N; 10, 11 = B(−2) at −h and at +h, normal M2; 12 = A + h (normal N); 13, 14 = A + h, A −
  h (normal M1); 15 = A − h (normal −N); 16, 17 = A − h, A + h (normal M2); 18-21 = B(−2), B(−1), B(+1), B(+2) at −h,
  and 22-25 = the same at +h, all with normal −T (the back plate). M1 and M2 are the two slanted sides' normals, each normalised in place (0x6D4C20):
  M1 = normalise(N × (L·T − 2w·S)), M2 = normalise((L·T + 2w·S) × N), the normals of the faces through B(+2), A and
  B(−2), A spanned with N [C, corrected 2026-11-16 by 0199: they match v420 and v426 of 0179's capture to 1e-5, T
  component −0.486; arrows-and-path-line.md 12.2. Replaced: "[I: their exact formula is not trusted ... 0199 takes them
  from the capture]"].
  Indices (42): (0,12,1), (1,12,2), (2,12,3); (4,13,5), (5,13,14); (6,15,7), (7,15,8), (8,15,9); (10,16,11), (11,16,17);
  (18,22,19), (19,22,23), (20,24,21), (21,24,25).
- **Block 3, 26 vertices (the head's outline)**, normal 0, COLOR0 = outline, COLOR1 = 0. Base points at C (no O shift):
  C + k·w·S for k = ±1, and C ± (2w + O)·S for the outer corners; tip A' = C + (L + √3·O)·T; ±H·N. The layout follows
  block 2's order. The indices are block 2's with the last two vertices of each triangle swapped (reversed winding).

Checks against 3801's capture (frame2846, draw 476) [L]: v400 = v384 (the collar starts on the last ring); v408 − v400 =
0.65·T; v416 − v408 = −w·S with |…| = 4.92; v419 − v416 = 4w·S; v428 (the tip, A + h) = midpoint(v416, v419) + 17.043·T,
where 17.043 = 2√3 × 4.92; v454 − v428 = (√3 − 1)·O·T + `outline_width2`·N to 3 decimals; v442 − v400 = −(w + O)·S +
`outline_width2`·N; v434's normal = −T. Each difference matches to the capture's printed precision.

**Upload.** If any index was written, the builder copies the vertex vector (count × 40 bytes) and the index vector
(count × 4 bytes) into the record's buffers (map, copy, unmap). It replaces the draw object with a new one of **material 0
(ArrowShader), with no texture (texture count 0)**, the record's index and vertex buffers, and primitive count =
indices / 3 [I: a triangle list]. It then clears the rebuild flag.

**Vertex (40 bytes).** Layout from 3801's capture [L]: POSITION R32G32B32_FLOAT @0, NORMAL R32G32B32_FLOAT @12, COLOR0
R8G8B8A8_UNORM @24, COLOR1 R8G8B8A8_UNORM @28, TEXCOORD0 R32G32_FLOAT @32. The builder never writes TEXCOORD0 and the
shader does not read it (the capture shows garbage there), so a re-implementation may write 0. The VS input signature
[C] is POSITION (xyzw, w = 1 from the 3-float layout), NORMAL, COLOR0, COLOR1. The VS blob's vertex-format mask is 0xC3
(`create_sites_table.inc`).

**Counts at full growth, from the capture [L]** (archer (50,14) to (49,16), Tiny save `C5H-0179-TINY_0009V` t9): 468
vertices = **25 rings × 16 (v0-v399) + a 68-vertex head (v400-v467)**; 1260 indices = 24 ring joins × 48 + 108 for the
head (the 108 matches the head's index write sites [C]). In each ring the first 8 vertices are body (COLOR0 red, COLOR1
white, normals set) and the last 8 are outline (COLOR0 white, COLOR1 0, normal 0). The index list starts 0, 16, 1, 1, 16,
17, ...: quads between vertex k of one ring and vertex k of the next. Ring starts on that capture: v0 = (1438.505,
−224.924, 18.856), v16 = (1435.874, −220.521, 30.532), v32 = (1433.240, −216.112, 41.258).

### 1.7 The render (0x4E5590) and ArrowShader [C]

- Called from world pass B 0x7492E0 after the Terrain render and the 0x200000 and 0x20 systems, before the 0x80, City,
  Unit and later systems, under view bit 0x400000. It is also called from pass A under the Overlay bit (P3).
- It resets its batch, then fills the shared constant buffer id 38 (0x60 bytes): rows 0-3 = View × Projection (the
  globals 0xDA1310 × 0xDA12D0, row vectors), row 4.x = `alpha`, row 5.x = `ambient_light_level`.
- **Two passes "Arrow Visualization"**, state presets 2 then 8, no render target named (the current frame target). In
  each pass it submits the draw object of every arrow that is not hidden, with ArrowShader. So the same geometry is drawn
  twice. The states, from 3801's capture [L]: both draws go to the back buffer (R8G8B8A8_UNORM) + D24S8, depth test
  LESS_EQUAL, depth write on, cull FRONT with FrontCounterClockwise = true, no stencil, no SRV, no sampler.
  **Draw 1 (preset 2): blend off, colour write mask alpha only.** In effect a depth (and alpha) pre-pass.
  **Draw 2 (preset 8): blend SRC_ALPHA / INV_SRC_ALPHA (ADD), alpha ONE / ONE, mask all.** Because draw 1 already wrote
  depth, draw 2 shows only the nearest surface of the half-transparent arrow: the ribbon does not show its own back.
  The blobs bound in the capture have the SHA-256 of the SM5 blobs below (`9c5bf893...`, `2525c4ee...`) [C + L].
- **ArrowShader** = material 0, one pass, no named texture slots (`slot_names.txt`). SM5 blobs: VS `0x0619B000` (680 B),
  PS `0x0619B7F8` (560 B); 4_1: VS `0x0619B2A8`, PS `0x0619BA28`; 4_0: VS `0x0619B550`, PS `0x0619BC58`
  (`render/src/shaders/create_sites_table.inc`). c5r reads them from the EXE file at run time with
  `c5r::shaders::ShaderSection`, as the terrain and overlay work do (`terrain-surface.md` 2, `overlay-system.md` 10).
  - VS: clip position = position × the 4×4 matrix in constant buffer slot 1 (rows 0-3). It passes normal, COLOR0 and COLOR1
    through.
  - PS (described): L = saturate(dot(normal, (0.57735, −0.57735, 0.57735))), a fixed light from the direction
    (+x, −y, +z). Then rgb = COLOR0.rgb × (COLOR1.x × L + (1 − COLOR1.x) + `ambient_light_level`) and alpha =
    `alpha`. So body vertices (COLOR1.x = 1) are lit plus ambient, and outline vertices (COLOR1.x = 0) are colour ×
    (1 + ambient). The output is half-transparent (alpha 0.5), and the blend preset decides how it is composed.

### 1.8 For ticket 0199 (the arc in c5r), and decision 6

Sections 1.2-1.7 give everything 0199 needs to build the arc from this description (D83): the end points (1.2, with the
server's own plot-centre function and HeightAtGrid), the curve, the sampling, the ring and head tables, the growth clock,
the shader, its constants and the two passes' states. The slanted head normals M1/M2, open here before, are settled in 1.6 (0199, 2026-11-16). 3801's capture
(`C:/c5-64bit-work/0179/3801/captures/*frame2846.rdc`, archived by 3801) is the reference for a vertex-for-vertex
comparison. Its inputs are the archer at (50,14), the target at (49,16) and progress 0.9999 (the head was built at the
early-stop sample s = 0.96).

**Decision 6 proposal: (i) re-implement.** The builder is self-contained. It reads no game object and calls no DLL
function. Besides the settings it needs only the two plot positions, the height at the start plot and the frame time.
The server has the height field (R4) and the plot geometry. The wrap-copy choice (1.2) needs the view frustum, which the
server's camera has. Calling the EXE's builder by address would also need the EXE's arrow record and its renderer objects.

## 2. The MovementPath system (`cvMovementPathVisSystem`, object 0xDAB490; bit 0x800000; the base for ticket 0300)

### 2.1 Start-up and assets (app start-up 0x595690 at gate 0x4763DA; asset load 0x4B5580) [C]

0x595690 creates a 4-target batch, a 32-byte × 1024 and two 4096-byte × 512 K buffer sets with mapped CPU copies, and a
blend state for the god ray (0x8BCDE0). It then loads the assets: for each of 18 names in a table at the object's start,
it loads the `.fxsxml` and builds the model ("Could not load / build %s, in cvMovementPathVisSystem"). It then loads
`assets/Effects/Level_Up/FXM_God_Ray.fxsxml` and marks its emitters of kinds 0x36-0x39. For each of the 18 it creates a
model instance and poses it once through the **pose pool 0x5F63958 / 0x5F639D8 / 0x5F63A58** (Granny local pose, world
pose, DAG cache; `world-systems-and-stubs.md` 6). It needs no Unit-system object beyond the pool [C for the functions
read; I for deeper callees]. With Unit on (increment 2) the pool exists; that is why MovementPath must be off when Unit is
absent.

Asset index (the instance kind used in 2.3), all under `assets/UI/Overlays/`:

| Index | Asset | Index | Asset | Index | Asset |
|---|---|---|---|---|---|
| 0 | CenterOneTurn | 6 | SelectedAniOneTurn | 12 | ConnectorOneTurn |
| 1 | CenterTwoTurns | 7 | SelectedAniTwoTurns | 13 | ConnectorTwoTurns |
| 2 | CenterThreeTurns | 8 | SelectedAniThreeTurns | 14 | ConnectorThreeTurns |
| 3 | CenterInaccessible | 9 | SelectedAniInaccessible | 15 | ConnectorInaccessible |
| 4 | CenterEmbark | 10 | SelectedAniEmbark | 16 | ConnectorEmbark |
| 5 | CenterAttack | 11 | SelectedAniAttack | 17 | ConnectorAttack |

[C: string order in `.rdata` 0xA33608..0xA33908 matches the index order used by the placement code.] Each `.fxsxml`
names one `.gr2` mesh and five textures. Center\*, for example, uses `CenterOneTurnFrameSpec`, `CenterOneTurnSpec`, a
`Color*.dds` (ColorRed for attack, ColorThreeTurns for one turn), `CubeLight_Generic_32` and `centeroneturnframe`.
SelectedAni\* add an `<Animation file="SelectedAni*_SelectedAni*.gr2" ec="1000">`. The `.fxsxml` names **no shader**.
The capture shows the engine drawing every path mesh with **UIShader, material 81** (VS `0x067FC9E8`, PS `0x067FEBA0`
at SM5; the PS bytes equal UIShader_Skinned's PS `0x067FF8B8`) [C hash match + L, 3801].

### 2.2 Events and the path data [C]

- StartupGame 0x5B67E0 subscribes two engine events (shutdown 0x74BC90 removes them): **path update** 0x55BF60 and
  **show/hide** 0x734D90.
- **Path update:** the payload is a 16-bit node count and up to 64 nodes of three 16-bit values (x, y, turn), 386 bytes.
  The handler only stores it at +0x5D00 (0x415E00). It is the queue record of type 0x51 that the path query writes
  (section 3).
- **Show/hide** (a bool; Lua `Events.DisplayMovementIndicator(bShow)`, raised by CP `InGame.lua` and VP `WorldView.lua`
  after each `UIPathFinderUpdate`): handler 0x5F8B50. It always clears the instance list (up to 256 instances of
  {asset index, 4×4 world matrix}, 0x44 bytes each, at +0x1900, count +0x5E84). On show it rebuilds the list from the
  stored nodes (2.3). So **stock refreshes the markers only when Lua raises `DisplayMovementIndicator(true)`**, not when
  the path record arrives.

### 2.3 The placement rule (0x5F8B50) [C unless marked]

> Corrected 2026-11-16 (ticket 0300, matched against 3801's frame6496 capture, all 54 draws) [C capture]: node 2 is scaled 1.5; the chevron spacing is (B - A) / 7 per step; the chevron frame's first row is the sideways axis. Details in `research/05-graphics-composition/path-models.md` sections 3.2-3.3; where this section says otherwise, that file is current.

For each stored node i (x, y, turn):

- **Position:** the plot centre in world space, with z = the height field (0x6851C0, as in 1.2). If z is below the floor
  at +0x5E90, or the plot's `GetActiveFogOfWarMode()` is 1 (revealed, not visible), or the plot is missing, z = the floor
  [I: the floor is the water level]. Only a translation; no rotation.
- **Scale:** 1.5 if i is the last node, or if the next node's turn is greater than this node's turn. That is the end of a
  turn's move, the "turn marker". Otherwise 0.75.
- **Node 0** (the unit's plot): one instance of asset 0 (CenterOneTurn) with no scale.
- **The last node:** if the plot holds an attack target (0x48B290: an enemy unit or city, through the DLL), asset 5
  (CenterAttack, scaled) and asset 11 (SelectedAniAttack, unscaled), connector kind 17. Otherwise it falls through to
  the middle-node rule, with "last" set.
- **Middle nodes:** if 0x768E50 holds for (this node, previous node), or failing that 0x5DEA50 does [I: embark and
  disembark], asset 4 (CenterEmbark, scaled) and asset 10 (SelectedAniEmbark), connector kind 16. Otherwise it decides by
  the node's turn value: 1 gives asset 0 (+ asset 6 if last), connector 12; 2 gives asset 1 (+ 7 if last), connector 13;
  anything else gives asset 2 (+ 8 if last), connector 14. The SelectedAni instance is added only on the last node.
- **Connectors (the chevrons)** 0x499FC0, between the previous node's position A and this node's B: n = 6 instances (3 on
  the last segment when it ends in a destination marker) at A + k·(B − A)/(n + 1), k = 1..n. Each is oriented with its
  first axis along normalise(B − A), its second along normalise((B − A).y, −(B − A).x, 0) and up = z [I: the axis order
  of the matrix rows].
- **No path** (count 0, show): a single marker at the mouse plot. Assets 4 + 10 if 0x6182B0 holds (embark), 5 + 11 if
  attack, else 3 + 9 (Inaccessible).

**VP note [I, from the DLL source]:** the node's turn value is VP's `iTurnNumber`. In VP this is turns × 100 + 10 ×
moves + moves left, negative on a stop node, or turns × 600 + moves with `MOD_UI_DISPLAY_PRECISE_MOVEMENT_POINTS`
(`CvDllContext.cpp` `TEMPCalculatePathFinderUpdates`). The EXE stores it as 16 bits and compares it with 1 and 2, so under
VP stock almost always shows the **ThreeTurns** variants. The scale rule ("next turn greater") still separates the turns.
3801's served nodes for a 3-plot preview were (50,13,22), (51,14,21), (52,14,120), (52,13,121), (53,13,220) [L]. By the
rule: node 0 gets CenterOneTurn, nodes 1-4 get CenterThreeTurns with scale 1.5 on nodes 1, 3 and 4 and 0.75 on node 2,
4 connector segments of 6 + 6 + 6 + 3 ConnectorThreeTurns, and SelectedAniThreeTurns on the last node. The capture's
tally agrees on the counts [L, 3801]: 5 discs (one with another t0: the start node), 21 connector instances, one
destination model. The variant textures are not yet checked.

The placement makes DLL calls of its own on the EXE's plot objects: `GetActiveFogOfWarMode`, and the attack and embark
tests. These are calls that route (b) (MovementPath on) adds to the served traffic.

### 2.4 Draw [C]

- Per frame: 0x42C510 adds dt to two clocks (+0x5E88, the animation time given to the SelectedAni poses; +0x5E8C, a
  shader time, divided by +0x5F24 when bound).
- Pre-pass 0x70BE30, called in world pass B before the "Clear" pass under bit 0x800000 ("Draw Movement Path"). It
  re-poses every instance through the pose pool with its world matrix. It sorts each instance into one of **two lists**
  by which of two screen-space x/y boxes holds its position [I: the two wrap halves of the view]. It records the model
  draws, and the god ray if armed (+0x5F20, at +0x5EF0).
- Render 0x519B90, the **last call of world pass B** under 0x800000: for each of the two lists, a pass "Movement Path"
  (presets 8, 4, 2, no render target named, so the frame target) that draws the list's records. Then "God Ray" with its
  own blend state when armed.
- Pass A calls both under the Overlay bit 4 (P3).
- From 3801's capture of the same preview [L]: 54 draws, all UIShader, into the back buffer + D24S8 (test LESS_EQUAL,
  write on), cull BACK, blend SRC_ALPHA / INV_SRC_ALPHA. Each mesh is an immutable VB of stride 32 (POSITION, NORMAL,
  TEXCOORD0; **no bone weights**) with an R16 index buffer. cb0 = the instance's world matrix (rows 0-3; translation for
  example (1471.9, −277.06, 19.52)), cb1 = view-projection (128 B). Textures t0 128×128, t1 128×128, t2 32×32. Per kind:
  disc 1056 indices ×5, an inner part 384 ×5, connector 6 + 90 indices ×21, destination 576 + 1392 once. No god-ray
  draw in that frame.
- So the markers are rigid meshes placed by a matrix. Drawing them in the server needs: the `.gr2` mesh data (a reader
  built from source under D34), the three textures named in the `.fxsxml` (packed in `UITextures.fpk`), UIShader from
  the EXE file, and the world matrices of 2.3. SelectedAni's animation (`ec="1000"`, clock +0x5E88) moves the mesh
  through a Granny transform track [I: per-bone rigid motion, evaluated by the EXE's pose code; a reader must evaluate
  that track too].

## 3. Who calls the five path functions (P6) [C]

- **0x44FA30** (a method of the EXE's interface object InterfaceBuddy; run from 0x4D9800 when its "path dirty" byte +0x22D
  is set; 0x4D9800 is an engine-event delegate subscribed in InterfaceBuddy's setup 0x5A05D0). It takes a record of type
  0x51 from the 4 MB double-buffered queue (0x6D5E40; `message-queue-and-fixed-buffers.md`) and sets its count to 0. It
  takes the head selected unit and the mouse plot (+0x90/+0x92). The plot is wrapped by the map size if the map wraps,
  else clamped. If the unit's state is not 1, it calls **`ICvGameContext1::TEMPCalculatePathFinderUpdates(unit, x, y)`**
  (vtable +0x22C). Then while **`ICvEnumerator::MoveNext`** is true it calls **`GetCurrent`** and the update interface, and
  for the first 64 nodes **`ICvPathFinderUpdate1::GetX`, `GetY`, `GetTurnNumber`**, each stored as 16 bits. It releases
  each update object and the enumerator, and writes count = min(n, 64). The record then feeds MovementPath's path
  handler (2.2) and [I] the Lua event `UIPathFinderUpdate`.
- **0x5A0040** (InterfaceBuddy's reset, vtable entry 0xA3FEB8, also called from 0x46D5D3) calls
  **`ICvGameContext1::ResetPathFinder`** (vtable +0x230). It clears several highlight groups and, when the head unit is
  the active player's, writes an empty type-0x51 record. VP's `ResetPathFinder` does nothing ("not necessary anymore").
  So **ResetPathFinder does not track the preview's hide**. Hide comes only from `DisplayMovementIndicator(false)`.
  Decision 3 (ii) (derive show/hide from the served calls) would not match stock's timing [I: the timing of 0x5A0040's
  callers not measured].
- Lua `UI.SendPathfinderUpdate()` (CP `InGame.lua`) is what asks for a new path [I: it sets the dirty byte].

## 4. The server's handler of the five calls (P7; a pointer for ticket 0300) [C]

Epic 0179 copies no path answers now (route (b), #1009). For 0300: the five calls are served by the generated dispatchers
(`proxy/gen/gen_runtime.py` `server_dispatch()`, written to `<out>/gen/server/server_dispatch_N.gen.cpp`:
`Dispatch_ICvGameContext1`, `Dispatch_ICvEnumerator`, `Dispatch_ICvPathFinderUpdate1`). They are entered from
`SrvRemote::OnCall` (`server/src/remote/srv_rt.cpp`, after `LockSync`) on the service thread of the channel used. That
is the `ui` channel if the EXE's interface object runs on its main thread [I]. A copy without extra DLL calls (R1) would be
hand-written `ServerHook_ICvPathFinderUpdate1_GetX/GetY/GetTurnNumber` (and the `TEMPCalculatePathFinderUpdates` hook as
the start mark) in `server/src/remote/`. Each calls its `ServerFwd_` and records the returned integer. The generator
picks hooks up from that folder. The end of a path is the first `MoveNext` that returns false.

## 5. Not verified, and what settles it

- The head's slanted side normals M1/M2 (1.6): from the capture, or a further static pass.
- Bit-exactness of the decoded tables: the symbolic decode gives each value's operands and the order of its additions
  for the ring (C ± w·S first, then ± width2·N), but a vertex-for-vertex comparison against the capture is 0199's.
- The meaning of the state presets in general: only their values in these draws are known (1.7, 2.4, from the capture).
- Pass A's trigger value (`[app+0x690]` from the 0x4CBD15 settings read): a live read of `[GameView+0x1B8]` in a normal
  game (expected 0).
- The thread of the path query (main thread, ui channel): the served-call counts per channel in 3801's `server.log`.
- The SpawnArrowEvent Lua binding (how (x, y) become the event's triples): reading 0x4D2966's event object; not needed
  if the server computes plot centres itself.
- The path variant textures per node in stock (which Center*/Connector* draws): a draw-by-draw check of 3801's path
  capture (frame6496) against the placement rule of 2.3.
