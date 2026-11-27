# Farm fields and farm tracks: the EXE's layout as behaviour (ticket 4850, epic 0528 piece A)

Written 2026-11-18 from 01:20 EST by worker-4850 (account B) for 0528 C, the server's own farm-field producer (D186).
Read on a scratch copy of the analysed DX11 EXE (a copy of the Ghidra project under `C:/c5-64bit-work/0528/4850/`,
headless, read only); no game instance. Offline checks against two recorded decal taps of stock loads of the huge save
(section 11). D83: nothing below is decompiler text, a listing or pseudo-code taken from one; every statement is
behaviour in this file's own words, with addresses for a reader who wants to check. Marks: `[C]` read in the code,
`[L]` measured (live by an earlier ticket, or offline on a recording, as cited), `[I]` inferred. Addresses are DX11 VAs
at ImageBase 0x400000. Lengths are in **layout units**: the decal sample frame with its y divided by 1.1547344 (the
EXE stores a decal's y as (origin y + layout y) x 1.1547344, `terrain-decals.md` 1.2); in layout units plots are 64
apart in x and 55.43 (64 x 0.866) apart between rows.

## 0. Results in one screen

- **What it is** [C]: the farm fields are the faces of a planar graph of **hyperstreamlines** traced through a smooth
  direction field (the street-layout method of tensor-field road modelling; the EXE's own source names are
  `cvRoadNetwork.h` and "RoadLayout"). Two families of lines cross at right angles: family 1 follows the field's
  direction, family 2 its perpendicular. Each closed face of up to 11 edges becomes one field polygon; the edges that
  border fields become the farm tracks.
- **The direction field** [C, L offline]: angle(p) = 0.65572 rad + (pi/8) N(p x 1/150 + o) + (pi/16) N(p x 1/40 + o),
  N = standard 2D simplex noise with Ken Perlin's permutation table, o a per-map offset from the terrain system. It does
  **not** read the city stamps of the city-field manager (corrects `cities-and-landmarks.md` 1.9, see section 7).
- **When** [C]: the farm builder 0x748FC0 listens to queue B event 59 (improvement created/changed). For a farm art on
  a plot that is on the map and flagged (event byte +0x1C, meaning open, 1.2 and 2.1), and only the first time for that plot,
  it lays out a 128 x 110.85 rectangle centred on the plot, then extracts the faces on the plot and its six neighbours,
  posts the polygons to the 4 MB queue (types 4, 3, 6, 5), and re-lays the farm tracks of the touched graph cells.
- **Per field** [C, L offline]: the landmark sim's farm manager turns each polygon into one decal: ear-clipped
  triangles (a fan for convex fields), priority from `farm_types.xml` (Crops 0.5), uv = R(a) x ((p - centroid) / 25.6
  + 0.5) with a = -(field angle at the centroid) and, for one field in three (`rand() % 3 == 0`), a + pi/2; the
  texture a random variant of the farm's continent category (Crops_Africa, _America, _Asia, _Europe), or of
  Crops_Half_Built / Crops_Wheat / Crops_Wheat_HB by the farm record's state and wheat flag; a second Crops_Damage decal
  for state 8. Verified on 9,256 recorded field decals: uv scale 1/25.6 (p50 0.0390625), angles in [-71.3, -3.8] and
  [18.7, 86.2] degrees, 32.9-33.0 % turned.
- **Tracks** [C, L offline]: one Detail_Road decal per graph grid cell (two cells per plot in each axis): a strip 0.5
  wide (+-0.25) along every polyline of the cell that lies wholly inside the field-margin mask bit 0x10; u 0 / 1 across,
  v + 1.3 per polyline point. Verified on 4,903 recorded track decals.
- **rand()**: 100 calls at GameView start-up (the seed tile), per farm call one per accepted point seed's family plus
  one per point seed queued after the first, and per field 1-3 in the farm manager (section 8). Process-wide CRT
  generator, shared with every other `rand()` user (and reseeded by the SPECIAL placers' `srand(q + r)`).
- **Point 10**: skipping the builder is safe for the EXE's own state if the server draws fields and tracks; three
  visible side effects lose their input (farm-building snap, the trees' cut mask under fields, the track mask), and the
  route builder does not use this graph (section 10).

## 1. When the builder runs, and what it reads per plot

### 1.1 Registration and events [C]

GameView Startup's Landmark branch 0x623170 (only when `DisableSystems` bit 0x2 is clear) calls 0x465600 with the
builder object 0xBA20D0, which registers four listeners (each a 20-byte node: object at +0xC, function at +0x10,
`message-queue-and-fixed-buffers.md`). The event numbers come from the registration thunks' slot addresses (slot =
manager + 0xC + 8 x type, both managers):

| Function | Queue, event | What it does with the event |
|---|---|---|
| **0x748FC0** (builder) | B 59 (slot 0x2A103E4): Improvement created/changed (`world-systems-and-stubs.md` 7 names B59/60 "Improvement") | section 1.2 |
| 0x45BDB0 | B 60 (slot 0x2A103EC): Improvement destroyed | marks the plot's entry of the builder's per-plot table (+0x10) with -2 when it held a farm; nothing else |
| 0x4C2CA0 | 4 MB queue ("A") 36 (slot 0x1A0E52C) | copies the event's 332 bytes into its object at +8; reader not found [I: a settings block] |
| 0x79D040 | A 33 (slot 0x1A0E514) | marks the graph grid cells under the event's rectangle dirty (0x504B10, wrapping in x) and re-lays their tracks (0x6E27D0, section 5). Posted (type 0x21) by the farm manager's record removal 0x421280 with the rectangle of the plot and its six neighbours, after it cleared the plot's field masks and re-stamped the neighbours' |

The same thunks register the landmark sim's farm-manager listeners (0x582970): **A 3 -> 0x7AD6C0** (field polygon), **A 5 ->
0x603000** (farm end: re-place the farm building), B 48 -> 0x5307A0 (era change of farm buildings, `cities-and-landmarks.md`
2.6), B 60 -> 0x4349F0, and others. The builder's records reach the farm manager only when the 4 MB queue is pumped, after
queue B's whole batch (`load-sequence.md` 1.1).

### 1.2 The builder's decision per event [C]

From the event: hex x at +0x18 and y at +0x1A (16-bit; cube z = -(x + y)), the improvement art id at +8, a flag byte at
+0x1C. The builder acts only when: the flag is non-zero [C; its meaning is open: the poster at 0x7AB59E sets it to an
equality test between an event value and a 16-bit field at +0x38 of an object; 2.1 shows it is false on a fixed set of
plots whose farm model is drawn. *Corrected 2026-11-18 03:20 (A3); replaced "[I: the plot is revealed to the active
player; consistent with terrain-decals.md 5.3 ...]": the reveal filter acts earlier, through the revealed improvement
the event carries (2.1)*; *2026-11-18 05:38 (4853, 14.5) [L offline, 2,400 events of two loads]: the flag is 1 on exactly
the events whose art id +8 is the farm's, so it repeats the art test below*], the plot lies on the map (0x42B440),
and the art id equals the id of `ART_DEF_IMPROVEMENT_FARM`. Then:

1. post a **type 4** record (farm begin, no payload) to the 4 MB queue;
2. look up the plot's entry in the builder's per-plot table (20 bytes per plot, map width x height, wrapping in x when
   the map wraps; the farm id at +0x10, -1 = never laid out):
   - **-1**: store the art id there, compute the plot's layout rectangle (1.3), run the layout 0x6016F0 on it (section
     3), run the face pass 0x4A8990 (sections 3.6, 4.1), then the track pass 0x61FD40 (section 5);
   - **anything else** (laid out before, or -2 after a destroy): call a function through a CEG-protected thunk (0x42B4F0
     -> 0x6AB570, target computed at run time, not read). [I, strong: the target is 0x79D930, which re-posts every field
     the plot already owns as type-3 records without any new layout; it is the only other poster of type-3 records and it
     walks the same per-plot face list.]
3. post a **type 5** record (farm end) carrying the hex x and y.

So a plot is laid out **once per session**: later events (state changes, re-reveals, a farm destroyed and rebuilt) do not
lay it out again [C for the -1 test; I for what the thunk does].

**The farm manager's side of the same event** [C]: the landmark sim's farm manager also listens to B59 (0x7040D0,
registered on the event type the sim looks up at start-up). When the plot already has a farm record it first **removes
it** (0x421280: every field decal of the record removed, its curves freed and their mask bits cleared, the neighbours'
field masks stamped again, an A33 track re-lay posted, the record freed); then, if the event's improvement is the farm,
it makes a new, empty record from the event (section 4.2, step 5). So every B59 on a farm plot wipes its fields, and
they come back only through new type-3 records: from the layout the first time, and from the builder's "already laid
out" path afterwards (which is why that path must re-post the plot's faces; 0x79D930 does exactly that).

### 1.3 The plot's rectangle [C]

0x418B60 with the cube coordinates: the plot centre through the EXE's hex matrix (0xD91030), then a square of +-64 around
it, its x scaled by 1.0 and its y by 0.866, plus the map origin (0xD90FCC, 0xD90FD0): **128 x 110.85 layout units centred
on the plot**, which reaches about half-way into the six neighbours. Everything the layout traces is clipped to this
rectangle (strict inequalities).

### 1.4 Load against play [C, L where cited]

- **Load**: StartInitStuffOnMap posts queue-B records for every plot; the B59 events of all farms are dispatched in one
  frame during the load gap (`load-sequence.md` 1.1), in the order the plots were walked (records "arrive in column order",
  `message-queue-and-fixed-buffers.md`) [L]. One builder call per farm: 121-235 ms each on the huge save [L, G88 capture].
- **Play**: one B59 per created or changed improvement; a new farm's fields reached the decal store 0.40 s after the
  setter in stock (`terrain-decals.md` 5.3) [L].
- The layout depends on what earlier calls left in the graph (section 2), so the **plot order** changes the picture.

## 2. Farm groups

There is no explicit group object [C]. What joins farms:

- **One shared, persistent graph.** Every builder call adds to the same graph at 0xBA2228 and never clears it during a
  session. A new farm's lines stop where they meet existing lines (3.4), and the seeds of a later call include
  continuation seeds from the dangling ends of earlier lines inside the new rectangle (3.3). Adjacent farms therefore
  form one connected pattern whose shape depends on the order they were laid out [C].
- **Rectangles overlap.** A farm's rectangle covers half of each neighbour; lines and faces cross plot borders.
- **Fields belong to the plot under their centroid** (4.1). A field whose centroid lies on a neighbouring farm plot
  that has already been laid out is given to that plot; a field whose centroid lies on a plot that is not a laid-out farm
  is dropped. In principle a laid-out farm ends with no fields when no face centred on it closes; the recorded loads show
  this is **not** why stock farms are bare (2.1). *Corrected 2026-11-18 03:20 by A3; replaced "So a farm can end with no
  fields when its rectangle's faces all centre on other plots, or when no face closes (rivers, map edge) [I: the cause of
  the 44 fieldless farms ...]".*
- **Re-cut of neighbours.** The face pass re-examines the face lists of the plot and its six neighbours (hex distance
  <= 1): faces that are no longer valid are withdrawn (type 6 record) and rebuilt from their edges, so laying out a farm
  can change the fields of the farms around it [C for the walk over the seven plots; I for how often it fires].

The "farm group" of the G88 capture (22.3 records, 5,191 bytes, 181 ms mean) is one builder call: begin, the field
polygons and withdrawals, end [L].

### 2.1 Why stock leaves farms bare (follow-up A3, 2026-11-18 03:20 EST)

`evidence/4850/bare_farms.py` (own code; reads the recordings and the plot dump at run time, prints statistics only;
reuses 4852's `render/evidence/0528/compare_huge.py` reader and plot geometry read-only), run 2026-11-18 03:15 on the two
stock huge recordings of section 11 and 4101's plot dump `plots_huge_stock.json` (128 x 80, 425 farms, 175 on river
plots, 26 pillaged) [L offline]:

| | 0366 recording | 4111 recording |
|---|---|---|
| farms without a field centred on them | **85** | **87** |
| the same plots in both | 85 (2 more only in 4111) | |
| bare farms whose hex (points within 26 units of the centre) is >= 5 % covered by any field | **0** | **0** |
| 1: the farm's own model ground (`modern_farm_decal_diff`) drawn on the plot, no fields | 15 | 16 |
| 2: a resource model's ground (`corn_random_d`, `rice_random_d`) instead | 7 | 7 |
| 3: another improvement's model ground (trading post 5, fort, lumber mill, antiquity site) | 8 | 8 |
| 4: no improvement model at all (22 / 25 of them with no decal of any kind on the plot) | 55 | 56 |
| for comparison, farms with fields that show the farm model ground | 330 of 340 | 329 of 338 |

What this rules out [L]: the centroid rule and the order dependence of this section (a farm whose faces were given to a
neighbour would still be covered by those fields: no bare farm is), and any random layout outcome (the layout is random
per session, yet the same 85 plots are bare in both sessions). Withdrawals cannot empty a laid-out farm either: an
attached face is withdrawn only when its edge chain is broken, and its edges are walked again in the same pass (4.1) [C].

The cause [I, strong]:
- **Classes 2-4 (70 / 71 farms): the EXE does not show a farm there.** The B59 events carry the active team's
  **revealed** improvement (`terrain-decals.md` 5.F: `GameplayPlotStateChange` describes the revealed view); on these
  plots the active team never saw the farm (no model, often no decal at all: unexplored or fogged since before the farm
  was built) or last saw another improvement there. No farm event, so neither the farm model nor any field. The plot dump
  is the true state (Lua getters), which is why these count as farms in 4852's comparison.
- *Corrected 2026-11-18 05:38 by 4853 (14.5) [L offline]: the flag is never 0 on an event that carries the farm's art id
  (2,400 events of 0527's h15 / h16), so the explanation below does not hold; class 1's cause is open, the candidates are
  the 7 farms per load whose event carries another art id. The text below is kept as A3 wrote it.*
- **Class 1 (15 / 16 farms): shown as a farm, never laid out.** The farm manager makes the record and model from every
  B59 farm event (0x7040D0 checks no flag), while the builder also requires event byte +0x1C (1.2). The event poster at
  0x7AB59E sets that byte to the result of an equality test between an event value and a 16-bit field at +0x38 of an
  object [C for the test; its operands were not identified]. A flag that is false for a fixed set of plots in one save
  gives exactly this: a farm model with no fields on the same plots in every load. Seven of the 15 have no other farm
  next to them, so an empty layout is not plausible for them (stock's median is 14 fields).
- So no class comes from the face walk's early exits or the withdrawal path of 10.4.

**For the server**: classes 2-4 come out right when the producer lays out only plots whose **revealed** improvement is a
farm: the feed's `PlotRecord.improvement` is the revealed one once a `GameplayPlotStateChange` was seen (section 9); a
comparison against stock must use that, not the plot dump's true improvement. Class 1 (3.5 % of the farms stock shows)
is behaviour to reproduce once the +0x1C test is known; until then the server lays those farms out, a small visible
difference.

What would confirm the [I] items: a live read on the huge save of the B59 events' +0x1C byte and art id per plot (a
breakpoint at 0x748FC0 logging x, y, +8, +0x1C), compared with these classes; and the active team's `IsRevealed` and
`GetRevealedImprovementType` for the 85 plots through the Lua channel.

## 3. The layout graph at 0xBA2228 and the streamline layout 0x6016F0

### 3.1 The objects [C]

| Object | Where | What |
|---|---|---|
| layout | 0xBA2224 (builder + 0x154) | +0: streamline counter (each traced line gets the next number, its tag); +4: the graph; +0xEC: the seed queues; +0xFC: the tracer; +0x1AC: the seed tile (50 entries of 0x30 bytes); parameters at +0x44..+0x5C (3.2) |
| graph | 0xBA2228 | the planar graph of `crashes-huge-load.md` 2: **junctions** (the "endpoints", 20 bytes: up to four handles and a count) and **edges** (the "nodes", 56 bytes: a polyline between two junctions, its family byte +0x36, its face-use count +0x37, its tag +0x30); a grid of 2 x 2 cells per plot over the map (0x618CE0, sized by CreateTerrain's 0x760920) for spatial queries and the track decals; a list of face records (0x40 bytes: up to 12 edge handles) and polygons |
| tracer | 0xBA2320 | the line being traced: field object pointer, rectangle, family (1 or 2), direction (forward 4 / backward 8), its point list |
| field | 0xB9DDA0 (the city-field manager), its tail +0x4068..+0x4080 | base angle, noise offset, field bounds (section 7) |

A handle names an edge and which end of it (bit 28), as in `crashes-huge-load.md` 2. A junction holds at most four
handles; the edge creator 0x554DB0 refuses an edge whose either junction is already full.

### 3.2 Parameters [C values; I where the role is marked]

*Rewritten 2026-11-18 02:35 EST by worker-4850 (follow-up A2, after 4852's port gave faces of median 24 units^2 against
stock's 186). The first version read every offset against the layout object; the block lives in two other objects, and
the readers are now each checked against the object pointer they are given (ECX at the call) [C].*

The layout's constructor 0x541A70 builds the graph (layout + 4 = 0xBA2228) with 0x49B020 and the tracer (layout +
0xFC = 0xBA2320) with 0x597270. Both write the same block at **their own** +0x44..+0x68: 2.4, 1.3, 14.4, 13.6, 13.6,
4.0, 9.6 and +0x68 = 2. Nothing in the 99 functions of the layout chain (0x6016F0 and 0x4A8990 to depth 3) writes them
again. Who reads which copy [C]:

| Value | Copy read (offset in the reader's object) | Reader | Role |
|---|---|---|---|
| 1.3 | tracer +0x48 | the step 0x64D890 (this = tracer) | tracer step h: one step advances 0.5 h = **0.65** units (unchanged) |
| 14.4 | graph +0x4C (read as layout +0x50) | 0x74B850 (this = layout) | **arc length between offset seeds** along a new line (3.3) |
| 13.6 | graph +0x50 (layout +0x54) | 0x56C0D0 (this = layout) | separation radius of **point seeds (kind 0)** |
| 13.6 | graph +0x54 (layout +0x58) | 0x56C0D0 | separation radius of **offset seeds (kind 1)** |
| 4.0 | graph +0x58 (layout +0x5C) | 0x56C0D0 | separation radius of **continuation seeds (kind 2)** |
| 2.4 | graph +0x44 | the segment test 0x4072C0 (this = graph) | **stop distance**: a step that ends closer than 2.4 to a line of the same family (other tag) ends the trace (3.4) |
| 9.6 | graph +0x5C | 0x4072C0 | **near zone**: once a step ends closer than 9.6 to such a line, the trace stops at its next crossing (3.4) |
| 1.3 (graph +0x48), 2 (+0x68), the tracer's other copies | - | no reader in the chain | none found [C for the 99 functions; I that nothing outside reads them] |

*Replaced: "+0x50 13.6 arc length between offset seeds; +0x54 13.6 kind 0; +0x58 4.0 kind 1; +0x5C 9.6 kind 2; +0x44,
+0x4C 2.4, 14.4 not used; +0x68 2 not used".*

Other constants: offset-seed start 0.65 (1.3 x 0.5) beside the line; seed tile period 64; field-polygon uv scale 1/25.6;
field-margin push 5.0; track half-width 0.25; track v step 1.3; point merge tolerance 0.004 (squared distance).

### 3.3 Seeds [C]

A seed is 0x30 bytes: kind (+8), family (+9), a direction code (+0xA), position (+0xC, +0x10), normal (+0x14, +0x18),
tag (+0x24), junction (+0x28). Three queues, popped in this order: **continuation** seeds first, then **offset** seeds,
then **point** seeds.

- **The seed tile** (made once per GameView start-up by 0x432220): 50 points, each x = (rand() % 1000) x 0.064 then
  y = (rand() % 1000) x 0.064 (x drawn first), so a random pattern in a 64 x 64 square. The pattern repeats every 64
  units over the whole map: the same 50 relative positions in every 64 x 64 tile.
- **Point seeds (kind 0)**, per builder call (0x6AD000): every tile point of every period cell touching the rectangle
  that lies strictly inside the rectangle (about 173 for a full rectangle). Each is inserted into the point queue at a
  random place: when the queue already holds k seeds, at index rand() % k; the first goes in without a call.
- **Continuation seeds (kind 2)**, per builder call (0x662180), before the point seeds: for every live edge of family
  1 or 2 whose bounding box meets the rectangle, each of its two junctions that holds exactly one handle of that family
  (a dangling end) gives a seed at the junction's point, with the edge's family, its tag and the junction; the end at
  +0x16 goes to the front of the queue, the end at +0x14 to the back.
- **Offset seeds (kind 1)**, made while a line is added (0x74B850): walk the new line's points accumulating arc length;
  each time it passes **14.4**, subtract 14.4 and push a seed at the midpoint of the current segment, with the segment's
  unit normal and the **other** family. Pushed to the front of the offset queue (last made, first taken). The line
  traced from it starts 0.65 (constants 1.3 x 0.5, not the parameter block) from the parent along that normal, on one
  side only. *Corrected 2026-11-18 02:35 (A2): the spacing is 14.4 (graph +0x4C); replaced "each time it passes 13.6".*

### 3.4 One seed, one line [C]

The layout loop pops a seed and tests it (0x56C0D0): rejected if any live edge with more than one point, inside the
seed's +-r box, has a segment closer than r to the seed point (squared point-to-segment distance against r squared),
with r by kind: **13.6 for kind 0, 13.6 for kind 1, 4.0 for kind 2**. Kind 0 tests against every edge; kind 1 only
against edges of the seed's family (the parent, of the other family, is not tested); kind 2 against edges of the seed's
family with a different tag (not the line it continues). *Corrected 2026-11-18 02:35 (A2); replaced "r by kind (13.6,
4.0, 9.6)".* Then:

- **Kind 0**: family = 2 - (rand() % 2 != 0), i.e. 1 or 2 with equal odds; start at the seed point.
- **Kind 1**: start at seed point + 0.65 x normal; family from the seed.
- **Kind 2**: only if the junction still holds exactly one handle of the family: trace from that junction, in the
  direction the seed names; the backward case reverses the new edges afterwards (the node reversal 0x52F0E0).
- Kinds 0 and 1: the counter (tag) is raised, a new junction is made at the start, the line is traced **forward**, then
  **backward** from the same junction; when both halves made edges, the first edge of each half is merged into one
  (0x46F8B0) so the junction disappears; a start junction left empty is freed.

**The trace** (tracer 0x5B5640 set-up, step 0x49F790 / 0x64D890) [C]: from the last point p, with d(q) = the unit
field direction at q (family 1: the field vector normalised; family 2: it turned by -90 degrees; backward: negated):
q1 = p + 0.65 d(p), then three more times q = p + 0.65 d(q) using the direction at the previous q; the fourth q is the
next point. The line stops when a q leaves the rectangle or the field's bounds, when the field vector is zero, or (for
the stored point) when the point lies on a mask cell with bit 0x08 (river footprint, `cities-and-landmarks.md` 2.7).

**The distance stops** (0x4072C0, run on every step's segment before it is inserted; added 2026-11-18 02:35, A2) [C]:
the candidates are the live edges whose bounding box meets the segment's box grown by 20 units. For each segment of
such an edge, when the edge has a **different tag** and the **same family** as the line being traced (both non-zero):
the Euclidean distance d from the step's **new point** to that edge segment is measured; d < **2.4**: the step is
refused and the trace ends there, and the piece traced since the line's last junction is **not** kept (no last edge is
made for it); d < **9.6**: the line is marked "near". A line marked near stops at the next hit of any edge (the hit's
junction is made, then the trace ends); a line not near stops at a hit only by the rules of the next paragraph. Lines of
the other family and the line's own earlier pieces (same tag) are never distance-tested, so crossings are free. Hits
themselves use small tolerances: a segment end within 1e-5 (squared 1e-10) of an edge point counts as touching, a point
within 0.01 (squared 1e-4) of an edge snaps to it, and the segment-intersection tests use 1e-4 and, at the map's x
range, 0.001.

**Expected effect on face size** [I]: against the first version's rules, point and offset seeds are both kept 13.6 from
same-family lines (offset seeds were 4.0), offset seeds are 14.4 apart along their parent (13.6), and a line ends 2.4
before a parallel line or at its next crossing once within 9.6 instead of running on. All three make fewer, larger and
squarer faces; 4852's trial with a 13.6 separation alone gave a median of 252 units^2 against stock's 186. The
distance stops remove the dangling tails that the prune would otherwise delete and probably cut some long faces where
lines meet near a parallel neighbour; the net median was not computed here.

**Inserting the line** (0x5B86D0) [C]: each step's segment is intersected with the graph through the grid (0x4072C0);
hits are sorted along the segment. A hit on another edge splits that edge there (0x5A9C90) and makes a junction; the
line's piece so far becomes an edge from its last junction to the new one (0x554DB0) and is welded (0x77C1C0). The line
**stops** at a hit when the junction then holds three handles, or exactly one other handle of its own family; it goes
on through a crossing of the other family. A junction that is full (4 handles) on both sides ends the line. Hits of
kind 2 / 4 make a new junction and shift the tracer's frame by +-1 times the graph's world width (graph +0x6C) [C; I
that these are the wrap seam of a wrapping map]. When the trace ends, the rest becomes a
last edge; a last edge of a single point (+0x10 == 1) of family 1 or 2 is deleted at once (0x727420 at 0x5B8EA4).
Points are appended with a collinear merge (0x524730): a point whose predecessor lies within squared distance 0.004 of
the segment from the point before it to the new one replaces that predecessor, so straight runs keep only their ends
(the recorded fields have 4-16 polygon vertices, p50 5, section 11).

### 3.5 Pruning [C]

After the seed queues are empty (0x493CF0), repeated until a pass deletes nothing: delete every live edge that has a
single point, and every edge of family 1 or 2 used by no face (+0x37 == 0) with a junction holding only one handle
(dead ends). After the face pass, 0x5C86D0 deletes every edge made in this call (the list at layout +0x194 relative to
the graph object) that is of family 1 or 2 and borders no field (+0x37 == 0).

### 3.6 Faces [C]

The face pass 0x4A8990 starts a face walk (0x7A3BC0) from every live edge whose box meets the rectangle (from its +0x14
end): follow the edge's points to its far junction, then take the junction's handle (not of the same edge) that turns
by the smallest angle in one rotational sense (angles from atan2, wrapped by 2 pi), and repeat until the start edge comes
back. Each edge passed gets its face-use count raised. The walk is abandoned when it reaches a dead end (a junction with
one handle) or 12 edges; abandoned walks undo their counts. A face is kept only when the walk closes in the expected
orientation; its polygon is the concatenated points. (Two early exits, at a junction with no handle and with no
candidate, return without undoing the count: section 10.4.)

## 4. Field polygons and their decal records

### 4.1 From faces to records (0x5D3F50, 0x5EAD20) [C]

- For each plot at hex distance <= 1 from the farm: re-walk the faces listed for that plot whose validity check fails,
  and withdraw the old face records (0x606030: a **type 6** record with the plot and the face id; each edge's face-use
  count lowered; an edge of family 1 or 2 left at 0 is deleted). *Added 2026-11-18 03:20 (A3)* [C]: the validity check
  0x6A2010 asks only whether the face's edges still join end to start through the same junctions; it fails when a new
  line has split one of them (the face was cut). The re-walk starts from each of the old face's edges in the same pass,
  so the pieces of a cut field come back as new polygons (and go through the centroid rule below); an attached face is
  never withdrawn otherwise (a new polygon contained in an attached one is the one dropped), and 0x5EAD20 withdraws only
  polygons not yet attached. So withdrawals re-cut a farm's fields but cannot leave a laid-out farm empty unless the
  re-walk itself finds no closed face.
- For each new polygon: its area-weighted centroid (0x869570); polygons whose centroid lies inside another new polygon
  are dropped (0x869830) [C for the test; I for which of the pair goes].
- The centroid, moved to the world frame and through the world-to-hex matrix 0xD90FF0, names a plot. When that plot is
  off the map or has no laid-out farm in the per-plot table (+0x10 == -1), the polygon is dropped. Otherwise it is
  linked into that plot's face list, the face gets its plot, and the count of polygons that land on each plot is
  computed.
- Each kept polygon is posted (0x507BB0) as **type 3** records: the cube hex of its plot, the field id, the number of
  fields on that plot, chunk i of n, the point count, and up to 16 float pairs per record (`message-queue-and-fixed-
  buffers.md`) [C, L for the record form].

### 4.2 The farm manager's side (0x7AD6C0 -> 0x5865B0) [C]

The landmark sim's farm manager (0x2D70334, `terrain-decals.md` 8.3) gathers the chunks of one polygon into a **curve**
(a 0x2C-byte entry in its table at +0x6F4, points in 0xFC-byte segments of 30 points) and lists the curve in the plot's
**farm record** (0x90 bytes, at most 50 curves at +0x20); a type-3 record for a plot without a farm record (index
0xFFFF) is ignored. While the farm manager is deferring (its +0x207C0 bit 0) the record is queued for later. For each
complete curve it calls 0x5865B0:

1. **Mask bits** (0x543D10): the polygon's triangles are rasterised into the plot mask 0xBA392C with bit 0x01; the
   polygon with every point pushed 5.0 units away from its centroid is rasterised with bit 0x10 (the field margin).
2. **Triangles**: ear clipping (0x86FAD0): repeatedly, from index 0, the first vertex i whose next two live vertices j,
   k make a triangle with the polygon's orientation and no other vertex inside gives triangle (i, j, k) and j is
   removed; a full pass without an ear fails and **no decal is made**. A convex polygon gives the fan from vertex 0
   (`terrain-decals.md` 4.2 "every triangle repeats the first vertex") [C, L].
3. **Turn**: the field direction at the centroid c (0x44DA00 on 0xB9DDA0, section 7), normalised; a = -atan2(y, x);
   then **rand() % 3 == 0 adds pi/2** (one field in three).
4. **Vertices** (16-byte {x, y, u, v} handed to AddDecal): x = origin x + px, y = (origin y + py) x 1.1547344 (the map
   origin of the height field +0xB4 / +0xB8); (u, v) = R(a) applied to ((px - cx) / 25.6 + 0.5, (py - cy) / 25.6 + 0.5),
   R(a) the rotation by a about the uv origin (D3DX rotation about z, row-vector form).
5. **Texture**: by the farm record's flags (+0xC): bits 4-7 the continent (1 America, 2 Asia, 3 Africa, 4 Europe; any
   other value: **no decal**); if the record's era level (+7) is not 0: bit 0x200 set -> Crops_Wheat (state 2 ->
   Crops_Wheat_HB), else state (+4) 2 -> Crops_Half_Built, else the continent category. The variant is first +
   rand() % count of that category (`farm_types.xml` order, `terrain-decals.md` 8.3); a category with no variants makes
   no decal and draws no `rand()`. With an era level of 0 nothing is drawn after the turn [C; I: era level 0 means the
   farm record has no row yet].
   **Where the record's fields come from** (0x7040D0, the farm manager's B59 listener) [C]: continent nibble (bits
   4-7) = event +0x10 & 0xF; bits 0-3 = event +0x14 & 0xF; **wheat flag 0x200 = the event's resource art (+4) is
   `ART_DEF_RESOURCE_WHEAT`** (the id the farm manager resolved into +0x4A8); state (+4) = event +0x24; owner = event
   +0xC; the farm-building row slot (bits 11-15) = rand() % (rows of that era) when the era (event +0x20) has rows. The
   era level (+7) is written by the era listener 0x5307A0 (B48: era 0-2 -> 1, 3 -> 2, 4 -> 3, more -> 4), which queue B
   dispatches in the same batch, before the 4 MB queue delivers the field records [C for the order of the pumps].
6. **AddDecal** (0x49B5A0) with the variant's priority, albedo, height map, spec (the default spec when the XML gives
   none), pass 0. The handle goes to the curve's +0x24. If the state is **8**, a second decal with the same geometry and
   a Crops_Damage variant (another `rand()`), handle at +0x28.

The record therefore is: priority 0.5 (VP's Crops), the rectangle = bounding box of all polygon points, 3 x (n - 2)
vertices for an n-point polygon, texture triple from `farm_types.xml`.

## 5. Farm tracks

### 5.1 Where they come from [C]

0x61FD40 runs after every builder call: first the edge clean-up of 3.5 (0x5C86D0), then 0x6E27D0 over the graph grid:
every cell with its dirty byte (+0x14) set gets its old track decal removed (0x66D090 -> RemoveDecal), a new one built
(0x55D0C0), and the byte cleared. The same 0x6E27D0 runs from CreateTerrain 0x709B50 (at 0x709FD3) and from the A33
listener 0x79D040 after it marks a rectangle's cells dirty. Cells are made dirty by the weld (0x77C1C0 refiles an edge
in its cell, `crashes-huge-load.md` 2) and by A33 [C for both; I that every edge change passes the weld].

### 5.2 The strip (0x55D0C0) [C, L offline]

For each edge listed in the cell with at least two points, and only if **every** point of the edge lies on a mask cell
with bit 0x10 (inside some field's 5-unit margin), one strip: two vertices per point at point +- 0.25 x n, n the unit
normal (at the two ends the end segment's normal; inside, the normalised sum of the two adjacent segments' unit
directions turned by 90 degrees); positions in the sample frame as for fields; u = 0 on one side and 1 on the other;
v = 0 at the first point and + 1.3 at each further point (per point, not per length). Two triangles per segment. All
strips of a cell make **one decal** through the variant wrapper 0x60F180 with category 0 (`Detail_Road`: priority 0.6,
handles 0 / 0 / default spec, `terrain-decals.md` 8.4); the category has one variant, so **no `rand()`**. The handle is
kept in the cell (+0x10).

Note: the mask bit 0x10 of a new field is written only when the farm manager handles the type-3 record, after queue B's
batch, while the builder's own track pass runs inside the batch. So a cell re-laid inside the builder call sees the
masks of fields posted earlier, not of its own call [C for the order; I that A33 brings the missing tracks later: the
recorded loads have tracks along nearly every field, 2,449-2,454 track decals for 4,626-4,630 fields].

## 6. Half-built, pillaged, removed

- **Half-built** [C, L]: farm record state (+4) 2 -> Crops_Half_Built (Crops_Wheat_HB for a wheat farm). Seen: 144 and
  148 Crops_Half_Built fields on the two huge recordings; no Crops_Wheat_HB.
- **Pillaged** [C; L for the absence]: state 8 adds the Crops_Damage overlay. **No Crops_Damage decal is in either huge
  recording**, although the save has 26 pillaged farms (`terrain-decals.md` 3.2): at the time the fields were made the
  records were not in state 8 [I: the fields of a load are made from the reveal event before the pillage state arrives,
  and a later state change does not re-make them]. So stock draws pillaged farms with their normal fields on this pin.
- **Wheat** [C, L]: record flag 0x200 -> Crops_Wheat (196 and 205 fields in the recordings). Where the flag is set was
  not read [I: farms on a wheat resource; the farm manager resolves `ART_DEF_RESOURCE_WHEAT`, `cities-and-landmarks.md`
  2.5].
- **Removed** [C]: the builder's B60 listener only marks its per-plot table (-2). The farm manager's B60 listener
  0x4349F0 runs the record removal 0x421280 (every field decal removed, curves freed, masks cleared, neighbours'
  masks re-stamped, an A33 track re-lay over the plot and its neighbours). A face withdrawn by a neighbour's re-cut posts
  type 6, whose farm-manager listener 0x6DEC60 removes that one field's decals and curve.
- **Any later B59 on a farm plot** (state change, rebuild) [C]: the farm manager wipes and re-creates the record
  (1.2); the builder does not lay out again; the fields return through the re-post path [I: the CEG thunk's target].
  The graph keeps the plot's old faces, so a rebuilt farm gets the same field shapes, with new turns and variants
  (new `rand()` calls in 0x5865B0).

## 7. The direction field and the city-field manager 0xB9DDA0

- **The field** (0x44DA00, on the city-field manager's tail) [C]: angle(p) = A + (pi/8) N(p x 0.0066667 + o) +
  (pi/16) N(p x 0.025 + o), returned as the vector 64 (cos angle, sin angle). A = atan(1 / 1.3) = **0.65572 rad**
  (37.57 degrees), written once by 0x432220 to 0xBA1E08 (manager + 0x4068). o = (manager + 0x406C, + 0x4070), copied
  with the field bounds (+0x4074..+0x4080 = the map rectangle 0xD90FCC..0xD90FDC) by 0x795B80 from CreateTerrain
  (0x709B50 -> 0x760920). o comes from 0xD91324 / 0xD91328 (also read by 0x6668F0); its writer was not found [I: a
  per-map value of the terrain system]. N (0x8F3880) is 2D simplex noise: skew (sqrt 3 - 1) / 2, unskew (3 - sqrt 3) / 6,
  12 gradients, the permutation table at 0xCA1488 = Ken Perlin's reference permutation (151, 160, 137, 91, 90, 15, ...)
  [C for the table's first 16 entries; I that the remaining entries and the final scale are the reference
  implementation's].
- **Check** [L offline]: the fitted uv angles of 9,256 recorded fields lie in [-71.3, -3.8] degrees (unturned) and
  [18.7, 86.2] (turned), exactly -(A +- (pi/8 + pi/16)) and that plus 90 degrees.
- **The city stamps are not used by the farm layout** [C]: 0x44DA00 reads only the tail; the layout, the tracer and the
  farm manager reach the manager only through it. *Corrects `cities-and-landmarks.md` 1.9 ("The farm-field layout 0x6016F0
  ... reads the manager. So the field orients farm fields and city objects around a city"): the layout reads the
  manager's noise field, not the 128 x 128 city stamps; cities do not bend the fields [C for the reads].* The farm
  building placement 0x4BE160 also takes its facing from 0x44DA00, and 0x454DC0 (from 0x760DF0) evaluates it at a hex
  edge point [C].
- **What else the manager holds for farms**: the 50-point seed tile is stored in the layout object, not in the manager;
  0x432220 fills both [C].

## 8. Every rand()

All calls go to the CRT `rand()` of the process (the main thread's state), shared with every other user (forest
placement, landmark row picks, river variants, the SPECIAL placers, which also call `srand(q + r)`). [C for each call
site; the counts are per event as read.]

| Where | When | Calls | Picks |
|---|---|---|---|
| 0x432220 | GameView start-up (once per game view) | 100 | the 50 seed-tile points: x then y, each `rand() % 1000` x 0.064 |
| 0x751520 | each point seed pushed in a builder call | 1 per seed pushed while the queue is not empty (k seeds -> k - 1 calls) | insertion index `rand() % count` |
| 0x6016F0 | each point seed that passes the separation test | 1 | family 1 or 2 (`rand() % 2`) |
| 0x5865B0 | each complete field curve that triangulates | 1 | turn: `rand() % 3 == 0` adds pi/2 |
| 0x5865B0 | the same, when era level != 0 and the category has variants | 1 | variant within the category |
| 0x5865B0 | the same, state 8 | 1 | Crops_Damage variant |
| 0x60F180 | each track decal | 0 (Detail_Road has one variant) | - |
| 0x4BE160 | farm building placement (farm manager, not the layout) | 1 | quarter turn `rand() & 3` (`cities-and-landmarks.md` 2.6) |
| 0x7040D0 | each B59 farm event in the farm manager, when the era has building rows | 1 | farm-building row slot (record bits 11-15) |

Per field the farm manager draws 2 calls in the usual case; per farm call the layout draws about (point seeds in the
rectangle - 1) + (point seeds accepted).

## 9. What a re-implementation needs from the feed (`SrvFeed::PlotRecord`, `server/src/feed/srv_feed.h`)

| Input the EXE uses | PlotRecord | Gap |
|---|---|---|
| farm on the plot (art = `ART_DEF_IMPROVEMENT_FARM`) | `improvement` (the DLL's ImprovementTypes) | the server must map improvement types to "draws as farm" (the art define of the improvement row, `ArtDefineTag`), not compare the type id with one constant |
| revealed to the active player (event byte +0x1C) | `fow` bit 0 | none; the EXE lays out a farm when it is revealed, not when it is built |
| state: half-built / built / pillaged (farm record +4 = event +0x24) | `improvementState` (1, 2, 3; -1 never seen) | the mapping to the EXE's 2 / 4 / 8 is [I]; stock does not show Crops_Damage at load (section 6) |
| continent art 1-4 (record +0xC bits 4-7 = event +0x10) | `artType` (0 none, 1 America, 2 Asia, 3 Africa, 4 Europe) | same numbering as the EXE's switch [I, strong]; 0 = no field decal |
| wheat (record flag 0x200: event resource art = `ART_DEF_RESOURCE_WHEAT`) | `resource` | the server maps resource types to their art define; the flag is set for exactly the resource whose art is `ART_DEF_RESOURCE_WHEAT` [C] |
| era level (record +7, 0 = no decal) | none | **missing**: the farm record's era level comes from the landmark sim (B48, era 0-2 -> 1, 3 -> 2, 4 -> 3, more -> 4); it only gates the decal (0 = none), it does not pick the texture, so the server can treat it as always set |
| plot order of the layout (load: plot walk; play: event order) | the feed's event order | the server picks its own order; with its own seed per plot the order matters only through the shared graph |
| map wrap, map rectangle, hex geometry | the height field / scene | none (as for the trees, `trees.md` 10.3) |
| river footprint (mask bit 0x08: lines stop on it) | `flags` bit 0 (river on any edge) | the edge-exact river footprint is the river mesh's (`cities-and-landmarks.md` 2.7); the server needs its river geometry, not the plot flag |
| noise offset o (0xD91324 / 0xD91328) | none | **missing** [I: per map]; the server may choose its own (random per session in stock anyway: P6) |
| neighbour farms already laid out | its own graph | none |

## 10. What else needs the graph or the builder (for 0528 D)

### 10.1 Readers of what the builder makes [C]

- **The farm manager's curves** (filled only from the type-3 records): (a) the field decals; (b) the plot mask bits
  0x01 (field footprint) and 0x10 (field margin) via 0x543D10; (c) the **farm building placement** 0x4BE160 moves the
  building to the nearest point of the plot's curves (`cities-and-landmarks.md` 2.6), run again on A5 (farm end, 0x603000)
  and on the era change 0x5307A0. Without the builder the farm buildings stay at their plot centre. [CORRECTED 2026-11-21, ticket 0637, `../02-exe-internals/farm-field-double-free.md` 4.4 and 4.7: with the builder skipped the farm buildings are NOT DRAWN AT ALL [L, pictures on build 26577625]; 'at the plot centre' was a code reading never looked at in a picture]
- **Mask bit 0x01** is one of the bits that hide trees (`trees.md` 2.3, bits 0-2): fields that reach a forest plot cut
  its trees in stock. Without the builder those trees stay. The server's forest port reads its cut mask through
  `c5r::forest::CutSource` (`render/include/c5r/forest/cut.h`), whose reader of the EXE's grid (4553) would then see no
  field footprints: 0528 C should stamp its own fields' footprint (bit 0x01) into the cut source the forest reads [C for
  the seam; I that the footprint matters on the tested saves].
- **Mask bit 0x10** gates the farm tracks (5.2) and nothing else found [C: readers of 0x4365E0 with bit 0x10: 0x55D0C0
  only].
- The builder's other three listeners (1.1) only feed the builder's own table and the graph's tracks.

### 10.2 The graph's other users [C]

- Callers of the edge creator 0x554DB0, the weld 0x77C1C0, the split 0x5A9C90 and the delete 0x727420 outside the farm
  chain: **0x49CED0**, reached only from 0x70FE80 <- 0x59CB60. 0x59CB60 is the terrain bake's entry on 0xBA20D0
  (`world-systems-and-stubs.md` "Terrain bake -> 0xBA20D0.0x760920 / 0x59CB60"); 0x70FE80 takes a polyline, wraps its x
  into the graph's range and inserts it with 0x49CED0. So the **terrain bake also inserts lines into this graph** [C for
  the call chain; I what they are: likely the river or coast lines that the farm lines must stop at].
- 0x493D6C (prune), 0x5C8740 (track clean-up) and 0x6061AF (face withdrawal) are all in the farm chain (3.5, 4.1).
- **The route builder** (roads, railroads: RoadLayout 0x2B735E8, 0x5EC250 -> 0x583CE0) does not call any of these
  functions [C: the callers lists above]; the "RoadLayout::GameView::Startup()" timer label belongs to 0x4AB360 on
  0xBA2228 (two matrices copied into the graph object), not to the route builder. So route curves have no share in
  this double free.
- CreateTerrain 0x709B50 calls 0xBA20D0.0x760920 (sizes the builder's table and the graph grid, sets the field bounds
  and noise offset) and 0x6E27D0 (re-lays dirty track cells) whether or not the builder ever ran [C].

### 10.3 Recommendation: skipping the builder [I]

Skipping the builder (F1's function word returning without calling 0x748FC0) is **safe for the EXE's state**: no
reader dereferences anything the builder alone creates; the graph stays empty except for the bake's lines (10.2); the
farm manager simply receives no type-3, 4, 5 or 6 records from it (A5 from other posters still re-places buildings
*[superseded 2026-11-21 by `cities-and-landmarks.md` 2.6.1 (ticket 9078, [C]): the builder is the only poster of A5, so
with it skipped no building is made at all]*);
CreateTerrain's 0x6E27D0 finds no dirty cells with track edges. Keep the other three listeners registered (they are
cheap and keep the table and the bake's inserts consistent). What the server must then provide, besides the fields and
tracks themselves: **(1)** the farm building's snap to the field outlines (or accept buildings at the plot centre; [CORRECTED 2026-11-21, ticket 0637, `../02-exe-internals/farm-field-double-free.md` 4.4 and 4.7: with the builder skipped the farm buildings are NOT DRAWN AT ALL [L, pictures on build 26577625]; 'at the plot centre' was a code reading never looked at in a picture]),
**(2)** the trees cut by field footprints on neighbouring forest plots (or accept them), **(3)** nothing for tracks (the
server draws its own). The type-6 withdrawal path stays unused. Not verified live: that no other reader of the farm
manager's curves exists (only 0x5865B0, 0x543D10, 0x4BE160 and the record walkers were found).

### 10.4 For the crash chain (`crashes-huge-load.md` 8, held by 0526 / 0527) [C for the paths, I which fires]

- The face walk 0x7A3BC0 has two early exits (a junction with no handle; no candidate handle other than the edge it
  came from) that return **without** lowering the face-use counts it raised and without freeing the face record and
  polygon it allocated. An **isolated closed loop** (one edge whose two ends are one junction holding only those two
  handles) takes the second exit: its count stays 1, so neither the prune 0x493CF0 (needs a junction with one handle)
  nor 0x5C86D0 (needs count 0) deletes it. The polygon left allocated is in the list the face pass reads next [C], so it
  is handled like a closed face [I]: if its centroid is not on a laid-out farm plot, 0x5EAD20 withdraws it with 0x606030
  (at 0x5EAF51), which lowers the loop's count to 0 and **deletes the loop with 0x727420 (at 0x6061AF)**: the double free
  of `crashes-huge-load.md` 8.3, in the same builder call; if it is on a farm plot it becomes a field and the loop is
  deleted later, when a neighbour's re-cut withdraws that face (0x606030 from 0x5D3F50). So the delete sites in the chain
  are 0x5B8EA4 (one-point edge), 0x493D6C (prune), 0x5C8740 (unused new edges) and 0x6061AF (face withdrawal), and the
  last is the one an isolated loop reaches [C for each path; I that the leaked polygon carries the face index 0x606030
  is given].
- Point seeds can start a loop: a family-1 or -2 line traced forward and backward from one junction and merged (0x46F8B0)
  closes into a loop on one junction when the field circles. Whether the noise field above has closed integral curves
  inside a 128 x 111 rectangle is [I]; F0's breakpoint on 0x727420 with the caller's return address would show which
  site deletes the loop.

## 11. Checks against recorded counts

`evidence/4850/farm_check.py` (own code; reads a .dtap at run time, prints statistics only), run 2026-11-18 01:40 on two
stock loads of the huge save: 0366's `C5H-0366-HUGE_0350` and 4111's `C5H-0166-4111-HUGE_0350` recordings
(`F:/c5-64bit-work/0366/ref/.../load0003.dtap`, `F:/c5-64bit-work/0166/4111/ref/.../load0003.dtap`; stock arm, VT) [L
offline]:

| Measure | 0366 | 4111 | Expected from the code |
|---|---|---|---|
| field decals | 4,630 | 4,626 | (4,650 in 4101's census) |
| categories | Europe 2,295, Africa 1,605, America 390, Wheat 196, Half_Built 144 | 2,294 / 1,593 / 386 / 205 / 148 | continent, wheat, half-built; Damage possible but absent (6) |
| fan from vertex 0 | 3,220 (70 %) | 3,257 (70 %) | every convex field; concave ones not |
| polygon vertices p5 / p50 / p95 / max | 4 / 5 / 8 / 14 | 4 / 5 / 8 / 16 | faces of <= 11 edges, collinear points merged |
| field area p5 / p50 / p95 (layout units^2) | **81 / 218 / 427** | **72 / 217 / 417** | offset seeds 14.4 apart, separation 13.6 (3.2; was "spacing 13.6", corrected 2026-11-18 A2) |
| rectangle p50 (layout units) | 20.7 x 20.8 | 20.6 x 20.8 | |
| uv scale p1 / p50 / p99 | 0.039043 / 0.0390625 / 0.039080 | 0.039040 / 0.0390625 / 0.039089 | 1 / 25.6, a pure rotation (residual p99 0.0003) |
| uv angle unturned / turned (degrees) | [-70, 0) 3,102 / [20, 80) 1,528 | 3,103 / 1,523 | [-71.3, -3.8] / [18.7, 86.2] |
| share turned | 33.0 % | 32.9 % | 1 / 3 |
| track decals; strip width p5 / p50 / p95 | 2,454; 0.493 / 0.503 / 0.505 | 2,449; 0.492 / 0.500 / 0.509 | 0.5 |
| track u values; v step | 0 and 1; 1.3 | 0 and 1; 1.3 | 0 / 1; 1.3 |

*Area row corrected 2026-11-18 02:55 EST by worker-4852 [L offline, `render/evidence/0528/compare_huge.txt`]: the
first figures (27 / 186 / 398 and 23 / 188 / 384) were the areas of the polygon rebuilt as if every field were a fan
from vertex 0, which is wrong for the 30 % that are not fans (concave fields); the exact sums of the recorded
triangles' areas are 81 / 218 / 427 and 72 / 217 / 417.*

`terrain-decals.md` 3.2's per-farm figures (425 farms, 1-33 fields, median 14, 44 without) were not recomputed here
(the recording has no plot list); a field area of about 186 units^2 against a plot of 3,547 units^2 gives about 19
fields if every face centred on a farm plot were kept, consistent with a median of 14 after the drops of 4.1 [I].
*The bare farms are explained in 2.1 (A3, 2026-11-18): not drops of 4.1 but farms the EXE does not show as farms (70 /
71) and farms whose builder flag is off (15 / 16).* *(The second half corrected 2026-11-18 by 4853, 14.5: the flag is
not off on farm events; class 1's cause is open.)*
*Measured 2026-11-18 by 4852 with 4101's plot dump of the same save (`compare_huge.txt`, field centroid -> plot): 0 /
11 / 24 (0366) and 0 / 12 / 26 (4111) fields per farm (min / median / max), 85 and 87 of 425 farms without a field
centred on them [L offline]; all 144 / 148 Crops_Half_Built fields are centred on plots the dump does not list as
farms (a half-built farm is not yet IMPROVEMENT_FARM to the getter) [I].*

## 12. Not verified, open

- The CEG-thunk target of the builder's "already laid out" path (1.2), and therefore what a state change or a rebuild
  does to fields [I].
- The writer of the noise offset (0xD91324 / 0xD91328) and the full simplex-noise constants past the first 16 table
  entries [I]. A recording plus the save's offset would allow a bit-exact check of the field angle per field centroid.
- The exact stopping rule of the trace at the rectangle and the river bit (the step function returns no point when the
  probe leaves the rectangle) [C in outline]. *The roles of 2.4 and 14.4 were answered 2026-11-18 by A2 (3.2, 3.4); +0x68 =
  2 and the graph's copy of 1.3 have no reader in the layout chain [C]; replaced "The roles of the parameters 2.4, 14.4
  and +0x68 = 2 (3.2)".*
- The net face-size effect of the corrected rules (3.4) is for 4852's port to measure against section 11.
- Which polygon of an overlapping pair is dropped (4.1); the validity test 0x6A2010 of the re-cut.
- Whether the lines the terrain bake inserts through 0x49CED0 are rivers or coasts (10.2).
- Whether the type-3 handler itself (or only 0x421280) posts the A33 re-lay after a new field's masks are stamped
  (5.2); 0x421280 posts it [C].

## 13. The server's producer frame (ticket 4851, 2026-11-18) [C for the code, I where marked]

Written by lead-0528 from worker-4851's hand-back; the code is on `epic-0528-farm-fields` (render/include/c5r/farmfields/farmfields.h, render/src/farmfields/, server/src/render/srv_farmfields.{h,cpp}); the layout itself is a placeholder until 0528 C (ticket 4852) ports sections 1-8.

- The steps: `layoutFarms` (the layout; placeholder: one 40-sample square field and one track per farm plot) -> `checkLayout` -> `makeDecals` -> `checkDecals`. `Producer::produce()` never hands out a set that fails a check; on a failure the decals in force stay.
- A field: a fan stored as a list (c0, ck, ck+1), priority 0.5, pass 0; names `@farm_types.xml/<Category>/<child>/albedo|heightmap|spec`, or `blank_texture.dds` when the child has no spec. A track: priority 0.6, albedo and heightmap `kNoTexture`, spec `blank_texture.dds`.
- Vertex positions (p - rect min) x (1 / extent) as binary16; the rectangle is the box of the decal's own vertices; the hash is the tap reader's `DecalContentHash` form.
- The tap's farm records are recognised as priority 0.5 with an `@farm_types` albedo (fields) and priority 0.6 with albedo and heightmap `kNoTexture` (tracks) [I: the exact float match of 0.5f / 0.6f]. With removal off, a tap set that holds farm records is refused (Unsupported) and handed on as is, as the forest does.
- In the walk block (terrain-decals.md 18) the kept decals' ranks are closed up per cell; farm decals go to the head of their cell, the last one given first. The area marks are every tile a farm decal touched during the load (stock never clears tile+0x300 before the next load).
- Plot centre (64 hx + 32 y + 32, 64 y + 128) with hx = x - (y >> 1), from the 0166 census join; an odd row's last plot has its centre at samplesX, outside the world, so its field is clamped or dropped.
- Seed: `HeightGrid::mapSeed` (PreGame map random seed) mixed with the plot (`plotSeed`) [I: that a save keeps it]. The farm improvement id comes by name (art define `ART_DEF_IMPROVEMENT_FARM` or type `IMPROVEMENT_FARM`, `ICvImprovementInfo1::GetArtDefineTag`, pure under D30) through a table like `server/src/forest/srv_forest_ids.h`, filled in `server/src/remote/srv_remote_feed.cpp` (0528 C; outside the touches, asked of the MC). Until the id is known the tap's set is handed on unchanged.
- Generations of the producer's changes start at 0xE0000000. `FarmFieldsProducer` (server side) has no test of its own yet (server/tests/render/ is 0424's); the `Producer` it wraps is covered by render/tests/farmfields (18 cases, plain and ASan, lead re-run 2026-11-18 01:51 passed).
- *Note 2026-11-18 03:20 (A3): the reveal filter that matters is the revealed improvement the event carries (2.1); the
  builder's +0x1C flag is not a reveal flag (1.2). The producer's input should be the feed's revealed improvement.*
- Open: whether stock lays out unrevealed farms (section 1.2 says it needs the reveal flag; the frame's `revealedOnly` defaults to false and 4852 sets it from section 1). *Done by 4852 (13.1): on, with a fallback when the feed read no fog. The placeholder layout is replaced by the port (13.1); the id table exists (`server/src/render/srv_farm_ids.h`).*

### 13.1 The layout as ported (ticket 4852, worker-4852, 2026-11-18 02:00-03:05 EST) [C for the code, L offline where cited, I where marked]

Code: `render/include/c5r/farmfields/layout.h` and `render/src/farmfields/layout.cpp` (the stages), `layoutFarms` in
`render/src/farmfields/farmfields.cpp` (the assembly; it replaced 4851's placeholder, which the frame's tests keep as a
fixture), `server/src/render/srv_farm_ids.h` and a few lines in `server/src/remote/srv_remote_feed.cpp` (the ids),
`server/src/render/srv_farmfields.{h,cpp}` (polls them). Tests: `render/tests/farmfields/layout_test.cpp` (12 cases) and
`farmfields_test.cpp` (18). Evidence: `render/evidence/0528/` (`text_values.txt`, `compare_huge.py` and its printed
`compare_huge.txt`, `calibration.txt` superseded). Sections 1-8 as written, with these decisions:

- **Order independence (D186's "the same fields on every load")**: stock grows one graph farm by farm, so its picture
  depends on the order farms arrive in (1.4, 2). The port lays the graph out per fixed **block of 4 x 4 plots** (window:
  the block's hexes plus 48 L), from the game's seed and the block's index only, whatever plots are farms; the 50-point
  seed tile, the noise offset o and each block's `rand()` stream come from the seed (`seedTile`, `fieldParamsFor`, the
  block's `CrtRand`). A farm's fields are its block's faces whose centroid lies on it (4.1's rule), so a farm built,
  pillaged or removed changes only its own fields and the track cells near it (test
  `a_farm_added_and_removed_changes_only_its_neighbourhood`); the uv turn and variant come from a stream per field
  (the plot's seed and the field's place in centroid order). Continuation seeds (kind 2) only arise when a half line is
  cut (`LayoutParams::contArc`, default no cut: with the corrected stops the cut changes nothing measurable,
  `text_values.txt`: median 246-252 for pieces of 48 L to no cut).
- **Block seams**: two blocks' graphs share no lines. Two passes in a fixed order (blocks by y then x): a block's own
  faces, each dropped when more than a quarter of it overlaps kept faces of earlier blocks; then faces of a block that
  are centred on another block's farm plot, kept when less than a quarter of them overlaps kept faces (they fill the
  strip each side would leave) [I: the quarter]. Measured on the huge save (`compare_huge.txt`, 2 L samples of the farm
  hexes): no field on 26.8 % of the samples within 6 L of a block border against 7.6 % away from one; two or more on
  2.6 % / 0.6 %. Stock on the same samples: 54.1 % / 37.9 % without, 3.1 % / 1.8 % double (it leaves much more of a farm
  bare, everywhere). A DIAGNOSTIC picture: Visual Checks `vc-20261118-030033-workstation-4584` (`--issue 4852`).
- **Parameters**: the corrected 3.2-3.4 exactly (13.6 / 13.6 / 4.0, spacing 14.4, the 2.4 and 9.6 distance stops; the
  2.4 refusal drops the piece since the last junction; a piece joins the spatial grid only at a junction or the end, as
  an edge is made only there). The earlier calibration of the misread 4.0 to 11.1 is superseded.
- **Not ported**: the river footprint (mask bit 0x08; the server has no river geometry here), the wrap seam of a wrapping
  map (blocks at x = 0 and x = W - 1 are laid out apart), the overlapping-polygon drop of 4.1 (faces of one planar graph
  do not overlap), the re-cut of neighbours (type 6) and the "already laid out" re-post (a new production replaces the
  set). The track's cell is the cell of the edge's first point [I]. A pillaged farm (the feed's state 3) adds a
  Crops_Damage copy after each field [I: the EXE's state 8; stock shows none at load, 6]. `fieldCategory`: art type 0 gives
  no field (the continent nibble outside 1-4).
- **Against stock** (`compare_huge.txt`, the 425 farms of the huge save; their art type, half-built and wheat inferred
  from the stock fields centred on them, an input of the comparison only) [L offline]:

| Measure | server | stock 0366 | stock 4111 |
|---|---|---|---|
| field decals | 5,991 | 4,630 | 4,626 |
| fields per farm min / p50 / max; farms without | 7 / 13 / 40; 0 | 0 / 11 / 24; 85 | 0 / 12 / 26; 87 |
| area p5 / p50 / p95 (L^2, exact) | 117 / 250 / 499 | 81 / 218 / 427 | 72 / 217 / 417 |
| rectangle p50 (L) | 22.5 x 22.4 | 20.7 x 20.7 | 20.6 x 20.8 |
| vertices p5 / p50 / p95 / max | 4 / 5 / 7 / 11 | 4 / 5 / 8 / 14 | 4 / 5 / 8 / 16 |
| uv scale p50; turned share | 0.0390625; 33.5 % | 0.0390623; 33.0 % | 0.0390623; 32.9 % |
| uv angle unturned / turned (deg) | -66.7..-9.7 / 23.4..81.5 | -66.4..-5.1 / 27.5..79.8 | -69.2..-7.6 / 24.2..83.9 |
| track decals; strip width p50; v step | 3,213; 0.500; 1.30 | 2,454; 0.503; 1.30 | 2,449; 0.500; 1.30 |

  The gap: fields are 15 % larger at the median and the small ones are missing (p5 117 against 72-81) [I: stock's small
  fields come from lines cut by earlier lines of its shared, order-dependent graph and by the farm rectangles' edges]; no
  farm is without fields (stock: 85 of 425, mostly fields centred on neighbours or on plots not yet laid out). Not tuned
  (lead-0528, 2026-11-18).
- **Cost** [L, indication, workstation, plain clang-cl Release, single thread, not under the quiet lock, COMMON 23]:
  425 farms of the huge save in 146 blocks, 5.7-6.6 s at a load (about 40 ms per block graph); with the load's block
  graphs cached (`BlockCache`, kept by `Producer`), one farm changed: 249 ms. Stock's builder took 121-235 ms per farm
  (1.4).
- **The seed** [L offline]: the two huge recordings carry `mapSeed` 0 in their height field; with 0 every game gets the
  same pattern (deterministic, not per game). 0528 D checks what the field tap reads in a live load. **The choice**
  (lead-0528 with the MC, 2026-11-18, thread 0528 #5057/#5058): `mapSeed` when it is not 0; when it is 0 live, the
  game seed becomes a hash of the map's static plot fields (terrain, plot type, feature, natural-wonder bit of every
  plot), which a save keeps and which differs between maps [I until D's live check].
- **Cost in play** (MC #5058): a change re-lays only the changed farm's 4 x 4 block and its seam neighbours, never on
  the render thread; at load the blocks are laid out in parallel off the render thread. D reports load time and the
  per-change time by `docs/briefs/PERFORMANCE.md`. *As built (13.2): every production runs on the producer's worker
  thread; a load lays its blocks out in parallel; a change lays out no block at all (the load's graphs are cached, a graph
  depends on the seed and the block only) and re-assembles the fields and tracks of every farm on the worker (90-137 ms
  on the huge save). The single-thread figures of the Cost bullet above are superseded by 13.2's.*
- **The ids** (`srv_farm_ids.h`): at every plot snapshot the feed's DLL side reads every improvement's and resource's
  art define (pure getters, D30) and publishes the ids of `ART_DEF_IMPROVEMENT_FARM` and `ART_DEF_RESOURCE_WHEAT` (the
  lowest id when several share a tag); `FarmFieldsProducer::refresh` polls them. `revealedOnly` is on (1.2's reveal
  flag [I]); a table in which no plot is revealed (no fog read) counts as all revealed.

### 13.2 The producer in the scene (ticket 4852 step 4, worker-4852, 2026-11-18 04:13-04:45 EST) [C for the code, L where cited, I where marked]

Code: `server/src/render/srv_render_scene.{h,cpp}` (Scene), `server/src/render/srv_render_host.cpp` (the keys, the feed
batch), `server/src/render/srv_farmfields.{h,cpp}` (the worker, the rules below), `render/src/farmfields/farmfields.cpp`
(parallel block layout, `Producer::accept`). Not wired yet: the scene bundle member (needs 0424's paths).

- **The scene** (`render_farmfields=1`): Scene owns one `FarmFieldsProducer`, made once and kept across device rebuilds.
  At init it asks the producer to read `farm_types.xml` through the install with `ModFolders()` above it (the residency's
  decal view, `terrain-decals.md` 12.2) on its worker. Per field: `setGameSeed(HeightGrid::mapSeed)` and
  `setWorld((0, 0, samplesX - 1, samplesY - 1), loadSeq)`. The host hands every drained feed batch to `farmFeed` beside
  `forestFeed`. Each loop, `farmDecalPoll` (beside `forestDecalPoll`, in both of `Scene::poll`'s paths; nothing in
  `decide` or `idle`): the producer's `refresh`, then, when its decals changed, the residency gets `setDecals(
  composedDecals(tap))` with `composedDecals = forest.composeDecals(farm.composeDecals(tap))`, then the change in the
  farm band (`kFarmDecalGenBase + n`). Every other `setDecals` of the scene already goes through `composedDecals`, so a
  new tap set carries the farm decals too. One totals line `farmfields totals: ...` at the end.
- **Keys** (`srv_render_host.cpp`): `render_farmfields` 0 (default: the decal tap's farm fields as they are) / 1;
  `render_farmfields_keep_tap` 0 (default: the tap's own farm records are dropped) / 1 (kept; a tap set that holds them
  is then handed on without the server's, as the forest does); `render_farmfields_ids_wait_ms` (default 20000): with
  `render_farmfields=1` and no farm id this long after the first plot table, one LOUD line (the server draws no fields of
  its own; the tap's set stays in use), never a silent world without fields. A failed read of `farm_types.xml` is LOUD
  the same way. Not in `render_config` of the bundle yet (default 0 draws nothing new; the member comes with 0424).
- **Revealed farms only** (MC #5072, 2.1): the feed's `PlotRecord.improvement` is the active team's revealed improvement
  once a `GameplayPlotStateChange` was seen for the plot (`known` bit 1), else the getter's true value [C,
  `server/src/feed/srv_feed.cpp` UpdatePlot: the getter writes `improvement` only while bit 1 is clear]. At a load the EXE
  asks for the layout of every revealed, developed plot (`terrain-decals.md` 5.F: 9,966 of 10,240 plots on the huge save
  [L]), so bit 1 marks exactly the plots the EXE lays out. `FarmPlotsFromTable` takes a plot when bit 1 is set and its
  (revealed) improvement is the farm. Fallbacks: a table without any bit 1 (headless: the notification never fires)
  takes the getter's value on plots with fog bit 0 (revealed); a table without either takes every plot. **Gap** [I]: an
  unrevealed plot never gets bit 1, so its true farm is never drawn (right); a plot revealed at the load but with no
  layout request (nothing to lay out at the time) gets bit 1 when its first improvement appears in sight, which is the
  EXE's own moment. No feed change is needed.
- **The seed**: `mapSeed` when it is not 0, else `StaticPlotSeed` (FNV-1a 32 over every plot's terrain, plot type,
  feature and natural-wonder bit), taken from the first plot table of each load and kept for the load (a feature cut in
  play does not move the fields) [I: that a later save of the same game, with other features, gives another pattern].
- **Cost** [L, indication, workstation (32 hardware threads), plain clang-cl Release, not under the quiet lock, COMMON 23;
  `render/evidence/0528/compare_huge.txt` 04:24]: the producer never runs on the render thread (`FarmFieldsOptions::
  threaded`, default on: one worker, one job slot that a newer input replaces, one result slot). A load lays its 146
  blocks out in parallel on the hardware's threads less one, at most 8: **751-801 ms** for the 425 farms of the huge save
  (was 5.7-6.6 s on one thread); the result is byte-equal whatever the thread count (test
  `parallel_blocks_and_the_cache_give_the_same_bytes`). A change in play lays out **no block** (the load's graphs stay
  cached) and re-assembles the fields and tracks on the worker: **90-137 ms** for one farm changed. The re-assembly is
  global (every block's faces, the seam passes, the tracks); a per-block assembly would cut it further [I].

### 13.3 The farm fields in the scene bundle (ticket 4852 last item, worker-4852, 2026-11-18 04:36-05:20 EST) [C for the code, tests where cited]

- **No member, format 8 unchanged.** Every bundle reader REFUSES an unknown member kind (`srv_scene_bundle.cpp`,
  "unknown kind"; "the checker and every reader REFUSE (never skip)"), so a `farmfields.bin` could not be skipped by an
  older reader and would have been format 9 with a re-recorded library. `render_config` is a string map whose unknown keys
  no reader refuses (the checker only tests the keys it knows), so the producer's input goes there:
  `render_farmfields` ("0" / "1", written by the host for every bundle and again by the recorder at the span) and, with
  "1", `render_farmfields_state` = `c5r::farmfields::spanText` of the input of the decals in force at the span's first
  frame: `fv1 seed=<u32> farm=<id> wheat=<id> map=<W>,<H> world=<4 hex floats> farms=<x>.<y>.<state>.<art>.<wheat>,...`
  (`render_farmfields_keep_tap` "1" only when set). `farm_types.xml` is not in it: the player reads it from its install, as
  live. The layout is deterministic from these, so no graph or decal is stored. Size [L offline]: 813 bytes for 64 farms
  in the test; **5,282 bytes** for the huge save's 425 farms (`c5r_farmfields_layout.exe --produce` on
  `compare_huge.py`'s farm list, 2026-11-18 05:05).
- **The recorder** (`srv_scene_recorder.*`, `SceneRecorder::takeFarm`, set up by the host with `setFarm`): at the span's
  first frame the two keys, or a note `gap: farm fields: ...` when the producer had no production in force; a production
  put in force later in the span is a note too (the line holds the first frame's input).
- **The checker** (`CheckBundle`): `render_farmfields` other than 0 / 1, a state without "1", a state not starting
  "fv1 " are BadValue; "1" with neither the state nor a `gap: farm fields` note is MissingMember. Every older bundle (no
  key) passes and draws the tap's farm fields as they are. Tests: `bundle_format_test` `TestFarmConfig` (5 accepted, 5
  refused, the state byte-equal through the manifest's JSON, and an unknown render_config key accepted: the rule an older
  player relies on to load a bundle with the farm keys).
- **The player** (`server/tools/sceneshot/sceneshot.cpp`, outside 0528's touches until the MC republishes it, #5155): sets
  `farmfields`, `farmKeepTap`, `farmThreaded = false` (every production inside `poll()`, before the frame that needs it)
  from `render_config`, and after `Scene::init` calls `FarmFieldsProducer::restoreSpanInput` (the recorded input: no
  plot-table reads, the recorded seed, ids and world stand). The edit is kept ready; until it lands a bundle recorded with
  `render_farmfields=1` replays without the server's farm fields.
- **The round trip** [C, `server/tests/render/farmfields/test_farmfields_producer.cpp` `bundle_round_trip`, 2026-11-18]: a
  live producer's span text restored in a second producer (no table, another map seed and world given) gives the same
  1,594 decals byte for byte (FNV-1a 64 of records, vertices and names equal) and the same span text back.

## 14. The EXE's farm builder retired: the option, the trees' cut, the acceptance (ticket 4853, 0528 D, worker-4853, 2026-11-18 from 05:05 EST) [C for the code, L where cited, I where marked]

Code on `task-4853-farm-builder-option` (from `epic-0528-farm-fields` 2c94a47e8). Live items (3-8) run on oldpc after
4852's last item merges; until then this section holds the plan and what is expected, marked *(plan)*.

### 14.1 The switch, end to end [C]

- **The option**: `VP64Options.ini` `[Renderer] ServerFarmFields`, 0 (default) or 1 (`options/include/vp64_options.h`
  `kServerFarmFields`: enum value 7 on this branch, **8** once epic 0504's `GameEventsOneCrossing` (7) has merged, MC
  #5171 / #5192; the table is indexed by id, so a gap is not possible on a branch without 0504). The player text for
  `docs/OPTIONS.md` is `shell/evidence/0528/OPTIONS-ServerFarmFields.md` (the MC adds it at the merge, #5192).
- **The proxy** (`proxy/src/proxy_shell.cpp` `FarmLines`, after `PathLines`): with 1, and only when this boot writes the
  server's render lines (`ServerRenderer = 1`, the composite accepted, `server_config=` not set), server.cfg gets
  `render_farmfields=1` and `render_farmfields_keep_tap=0`, and `OnBoot` sets `VPSHELL_FARM_BUILDER=0` in the EXE's
  environment before the composite's `LoadLibrary`. Otherwise one `shell: [Renderer] ServerFarmFields = 1: ...` line
  says why and nothing is written. With 0 nothing changes (14.4). A second boot in one EXE process keeps the first
  boot's environment value [I: not tested; the composite stays loaded across boots anyway].
- **The composite** (`shell/composite/shell_composite.cpp`, `[composite] farm_builder`, 1 by default, read from
  `VPSHELL_FARM_BUILDER` first like every key; the shipped ini states `farm_builder=1`): `FarmGuardEnv::skipBuilder`.
  0527's hook (`farm_guard.cpp` `FgHook`) keeps its order (tripped, no GameView, stale node) and then, with the skip,
  **returns before the builder call**: counted `skipServer`, event line `skipped-server` in `farm-builder-calls.log`, no
  check (nothing ran). The burst line ends `; not called for farm_builder=0 N` (and says `checks 0: none run`); the
  in-game line ends `not called for farm_builder=0: N in this process`. With the skip the hook stays in the node even
  with `farm_guard=0` (`want = enabled || tripped || skip`; the farm_guard=0 line says it is ignored), and a new
  registration is swapped as before. The other three listeners of 0x465600 stay registered (10.3).
- **The server**: `render_farmfields=1` lays out and draws the fields and tracks (13.2); `render_farmfields_keep_tap=0`
  drops the tap's farm records (none arrive once the builder is retired).
- **What is lost without the builder** (10.1, 10.3): the farm building's snap to the field outlines (buildings stay at
  the plot centre: named in the A/B pictures; [CORRECTED 2026-11-21, ticket 0637, `../02-exe-internals/farm-field-double-free.md` 4.4 and 4.7: with the builder skipped the farm buildings are NOT DRAWN AT ALL [L, pictures on build 26577625]; 'at the plot centre' was a code reading never looked at in a picture]); the trees' cut under fields (14.2 puts it back); the track mask (the server
  draws its own tracks). 0x6E27D0 (CreateTerrain, A33) finds no dirty track cells.
- **When the composite refuses the EXE or its patterns** (another build): the guard is off, the builder runs, and the
  server still drops the tap's farm records and draws its own (the picture stays the server's; the crash risk is stock's,
  as 14.3).

### 14.2 The trees under fields: the field footprints in the forest's cut source [C; I where marked]

- `c5r::farmfields::FieldFootprint` (`render/include/c5r/farmfields/footprint.h`): bit 0x01 on every 2 x 2-sample cell
  of the sample frame whose low corner (2c, 2r) lies inside a field polygon (even-odd) [I: the best of four rules tried, 14.7; the EXE's
  rasteriser 0x6287A0 was not read] (*corrected 2026-11-18 06:55 after 14.7; was "whose centre lies inside"*); the lookup of a world point is `c5r::forest::cutCell`'s (sample = (ox + x, (oy + y) x
  1.1547344) with HeightGrid's ox / oy; sx = 1, sy = 0.866). Tracks set nothing; a Crops_Damage copy has the same corners.
- `FarmFieldsProducer` builds it on its worker with each production (`setCutFrame` from the field), puts it in force only
  when its hash changed (an unchanged layout rebuilds no tree), drops it at a new load; totals `footprints=`,
  `footprint_err=`, `footprint_cells=`, and the scene's `farm_cut_sets=`.
- `srvrender::CutOverlaySlot()` (`server/src/render/srv_cut_tap.h`): one per process, render thread only.
  `CutTapReader::bitsAt` ORs the overlay's bits into the EXE grid's, `generation()` adds the overlay's, so the forest's
  `pollCut` sees the generation move and rebuilds its built plots (as on any stamp of the EXE). `Scene::farmDecalPoll`
  hands the producer's footprint to the slot when its version moved; render_farmfields=0 and the Scene's end clear it.
- Offline [L, `render/tests/farmfields/footprint_test.cpp`, 2026-11-18 05:35]: on a 12 x 10 test map with 42 farms, 552
  fields of 169,184 samples^2 set 41,901 cells (167,604 samples^2, -0.9 %); every field's centroid cell is set (552 of
  552); the lookup equals a `DenseCut` of the same cells through the same frame at every point of a 0.53 x 0.37 world grid.
- **The coverage rule against the EXE's grid, by totals** [L offline, 2026-11-18 06:10; `shell/evidence/0528/cutgrid_4853.py`
  and `rule_check_4853.py`]: 4553's stock huge load on oldpc (`F:/c5-64bit-work/4553/oldpc-79b4/out/TB-HUGE_0350/cd-n4/
  cut_tap/cut_10096_4.bin`, 4,096 x 2,688 cells) holds **314,908** field-footprint cells (bit 0x01 without the river's 0x08;
  river 340,880, margin 0x10 393,555; generation 1, before the farms, holds the river cells only). The 4,630 stock fields of
  0366's recording (another session of the same save: the layout differs, about 3 % between sessions) cover 1,236,055
  samples^2 (309,014 cells' worth) and give 299,305 cells by the centre rule (-5.0 %) and 324,224 by an any-overlap rule
  (+3.0 %), river cells left out of both. So the totals do not decide the rule; the cell-by-cell check needs a decal
  recording and the cut grid of **one** session (live part, 14.6; lead-0528 #5207).
- Not covered: a forest without the cut tap's reader (`EmptyCut`: the offline player, tests) gets no overlay; with
  `render_farmfields=1` while the EXE's builder still runs (a hand-made test config), the EXE's own footprints and the
  server's are both cut (in the product only the option sets the key, and it retires the builder).

### 14.3 Item 10: a game without the composite (MC open point, 0528 log 05:06, D185)

- **Can the farm crash occur there? Yes** [C for the path, I for the rate]. With `ServerRenderer = 0` (the options
  file's default; increment 1's thin client) or a refused composite, the composite is not loaded, so neither 0527's guard
  nor this option exists in the process, and the server draws nothing (no server layout to fall back on). The EXE's
  builder 0x748FC0 runs for every farm event as in stock, through the same layout and node delete as
  `crashes-huge-load.md` 8 (10.4 here): the double free is a defect of the EXE's own code, so such a game has the stock
  rate. Measured: 3 crashes in 57 huge product loads, 0 in 24 stock loads (p = 0.55, `crashes-huge-load.md` 8.5), so the
  stock rate is not known to be lower.
- **What covers it today**: only the report. The proxy is loaded on every product start; an EXE fault reaches the proxy's
  crash filter and the D119 report (`error-report-exe-crash.md`), and Windows' box ends in ticket 0519's report path;
  nothing prevents the fault.
- **What could cover it** [I; for the MC; by D185 a question for the Architect if wanted]: (a) host 0527's F1 and F2 in
  the proxy (loaded on every product start) instead of, or beside, the composite: the swap is a data write under the
  EXE's own registration lock and can run from the proxy's Update hook (the game-core thread, `proxy_update.cpp`); the hook
  runs where the EXE calls it (the main thread's queue-B pump); only the in-game check needs a main-thread point between
  frames, which the proxy lacks (it could check at the next builder call instead). It lowers the rate as the guard does
  (it stops after a damaging call); it does not remove it. 0527 section 7 named this follow-up. (b) Retiring the builder
  there is not possible: without the server's picture there would be no farm fields at all (D66). (c) Removing the crash
  for such games needs the server's layout, that is `ServerRenderer = 1` with this option.

### 14.4 Tests [L offline, workstation, 2026-11-18]

- `farm_guard_test.exe`: **133 checks** (0527's 114 + section 11: the skip with a real payload and an unreadable one,
  the other listeners still called, the burst and in-game lines, the event lines, farm_guard=0 ignored while skipping, a
  re-registration while skipping, back to farm_builder=1, the skip with farm_guard=0 from the start, an env without the
  field behaving as 0527). `shell/evidence/0528/suites_4853.sh suites` (05:27-05:35): test_farm_guard 5 passed,
  test_config_defaults passed (the shipped ini's `farm_builder=1` equals the built-in default); under MSVC
  /fsanitize=address (05:49): test_farm_guard 5 passed, 0 ASan report files.
- `c5r_farmfields_footprint` (7 cases) with 4852's `c5r_farmfields_farmfields` and `_layout`: 3 of 3 passed
  (05:33-05:35, `render/build.py --test -R c5r_farmfields`); ASan 3 of 3, 0 reports (05:53); again on the merged branch
  (4852's last item in, bf0ef526c, 06:00-06:06): plain 3 of 3, ASan 3 of 3, 0 reports.
- `server/tests/render/farmfields/test_farmfields_producer.cpp` case `footprint_into_cut` (the producer's footprint equals
  one built from its layout, 72,981 cells under the first production's fields on the 16 x 12 test map; a field's cell reads
  0x01 through a `CutTapReader` without a control block and the slot, the reader's generation moves by one per `set`; a
  pillage keeps the footprint (the version does not move), a farm removed shrinks it, a new load drops it, totals
  `footprints=2 footprint_err=0`): the program's 12 cases pass, plain and MSVC ASan (`test_farmfields_producer.py`, 3
  passed each, 06:00-06:04).
- The four shell cases that failed in the first plain run (`test_decal_tap` huge-batch restart, three `test_cut_tap` cases;
  composite code this ticket does not touch) passed alone (8 passed with parameters, 05:58): load of two builds beside
  that run [I].
- options: `test_options` with `TestServerFarmFields`: **261 passed, 0 failed** on x86 and x64 (05:45) against
  `docs/OPTIONS.md` with the evidence text appended; on the real file 260 / 1 (the docs check of `ServerFarmFields = 0`,
  until the MC adds the text). Trap met: the default file text passed 4 KB with this option, and `TestDefaultText`'s
  `static char edited[4096]` + `strcpy_s` ended the process with 0xC0000409 (no output at all); the buffer is 8 KB now
  and the size bound 8,000 (was 4,000). The next option past 8 KB meets it again.
- proxy: `proxy/tools/test_proxy_rt.py --only-units --units-baseline 2c94a47e8` (05:41): with `ServerFarmFields` at its
  default the server.cfg lines of every ServerUnits / ServerArc setup are byte-equal to 2c94a47e8's (proxy_shell.cpp
  compiled with the proxy's toolchain); 96 of 97, the one failure the baseline's own negative ("the baseline ignores
  ServerUnits = 1"), which needs a baseline from before 4759 and is not this ticket's. The `FarmLines` cases themselves
  have no unit program (`proxy/tools/test_proxy_rt.py` is outside 0528's touches): the live part checks them in
  `vpproxy.log` and server.cfg.

### 14.5 Item 7, first half: the event byte +0x1C [L offline from 0527's recorded loads; I where marked]

`shell/evidence/0528/events_1c_4853.py` over 0527's h15 and h16 (`F:/c5-64bit-work/0527/h15|h16/out/`: each load's
`farm-builder-calls.log` and plot dump), 2026-11-18 05:38:
- In both loads, 1,200 events, one per plot: **+0x1C = 1 on exactly the 373 events whose +0x08 (the art id) is 0x27**,
  +0x1C = 0 on all 827 others (+0x08 one of many values). No plot got both values. So on these loads the byte says "the
  event's art id is the farm's" [L for the counts; I that this is its meaning: 1.2's equality test against the 16-bit
  field at +0x38 compares the event's art id with the farm's], redundant with the builder's own art test.
- Of the 425 farms (true improvement in the dump): 354 got a +0x1C = 1 event, **7 got only one event, with another art
  id** (+0x1C = 0; (51, 28) 0x2A and (72, 60) 0x32 in both loads; (28, 45), (28, 50), (31, 32), (45, 26), (51, 49) an id in
  0x4B-0x4E that differs between the two loads), 64 got none (the revealed view does not show a farm: 2.1's classes 2-4).
- **Correction to 2.1's class 1** (A3: "shown as a farm, never laid out" because the builder's flag is off): no event
  of 2,400 carries the farm's art id with the flag off, so the flag does not explain class 1 [L]. The candidates are the
  7 farms per load whose event carries another art id [I]; what would confirm: the art define names of 0x2A, 0x32 and
  0x4B-0x4E, and the model ground drawn on those 7 plots in the same load. For the server nothing changes: it lays out
  the plots whose revealed improvement is the farm (13.2).
- *(plan) The live half*: the option's own `farm-builder-calls.log` lines (`skipped-server`, the same payload fields) on
  oldpc give the same table for option-on loads, and the field's `mapSeed` read live (13.2).

### 14.6 The live part on oldpc (items 3-8) *(plan, written 2026-11-18 05:45 before any run)*

Set-up as 0527 (`shell/evidence/0527/run_0527.py`: 0410's load, 3308's prep and restore; D33 product path, Overlay on,
`ServerRenderer = 1`, saves copied under our own names and removed at the restore), on oldpc through
`tools/testbed/tb.py` (D106; quiet submits with `--expect-min`), on a Goldberg instance (D177: `frontend.py dismiss`,
`turns.py`), the dialog watcher's marker at every turn (COMMON 29), the GPU driver named (D175). Arms:
`ServerFarmFields = 1` (on) and 0 (off: 0527's path). The job: `shell/evidence/0528/live_4853.py` (0420's
`forest_t0.py` with 4553's cut watcher; per launch the option after product_path's prep, `layout_peek.py`, the newest decal
tap image of the session as `decals_after_load.dtap` beside the cut grids, `--farm-change` for item 5, `--tour --pictures`
for item 4; after the stop the builder's event log, the guard's, server's and proxy's lines). Build:
`shell/evidence/0528/build_4853.sh all` (pin c29442e8c, layout x64-c29442e8-p58724e0's bin with this branch's server.exe,
proxy and composite, on F:); submits: `shell/evidence/0528/submit_4853.sh ship | func | series N | pictures`. The rule
check of 14.2 cell by cell: `rule_check_4853.py --dtap <arm off>/decals_after_load.dtap --grid <arm off>/cut_tap/<newest>`
(one session). Expected:
- **Item 3** (>= 20 huge loads on): the composite's `farm_builder=0 ...` line, the swap at 0 nodes / 0 endpoints live,
  every burst `builder calls N (skipped N), checks 0: none run; ...; not called for farm_builder=0 N` (N about 1,200 on
  the huge save: every type-59 event of the load), 0 freed endpoints at the in-game check and in `layout_peek.py`'s read
  (no delete ran), the live node and endpoint counts equal across loads (the bake's lines only; 0527 t01: Tiny with 0
  farms holds 238 / 238), 0 crashes, 0 boxes.
- **Item 4** (A/B, DIAGNOSTIC unless from the full shipped system, D112): the same saves on and off; fields per farm,
  size, turn, style, tint, trees under fields (the overlay), farm buildings at the plot centre (on). [CORRECTED 2026-11-21, ticket 0637, `../02-exe-internals/farm-field-double-free.md` 4.4 and 4.7: with the builder skipped the farm buildings are NOT DRAWN AT ALL [L, pictures on build 26577625]; 'at the plot centre' was a code reading never looked at in a picture]
- **Item 5**: a farm built, pillaged and repaired with `turns.py`; the fields change within the same turn (the producer's
  change in play: 90-137 ms on the workstation, 13.2).
- **Item 6** (PERFORMANCE.md): load time of the huge save and the EXE's private memory after the load, on and off (the
  builder's layout leaves the load gap; the server's 751-801 ms run off the render thread); the queue guard's drops of
  farm types (expected 0: no farm records are posted with the builder retired).
- **Item 8**: one windowed identity pair (D164), on against off, the game state equal (the layout is a picture only).

### 14.7 Live results [L, oldpc, RTX 2080 with NVIDIA 32.0.16.1088 (held, D175; quiet-testbed.md 25), instance tb1, Goldberg]

**Stage 1, functional (job 20261118-062830-ec40, 06:28-06:42 EST; server.exe 5d0316cf, proxy 36cf7ff4, composite
e9f039dc, built at 6ce04c283 with the centre rule; layout bin x64-c29442e8-p58724e0; raw `F:/c5-64bit-work/0528/4853/jobs/
ec40/`, `summary_4853.py`)**: three launches, each exit 0, 0 LOUD lines, no box, stop and restore clean.

| Launch | Option | To in game | Private MB after load | Listener calls (built / skipped-server) | In-game check: live / size / freed endpoints | EXE grid field / margin cells | Server |
|---|---|---|---|---|---|---|---|
| Tiny t0009 n0 | on | 88.5 s | 1,224.4 | 0 / 22 | sound, 238 / 238 / 0 | (not read) | 0 farms at the load; the farm change below |
| huge n1 | on | 110.4 s | 1,376.3 | 0 / 1,255 (1,200 in the load burst) | sound, **754 / 754 / 0** | **0 / 0** | 373 farm plots -> 142 groups, 5,077 fields, 2,885 tracks in 1,239 ms on the worker; footprint 378,592 cells (15.7 ms) |
| huge n2 | off | 135.0 s | 1,384.8 | 1,252 / 0 (1,200 checks all sound) | sound, 9,035 / 9,111 / 76 | 317,839 / 395,861 | (draws the tap's fields) |

- **The switch path works end to end**: vpproxy.log `options: [Renderer] ServerFarmFields = 1 (from the file)`, the
  `server.cfg gets render_farmfields=1 render_farmfields_keep_tap=0` line, `VPSHELL_FARM_BUILDER=0 set`; server.cfg holds
  both keys; shell_composite.log `farm_guard: farm_builder=0: ...` 22 s before the swap, and the swap at 0 nodes / 0
  endpoints live (in time). The builder was never called: every listener call is `skipped-server`, the EXE's cut grid has
  no field and no margin cell (both set by the farm manager from the builder's records), and the layout graph holds
  **754 nodes and 754 endpoints with none freed** after the huge load: the bake's lines only (no delete ran, so the double
  free's path cannot be reached) [L]. layout_peek: verdict clean, free list sound, on and off.
- **Item 7, the seed**: the field's `mapSeed` is **0 live** on both saves (`farmfields: game seed 0x41A12B1F for load 3
  (the field's mapSeed 0x00000000, the plots' static seed 0x41A12B1F): mapSeed is 0, the static seed is used`; Tiny
  0xD29CFDD3) [L]: so `StaticPlotSeed` (13.2) is the seed in the product; 13.1's "[I until D's live check]" is answered.
- **Item 5, a farm built, pillaged, repaired and removed in play** (`--farm-change`, Lua on the job's own save copy, 6 s
  between steps, after 2 AI turns): Tiny plot (51, 15): build -> 16 fields + 9 tracks and footprint 2 (862 cells) in the
  next production (26 ms, one block laid out), pillage -> 32 fields (16 Crops_Damage copies) with the footprint unchanged
  (no new version), repair -> 16, remove -> 0 and footprint 3 (0 cells); huge plot (97, 17): 5,077 -> 5,086 -> 5,095 ->
  5,086 -> 5,077 fields, the footprint 378,592 -> 379,733 -> 378,592 cells, 92-97 ms per production on the worker, 0
  block graphs laid out (cached). Each change was in force before the next step (within 6 s; the plot table's
  `changed plots` read follows the Lua call at once) [L].
- **The coverage rule, one session** (huge n2, off: `decals_after_load.dtap` and `cut_16364_15.bin`, both after the 2
  turns, 7 s apart; `rule_check_4853.py`): the **EXE's own** 4,696 stock fields of that session rasterised by each rule,
  against the grid's bit-0x01 cells, cell by cell (river cells, bit 0x08, out of both). This tests the rule, not our
  layout (ours differs from stock's by design, 13.1):

  | Rule | grid cells | rule cells | both | grid only | rule only |
  |---|---|---|---|---|---|
  | centre (2c + 1, 2r + 1) | 317,839 | 301,863 | 295,198 | 22,641 | 6,665 |
  | **low corner (2c, 2r)** | 317,839 | 301,864 | 299,185 | 18,654 | **2,679** |
  | any overlap (centre + every cell an edge crosses) | 317,839 | 327,093 | 302,565 | 15,274 | 24,528 |
  | any of the four corners | 317,839 | 327,691 | 302,565 | 15,274 | 25,126 |

  Where the differences sit (distance of the cell's sample point to the nearest outline edge of the recorded fields, in
  samples: < 0.5 / < 1 / < 1.5 / < 2 / < 3 / < 4 / >= 4 or none within 16): low corner, rule only 2,645 / 34 / 0 / 0 / 0 /
  0 / 0 (every one on an outline: edge ties); low corner, grid only 2,989 / 317 / 264 / 270 / 477 / 394 / **13,943**;
  centre, rule only 3,900 / 2,279 / 486 / 0 ...; centre, grid only 4,064 / 2,640 / 780 / 274 / 465 / 389 / 14,029. So
  about 14,000 grid cells lie 4 or more samples from every recorded field: stamps whose field is not in the decal set
  (whole fields, not edges) [I: polygons that did not ear-clip (no decal, 4.2 step 2), or stamps left by withdrawn
  faces]; the rest of the disagreement is along outlines, where the low corner leaves the fewest cells the grid lacks.
  `FieldFootprint` uses the low corner since b20004871 (it was the centre). [L for the counts; **I** for the EXE's rule:
  only the best of the four rules tried; 0x6287A0 not read.]

**Stage 2, item 3 and item 6: two quiet series** (jobs 20261118-065449-d88f, 06:55-07:39, and 20261118-065633-5903,
07:40-08:15 EST; server.exe `76654287` (low-corner rule, built at 5ad951a73), proxy `5d31a130`, composite `e9f039dc`,
layout bin x64-c29442e8-p58724e0; save TB-HUGE_0350 (c499d3c6), D33 with Overlay on, statistics logging as tb1's base install
(not read for this series; no turns are played, only the load is measured); arms interleaved on x4, off x1; quiet: alone, no other job, foreign cores 0.14 mean, CPU 21 % mean, the
interactive session logged on (D74), so `undisturbed` false; raw `F:/c5-64bit-work/0528/4853/jobs/d88f`, `.../5903`;
`summary_4853.py`, `perf_4853.py`):
- **Item 3 [L]: 20 huge loads with the option on** (21 with stage 1): in every one the composite's listener took 1,200
  type-59 calls in the load burst, **all `skipped-server`, 0 builder calls**; the in-game check at a Present **754 live,
  754 size, 0 freed endpoints** (the bake's lines only, identical in all 21); layout_peek clean, free list sound; footprint
  378,586 cells under 5,077 fields in each. **0 crashes, 0 boxes, 0 LOUD lines** in the 25 launches of both series (5 off
  launches: 1,200 builder calls each, all 1,200 checks sound, 8,919-9,105 endpoints live, 62-75 freed). The crash chain of
  `crashes-huge-load.md` 8 cannot run with the option on: no builder call, no node delete in the farm graph (0 freed).
- **Item 6** (on - off; on n = 20, off n = 5, so grade B for the off arm by PERFORMANCE.md's six-run rule; Welch 95 %):

  | Metric | on: mean (sd), median, [min, max] | off | on - off [95 %], detectable |
  |---|---|---|---|
  | load burst end: composite start -> last type-59 listener call (s) | 55.72 (0.68), 55.62, [54.79, 57.27] | 65.88 (0.34), 65.82, [65.61, 66.46] | **-10.16 [-10.63, -9.69]**, 0.60 |
  | map up (in-game line - 10 s; s) | 50.82 (0.66) | 57.88 (0.34) | -7.06 [-7.52, -6.59] (biased: without the option the line waits for the burst's 2 s quiet) |
  | the harness's launch -> in game (s) | 110.64 (0.74) | 134.88 (0.08) | -24.24 (quantised by its 25 s poll: one poll period) |
  | EXE private MB after the load (+30 s settle) | 1,373.73 (2.52), 1,373.5, [1,369.2, 1,380.0] | 1,387.26 (2.13) | **-13.53 [-16.15, -10.91]**, 3.1 |

  Why [C for the paths, L for the counts, I for the split]: with the option the builder's 1,200 calls of the load return
  at once instead of running the layout of 373 farms (0x6016F0, the face and track passes; 121-235 ms per farm on the
  workstation, 1.4) and posting their records to the farm manager; the 10 s is the builder's work inside the load gap on
  oldpc, and the 13.5 MB is the graph's nodes and endpoints (about 8,200 live entries fewer), the builder's per-plot table
  entries and the farm manager's curves and decal records that are no longer made [I: no allocation profile was taken].
  The server's own layout (1.2-1.3 s on its worker, off the render thread) runs in parallel with the EXE's load.
- **The queue guard's drops** (ticket 0492): no `queue guard: dropped` line in any vpproxy.log of the 28 launches: 0 drops
  of any type, farm types included [L].

**Stage 3, item 8: the identity pair (D164)** (job 20261118-065728-b627, 07:57-08:17 EST, the stage 2 builds; TB-TINY_0009V
(27420a41...), turn 9; both arms the same route (offscreen windowed launches through the harness's `cli.py launch`), on
then off, 30 AI turns each with the harness's turn driver; `proxy/tools/live/eq_stats.py` dumps after the load and after
the turns; `OUT/identity_4853.json`) [L]: after the load **VERDICT IDENTICAL: 0 differing keys** (362 keys each, turn 9);
after 30 turns **VERDICT IDENTICAL: 0 differing keys** (434 keys each, turn 39). Both exit 0, 30 turns played, 0 LOUD, no
box. The AI built farms during the turns: 40 listener calls in each arm, all `skipped-server` (on) / `built` (off), and the
server laid out the new farms in play (1 farm plot -> 13 fields, then 2 -> 23 fields, 27 ms on the worker): item 5 in
ordinary play as well. The game state does not depend on the option: the layout is a picture only.

**Stage 4, item 4: A/B pictures, DIAGNOSTIC (D112: a test layout of this branch, not the full shipped system)** (on: job
20261118-065703-7cb9, 08:10-08:14; off: job 20261118-082024-9548, 08:21-08:26, alone after the first off launch of 7cb9
was refused by the harness's free-commit floor (6 GB) while the identity job ran beside it; nothing ran, restore and
verify clean). The same save, turn 350, 0420's camera tour: 4309's six plan cameras (s00 101,25; s01 111,25; s03 101,35;
s06 84,31; s15 96,34; s24 90,16) and two found in game (fog_forest 76,41; border_forest 94,39); the composite's back buffer,
1914 x 1051, popups closed first (D114). Visual Checks on oldpc: on `vc-20261118-081209-oldpc-77e5`, `...081221-...-186a`,
`...081232-...-6aa1`, `...081244-...-9043`, `...081255-...-dfc9`, `...081307-...-7c56`, `...081319-...-aa18`,
`...081330-...-ac2b`; off `vc-20261118-082349-oldpc-6a3d`, `...082401-...-92c6`, `...082412-...-cc29`, `...082423-...-caf3`,
`...082435-...-9ba0`, `...082447-...-fa1c`, `...082458-...-fc54`, `...082509-...-c66d` (same order); files under
`F:/c5-64bit-work/0528/4853/jobs/7cb9|9548/out/TB-HUGE_0350/<arm>/`. Looked at (s15, Alexandria): both show field
patchworks with tracks around the farms; the server's cover more of each farm plot (13.1: no bare farms, fields 15 %
larger at the median) and its patterns differ from stock's, as designed; style and tint come from the same
farm_types.xml textures. Not judged here: the look is the owner's to accept (D66); farm buildings at the plot centre and trees
under fields are named for him to check in the pairs, not measured. [CORRECTED 2026-11-21, ticket 0637, `../02-exe-internals/farm-field-double-free.md` 4.4 and 4.7: with the builder skipped the farm buildings are NOT DRAWN AT ALL [L, pictures on build 26577625]; 'at the plot centre' was a code reading never looked at in a picture]

### 14.8 The other EXE faults in the farm chain: the track strip builder 0x55D0C0 and the face walk 0x7A3F0A (lead-0528 #5372, #5379, #5471; MC #5367, #5377; written 2026-11-18 from the data of 14.7, no new run)

Ticket 0537 found two more EXE faults in the farm chain: the track strip builder 0x55D0C0 writes past its vertex vector
when an edge has 0 points, and 0x7A3F0A is a face walk on a stale handle. Whether `ServerFarmFields = 1` removes them:

- **The EXE's own farm-track and farm-field decal records are 0 with the option on** [L, read only:
  `shell/evidence/0528/tracks_4853.py` over the newest decal tap image of every launch of 14.7 (`decals_after_load.dtap`,
  the EXE's set before the server adds its own; a track = priority 0.6 with albedo and heightmap NO_TEXTURE, the
  Detail_Road class, terrain-decals.md 8.4)]. **22 huge launches on: 0 field and 0 track records in each.** 6 huge
  launches off: 28,226 fields and 14,826 tracks (2,446-2,499 tracks per load). Tiny: 2 launches on, 0 / 0; 1 off, 27 / 17.
  The same holds after 2 AI turns and the farm change (stage 1) and after 30 AI turns with farms built in play (stage 3).
- **The face walk 0x7A3F0A**: it is part of the face pass that runs only inside a builder call (4.1: 0x4A8990 on this+0xC70,
  after the layout 0x6016F0; 10.4: 0x7A3BC0's walk) [C for the call path]. With the option the builder is called 0 times
  in every load (14.7: every type-59 listener call `skipped-server`), so no face is made and no face handle exists to go
  stale: **the input is gone** [L for 0 builder calls and 0 freed graph entries; C that the face pass has no caller outside
  the builder, 10.2].
- **The track strip builder 0x55D0C0**: its one caller is 0x6E27D0 (5.2; 10.1: the only reader of mask bit 0x10 found),
  which builds a strip per **dirty** graph cell and is reached from three places (5.1): (a) 0x61FD40 after each builder
  call: **never runs** with the option [L: 0 builder calls]; (b) CreateTerrain 0x709B50 at 0x709FD3, and (c) the A33
  listener 0x79D040 for a rectangle another poster marks dirty: **these still run** [C]. What the data shows for (b) and
  (c): 0 track records in every on load, so no call produced a strip; the margin bit 0x10 that a strip needs on every point
  is absent from the whole grid (0 margin cells, stage 1), so 0x55D0C0 can make no strip there. What it cannot exclude:
  (1) **a call that produces nothing**: 0x6E27D0 may still call 0x55D0C0 for a dirty cell; the cells are made dirty by the
  weld 0x77C1C0 (5.1), and the terrain bake inserts its own lines through 0x49CED0 and the weld (10.2), so the bake's 754
  lines could leave dirty cells at CreateTerrain [I]. Whether 0x55D0C0 loops over a cell's edges before testing the mask
  (and so meets a 0-point edge) is 0537's call path, not read here. (2) **Callers outside the farm chain**: only the three
  above were found [C for the callers of 0x6E27D0 named in 5.1; I that there is no other]. The 0-point edge of 0537 needs
  an edge the builder's layout made (the bake's lines are polylines with points) [I]. So: **the option removes the farm
  chain's calls into 0x55D0C0** ((a), [L]) and every strip it could make ([L]); **calls from the bake and A33 paths on the
  bake's own cells are not excluded** [I, to confirm: 0537's exact call path, or a read of the graph cells' dirty bytes
  (+0x14) and edge lists after CreateTerrain on an on load].
- **The weld walk 0x77C1C0 is the EXE's own defect** [L, epic 0398, queue #5465]: epic 0398 hit the crash at VA 0x77C6E6
  in the weld walk on the **stock** route (unmodified VP, no proxy; instance c5h-h, 2026-11-18 08:24:55, save
  ATOMIC-HUGE-T350, the 9th in-process reload). So the farm chain's faults belong to the stock EXE, not to the patch, and
  `ServerFarmFields = 1` is the product's way around them: with it the builder, its layout and the faces never run (14.7),
  and the weld then runs only for the terrain bake's own lines (10.2) [C for the callers; I that the bake's inserts
  cannot reach the faulting walk: whether 0398's reload crash was reached from a builder call or from the bake is not
  read].

### 14.9 M2: `ServerFarmFields = 1` on 9074's oversize saves (ticket 4854, the Architect's M2, worker-4854, 2026-11-18 from 09:06 EST)

The Architect's M2 (`../07-architecture-decisions/architect-2026-11-18-farm-graph-routes.md` 4 and 6.4): the option on
9074's failing oversize saves (180 x 112 plots, 80,640 grid cells, 42 majors + 20 city states, Marathon; stock failed 9
of 14 loads from T1100). Scope by lead-0528 #5506: a check load first (does the product load these 43-civ saves with
`ServerRenderer = 1` at all), then 12 loads x 2 AI turns, one T1650 load carried to 5 turns; what is measured is the
mechanism at load (builder calls 0, the graph line clean, cell 65,535 read), since in-play farm events were shown live by
14.7.

**Set-up.** oldpc through the testbed (`shell/evidence/0528/submit_4854.sh check | series N T...`, both slots, headed, not
quiet: no timing is claimed), 4853's driver `shell/evidence/0528/live_4853.py` (product path, prep and restore by hash per
launch, the option per arm: on = `ServerFarmFields = 1`, off = absent). GPU RTX 2080 with NVIDIA 32.0.16.1088 (held, D175;
`../06-tooling-and-workflows/quiet-testbed.md` 25). Layout (`shell/evidence/0528/build_4854.sh`): the x64 DLL is 0334's
43-civ build **m43 `cf981126...`** of the older pin 04b1211b8 (`Release-5.4.8-51-g04b1211b8+m43 Copy`), **not a pin: the
COMMON 18 exception that 0459 and 0491 had** (lead-0528 #5457; no 43-civ build exists at the current pin c29442e8c);
`server.exe` `aa8fbd76...` and the proxy `979f2431...` built at this repository's `task-4854-farm-graph-line` 1b6adbfce
against a copy of the m43 deps, from a git checkout of the fork at 04b1211b8 (`srcgit_4854.sh`: `build_server.py` takes a
source other than `deps/dll.lock`'s only as a git tree with `--unlocked`; the checkout's tree equals 0334's plain copy,
`diff -rq`); the composite `0c5a4fbd...` of `task-4855-farm-guard-rule-2` e74a891ca (4854's graph line and 4855's rule 2).
Saves: copies of `F:/c5-64bit-work/9074/saves/TS1.20-T<turn>.Civ5Save` named `TB-4854-T<turn>` (T1100 `968d7ccd...`,
T1150 `c69019d0...`, T1300 `668812fc...`, T1600 `43e420fe...`, T1650 `16921518...`), shipped with each job; the originals
are not touched.

Summaries: `shell/evidence/0528/summary_4854.py JOBDIR...` (fetched jobs under `F:/c5-64bit-work/0528/4854/jobs/`): per
launch the exit, crash box, time to the map, turns, private memory, the listener's event classes, the burst lines, every
graph line (farm-builder-guard.md 9) with "clean" = no LOUD, no list not sound, no listed node without points or with the
freed bit, no never-filled handle, live + freed = size, and cell 65,535 empty and sound.

**The check load: the product loads these saves** [L, job 20261118-090609-9a17, 2026-11-18 09:06-09:26 EST, instance tb1,
composite `0c5a4fbd...`, adapter "NVIDIA GeForce RTX 2080" in the server's log]: `TB-4854-T1100` with `ServerRenderer = 1`
and `ServerFarmFields = 1` reached the map in 195 s and played 1 AI turn (to 1101); exit 0, no box, the harness's stop
clean; the EXE's private memory 1,880 MB after the load, 2,010 MB after the turn. The listener got 4,541 calls (4,533 in
the load's burst, 8 in play), **all `skipped-server`, 0 builder calls**. All 5 graph lines clean: 360 x 224 = 80,640
cells, **5,578 nodes, all kind 0, 5,578 endpoints** (the terrain bake's coastline pieces only; 0542 counted 5,578-5,579
on this map), no freed node or endpoint, every cell list sound, **cell 65,535 count 0, sound**, 0 nodes with the id
0xFFFF, 0 never-filled handles, 0 handles not named back, scan 0.26-1.91 ms; the vertex vector's size word 0 (capacity
245, then 260 in play [I: a track re-lay grew it, A33 or the EXE's other strip users; not read]). So with the option on the bake's pieces are each filed once and the graph
stays as the bake left it [L, one load]. The same job's second launch (the option off, 4855's rule 2 in force) is in
`../02-exe-internals/farm-builder-guard.md` 10.

**Every M2 record** (MC #5463 via lead #5464): x64 DLL m43 `cf981126...`, `server.exe` `aa8fbd76...`, proxy `979f2431...`;
**COMMON 18 exception, MC #5458 answer: no 43-civ build exists at batch 4**; M2 results are not compared with batch-4 game
results.

**Stock's failures in the same window** (MC via lead #5514; `../08-performance/marathon-stress-t2000.md` 4 and 4.1, stock
EXE with the 43-civ guard DLL `204fbfcc...`, c5h-i, 2026-11-16/17, n = 1-3 per save) [L]: all **9 of stock's 9 failures in
14 loads from T1100 to T1650 came within the load plus 2 turns**: 8 in the load (T1100 run 3, T1150, T1300, T1450, T1550,
T1600 crashes; T1200, T1650 hangs) and 1 in the first autoplay turn (T1100 run 1, hang). On the five saves M2 uses, stock
failed **6 of 7** loads (T1100 2 of 3; T1150, T1300, T1600, T1650 each 1 of 1). Against stock's 9 in 14 (p = 0.643 per
load), 12 clean loads have the probability (5/14)^12 = 4.3e-6; against the 6 in 7 of these saves, (1/7)^12 = 7e-11.

**The series, 11 loads x 2 AI turns** [L, job 20261118-091849-abc7, 2026-11-18 09:25-11:35 EST, instance tb1, composite
`9913e153...` (task-4855-farm-guard-rule-2 9127dc998: 4854's graph line and 4855's rule 2, which does not act with the
option on), the rest as the check; adapter "NVIDIA GeForce RTX 2080" in every server log; job rc 4 from the two T1600
launches below; `summary_4854.py` over the fetched job in `F:/c5-64bit-work/0528/4854/jobs/series2/`]:

| # | Save | Map s | AI turns | Exit | Box | Listener calls (all skipped-server) | Graph lines, clean | Private MB load -> end |
|---|---|---|---|---|---|---|---|---|
| 0 | T1100 | 194.5 | 2 (to 1102) | 0 | no | 4,552 | 9, 9 | 1,882 -> 1,988 |
| 1 | T1150 | 195.6 | 2 (to 1152) | 0 | no | 4,708 | 7, 7 | 1,895 -> 2,055 |
| 2 | T1300 | 211.2 | 2 (to 1302) | 0 | no | 5,156 | 9, 9 | 1,906 -> 2,019 |
| 3 | T1600 | 219.8 | 0 (wait ended at 177 s, turn 1600 at the stop) | 4 | no | 5,717 | 5, 5 | 1,976 -> 2,023 |
| 4 | T1650 | 219.8 | 2 (to 1652) | 0 | no | 5,775 | 7, 7 | 1,989 -> 2,116 |
| 5 | T1100 | 195.2 | 2 (to 1102) | 0 | no | 4,552 | 9, 9 | 1,881 -> 1,994 |
| 6 | T1150 | 197.6 | 2 (to 1152) | 0 | no | 4,708 | 7, 7 | 1,896 -> 2,060 |
| 7 | T1300 | 211.2 | 2 (to 1302) | 0 | no | 5,156 | 9, 9 | 1,888 -> 2,023 |
| 8 | T1600 | 217.4 | 0 (wait ended at 179 s, turn 1600 at the stop) | 4 | no | 5,717 | 5, 5 | 1,963 -> 2,024 |
| 9 | T1650 | 222.5 | 2 (to 1652) | 0 | no | 5,775 | 7, 7 | 1,985 -> 2,106 |
| 10 | T1150 | 211.4 | 2 (to 1152) | 0 | no | 4,708 | 7, 7 | 1,888 -> 2,052 |

- **With the check load, 12 loads of the five saves: 0 crashes, 0 boxes** (the dialog watcher's state "none" at all 22
  status reads of the series, crash lists empty), **0 builder calls** (61,065 listener calls, all `skipped-server`), and
  **all 86 graph lines clean**, each with the same graph: 80,640 cells, 5,578 nodes and 5,578 endpoints, all kind 0 (the
  bake's), every list sound, cell 65,535 count 0 and sound, 0 nodes with the id 0xFFFF, 0 never-filled handles; the
  vertex vector's size word 0 at every line (capacity 28, 245 or 260); scan at most 2.23 ms [L]. So with the option on
  the graph is the bake's and does not change in play: the farm graph's faults have nothing to act on [L, 12 loads, up to
  2 turns].
- **Against stock** (above): stock failed 9 of 14 loads in this window and 6 of 7 on these five saves; 12 clean loads
  here. The mechanism (the builder never runs, the graph never changes) is what removes the fault, not a rate (D185).
- **The two T1600 loads played no turn: not a crash** [L for the facts; I for the cause]: in both, the load was clean
  (graph lines clean, 0 builder calls) and the first AI turn's wait (`tools/harness/turns.py wait`) ended at 177-179 s
  with "channel silent for 64 s (leader screen or popup?)" twice (rc 2, `WAIT_STALL`: turns.py's default stall for the
  first turn of a launch is 60 s, `STALL_MIN_S`); the game was in game again 25 s later (`frontend.py ingame` 0), the turn
  still 1600, no dialog, no crash record, the EXE alive, and the driver stopped it cleanly (exit 4 = turns not played).
  The server's log has no fault (the engine ended at the harness's stop, exit code 15). 0491 measured a first AI turn of
  453 s on T1100 on the product path, and stock's turns on these saves ran 200-480 s, so a first turn of T1600 whose
  main-thread stretches without a Lua answer pass 60 s is plausible [I]. **To confirm**: job 20261118-113836-573d, T1600
  again with `live_4854.py` (the same driver with `turns.py wait --stall 600`), 2 AI turns.

**T1650 carried to 5 turns** [L, job 20261118-091941-2add, 2026-11-18 11:35-12:00 EST, same layout and composite]: map in
225 s, **5 AI turns to 1655** (turn waits 211, 418, 196, 208, 168 s, every rc 0), exit 0, no box (watcher state "none" at
both status reads), 5,781 listener calls all `skipped-server`, 0 builder calls, **13 graph lines, all clean** and the same
graph as above (5,578 kind-0 nodes, cell 65,535 empty and sound); private memory 1,990 -> 2,108 MB. So **13 loads in all (10
played 2 AI turns or more, the check load 1, the two T1600 loads none), 0 crashes, 0 boxes, 0 builder calls, 99 graph lines
clean**.

**The T1600 rerun with a longer turn-wait stall: the turns complete** [L, job 20261118-113836-573d, 2026-11-18 12:00-12:15
EST, same layout and composite, `shell/evidence/0528/live_4854.py` = live_4853.py with `turns.py wait --stall 600`]: map in
221 s, **2 AI turns to 1602** (turn waits 315.5 s and 227.7 s, rc 0), exit 0, no box (state "none" at both reads), 5,725
listener calls all `skipped-server`, 0 builder calls, 9 graph lines all clean (the same bake-only graph). So the two T1600
launches of the series stopped because the harness's turn wait gave up (turns.py's first-turn stall of 60 s against a
first AI turn of 315 s whose Lua channel was silent for longer stretches), not because of a fault: **cause confirmed [L]**.
Follow-up for the harness's owner (not this ticket's files): a first-turn stall of 60 s is too short for 43-civ late
Marathon saves on the product path (first turns 315-453 s: this run, 0491); a caller playing such saves should pass
`--stall` (as live_4854.py does), or turns.py could scale its first-turn default by map size.

**M2 in all** [L]: 14 loads of the five saves with `ServerFarmFields = 1` (the check, 11 in the series, T1650 x 5, the
T1600 rerun): **0 crashes, 0 boxes, 0 builder calls (72,571 listener calls, all `skipped-server`), 108 graph lines, all
clean**, cell 65,535 empty and sound in every line, the graph the bake's 5,578 kind-0 nodes throughout; 11 loads played 2
AI turns or more (T1650 5), the check load 1, the two T1600 series loads none (the harness's stall, above). Stock failed 9
of 14 in the same window (6 of 7 on these saves).

## 15. Rule 2's premise: where the farm chain can put a graph point (ticket 4855, worker-4854, 2026-11-18 08:36 EST) [C from the sections cited, I where marked]

The Architect's rule 2 (`../07-architecture-decisions/architect-2026-11-18-farm-graph-routes.md` 6.1 item 2) skips the
builder calls of plots near the plot holding cell 65,535. It is exact only if **no step of the farm chain creates a graph
point outside the calling plot's rectangle** (1.3), so that a node can be filed in cell 65,535 (by one of its own points:
the weld files a node by its **middle** point, `../02-exe-internals/farm-field-double-free.md` 3.2 on branch
`task-0542-bake-farm-graph-inserts`, correcting 2.4's "first point") only through a call whose rectangle meets that cell.
Read only, from this file and `crashes-huge-load.md` 2 (D83); no code read for it. The bake's own lines are ticket 0542's,
not this; 0542 read the same steps in the code (its 3.4 (ii)) and found the same three exceptions.

| Step | Points it creates | Where they lie |
|---|---|---|
| The trace (3.4): every stored point is a tracer step | new | inside the rectangle: the trace stops when a step leaves it (1.3: clipped, strict inequalities) [C] |
| Point seeds, kind 0 (3.3) | the start junction | strictly inside the rectangle [C] |
| Offset seeds, kind 1 (3.3, 3.4) | the start junction, 0.65 off a segment midpoint of a new line | the midpoint is inside; the start can be **up to 0.65 units outside** if the parent segment runs within 0.65 of the border: no test of the start against the rectangle is written down [I: not read either way] |
| Continuation seeds, kind 2 (3.3) | none at the start (an existing junction); the trace's steps as above | as the trace [C] |
| A hit (3.4): the split 0x5A9C90 and its junction | the hit point, inserted into the other edge | on the step's segment, so inside (or as the step); a snap moves a point by at most 0.01 onto an existing edge [C] |
| The merge 0x46F8B0, the reversal 0x52F0E0, the collinear merge 0x524730 | none: they join, reverse or drop points | - [C] |
| The weld 0x77C1C0 | none new: it writes a junction's reference point (an existing point) into other nodes' ends | where that point already was [C, `crashes-huge-load.md` 2] |
| A wrap hit, kind 2 / 4 (3.4) | the trace goes on with its frame shifted by +-1 world width (graph +0x6C) | the rectangle's image one world width over [C for the shift; I that it is the map's x seam] |
| Prune, face pass, withdrawals, track clean-up and re-lay (3.5, 3.6, 4.1, 5) | none (faces and polygons are not node points; deletes) | - [C] |

**Answer: the premise holds up to a margin of 0.66 units** (the offset start, 0.65, plus a snap, 0.01), and modulo the
x wrap [C for every step but the offset start, I for that one]. Pieces made by a split keep points an earlier call made,
so they lie in that earlier call's rectangle; a far farm's call can still re-weld a node that some other call or the bake
filed in cell 65,535, which is why rule 2 (a) reads that cell's count before every call.

**The hex-distance bound with the margin** [I, arithmetic from C sizes]: the layout frame is a regular hexagonal lattice
(plots 64 apart in x, rows 55.43 apart, odd rows shifted 32: `../02-exe-internals/camera-and-picking.md` 5), so the plot P
whose centre is nearest the centre of cell 65,535 is the hexagon holding it: |dx| <= 32 and |dy| <= 36.95 from P's centre.
A call of plot Q can put a point into the cell only if Q's rectangle grown by 0.66 meets it: |Qx - x| < 64 + 0.66 + w/2
and |Qy - y| < 55.43 + 0.66 + h/2 for the cell's centre (x, y) and size w x h (32 x 28.70 on 9074's map; w = 32 and h =
(H + 4) / (2H) x 55.43 on an H-row map, since the grid is 2W x 2H cells over the map's H + 4 rows of height,
`../02-exe-internals/farm-field-double-free.md` 2.6). So |Qx - Px| < 112.66 and |Qy - Py| < 107.4 on 9074's map: Q is in
P's row or the next one up or down (two rows are 110.85 apart), in P's row at most one column over (128 > 112.66), in a
neighbour row at x offsets +-32 (distance 1) or +-96 (distance 2; +-160 is past 112.66). **Every such Q is within hex
distance 2 of P**; in y the bound holds for every map of 15 rows or more (h/2 < 17.81). The code checks it on each
oversize grid (4855: every plot within hex distance 4 of P whose grown rectangle meets the cell must be within distance
2, else the rule falls back to no builder call on that grid, LOUD).

**What would confirm the [I]**: a static read of the offset seed's start (does the layout test it against the
rectangle?), or the graph line's cell 65,535 count staying 0 on oversize loads with rule 2 in force (4854's M2 and 4855's
live load). *Confirmed [C] by ticket 0542 (its 3.4 (ii), 2026-11-18, read in the code): the offset line's start is not
tested against the rectangle (up to 0.65 outside), the snap is 0.01, the wrap shifts by one world width.*

**The coastline exception (MC #5493 from 0542's 3.4 (ii), added 2026-11-18 08:45)**: a split refiles both halves of an
existing node by their middle points, and those points may come from another call. For farm nodes that call is within
radius 2 as above. For the bake's coastline pieces (kind 0, up to 72.6 units long) it is not: a farm call up to **hex
distance 4** away can split a piece that has a point in cell 65,535 and file its halves there. So rule 2 reads the live
kind-0 nodes once per load (at the first builder call, after the bake): none whose box meets the cell (or its image one
world width over) -> radius 2 (19 plots); some -> **radius 4** (61 plots). On 9074's and 4203's maps no bake piece has a
point in the cell or a box meeting it (0542 3.3 (2)), so radius 2 applies there. Code and rule text:
`../02-exe-internals/farm-builder-guard.md` 10.

## 16. The farm buildings drawn by the server, part A: the records (ticket 0653, decision D230, impl-0653, 2026-11-21 from 18:02 EST) [C for the code, L where cited, I where marked]

The owner's order (2026-11-21, D230): the server draws the farm buildings, in the first beta, because the first beta ships
`[Renderer] ServerFarmFields = 1` (D223, K1) and with it the EXE makes no farm building at all
(`../02-exe-internals/farm-field-double-free.md` 4.4 [L]; the mechanism: `cities-and-landmarks.md` 2.6.1 [C], one-off
9078: the building is made only on the builder's type-5 farm-end post). Part A (this section) makes one record per farm
and teaches the model library the FARM rows; part B (ticket 0654) draws them. Code: `render/include/c5r/farmfields/
buildings.h`, `render/src/farmfields/buildings.cpp` (the records), `render/src/landmarks/library.cpp` (`farmRows`,
`rowFile`, `farmBuildingModel`), `server/src/render/srv_farmfields.{h,cpp}` (the producer's list). Nothing is drawn by
this part and no default changes: no file the scene, the model layer or the bundle reads was touched, so the scene
library replays unchanged.

### 16.1 Step 0: what stock shows (EXE build 4390913, c5h-a, 2026-11-21 18:07-18:14 EST) [L pictures, C the model files]

One stock launch (the patch off: the instance carries no proxy, `provision.py verify`: `"proxy": []`), a copy of
`C5H-VP8P-HUGE_0350` (turn 350), mods form cp, vp, eui, frame-capped, no turn played, no autosave written; the copy
removed and the front end restored by hash after the stop. Four DIAGNOSTIC pictures (never in git, COMMON 27) in the
Visual Checks feed, issue 0653 (`vc-20261121-181130-workstation-6b39`, `vc-20261121-181152-workstation-35c1`,
`vc-20261121-181234-workstation-9dc9`, `vc-20261121-181247-workstation-4ec6`; files `C:/c5-64bit-work/0653/s0/pics/`);
the survey through the Lua channel (`C:/c5-64bit-work/0653/sh/s0_probe.py`, output `C:/c5-64bit-work/0653/s0/survey.json`;
scratch, not committed).

- **The save**: 432 farm plots (true or revealed improvement IMPROVEMENT_FARM), all revealed to the active team, 57
  visible, 304 in the fog (revealed, not visible, the revealed improvement a farm), 26 pillaged, 0 without an owner.
  **Every living player is in era 5 (Modern)** at turn 350, majors and city-states alike, so every farm asks for era
  level 4 and takes the rows of level 3 (below). The producer counts 373 farm plots on this save (the double-free
  file's 4.2): it takes only plots whose layout the EXE asked for (13.2); the survey counts every plot.
- **(c) The rows** (this load's database, `DB.Query` on `ArtDefine_Landmarks`): 24 rows with `LayoutHandler` FARM. The
  six farm-building rows: improvement `ART_DEF_IMPROVEMENT_FARM`, resource `ART_DEF_RESOURCE_NONE`, state `Any`, scale 1:
  Era `Ancient` with `Medieval_Farm3`, `Medieval_Farm2`, `Medieval_Farm1` and Era `Industrial` with `Modern_Farm2`,
  `Modern_Farm3`, `Modern_Farm1` (table order). The other 18 are WHEAT, MAIZE and RICE rows, one per state
  (`UnderConstruction`, `Constructed`, `Pillaged`) and era (Ancient, Industrial), models `HB_Medieval_Farm`,
  `Medieval_Farm`, `PL_Medieval_Farm` and the Modern ones; no manager chooses them (`cities-and-landmarks.md` 2.3). Six
  SNAPSHOT rows of the farm improvement (wheat, maize, rice; states `Any` and `UnderConstruction`) have model NULL.
  So the huge save's farms all show **Modern_Farm** buildings [L: the pictures show the modern set's metal silos and
  white barns on every visible farm].
- **The model files** [C, file read with the render tests' `c5r_gr2_facts`, 2026-11-21]: each of the six `.gr2` files
  holds **three models**, e.g. `modern_farm1.gr2`: `PL_Modern_Barn` (meshes `PL_Farm_Decal`, `PL_Modern_Barn`),
  `HB_Modern_Barn` (`HB_Farm_Decal`, `HB_Modern_Barn`) and `Modern_Barn` (`Tree cut`, `Farm_Decal`, `Modern_Barn`). Each
  model has one BuildingShader barn mesh (materials `PL_Mod_Farm`, `HB_Mod_Farm`, `Mod_Farm`); the `*_Farm_Decal` meshes
  are LandmarkShader ground decals and `Tree cut` a LandmarkShader_Stencil tree cut, which the model library does not
  pack (`packedMesh`), so the library's 3 parts per file are the three barns. `medieval_farm1.gr2` has the same form.
- **(a) A pillaged farm** [L, pictures `pillaged-94-26-close`, `pillaged-93-27-nearest`]: the fields show the
  Crops_Damage decals, smoke columns rise (`ART_DEF_VEFFECT_FARM_PILLAGED_SMOKE_01`), and the building stands where an
  intact one would, **as a dark, broken structure**, not the intact barn. With the model file above this reads as the
  file's `PL_` model [I: which model the farm manager draws per state was not read in the code; the picture and the file
  agree]. So part B draws the `PL_` model for a pillaged farm (`ModelLibrary::farmBuildingModel(file, 3)`), the `HB_`
  model for a half-built one [I: no half-built farm was in view] and the plain model otherwise. The smoke needs route S's
  effects (D211) and is in neither part.
- **(b) A farm in the fog** [L, picture `fog-46-19-close`, player 7's farms around 46,19]: the fields and the building
  are drawn, fogged like the terrain and the landmarks around them. So the records carry every revealed farm, visible or
  not, and part B fogs them with the layer's model fog.
- **Why no building with the option on**: answered by 9078 (`cities-and-landmarks.md` 2.6.1 [C]); step 0 was not spent
  on it (the MC's word via dispatcher-B, 2026-11-21 #9661).

### 16.2 The rows: the model library's FARM table [C; tests L offline]

`ModelLibrary::farmRows(FarmRowChoice&)`, over the `ArtDefine_Landmarks` rows `load` already read from the install's
database copy (D83: nothing of the game committed): every row with `LayoutHandler` FARM is counted; it is a
farm-building row when its improvement is `ART_DEF_IMPROVEMENT_FARM`, its resource empty, `ART_DEF_RESOURCE_NONE`,
`RESOURCE_NONE` or `NONE`, and its state `Any` or empty (the farm manager's selection, `cities-and-landmarks.md` 2.5).
Its level = `farmRowLevel(Era)`: `Any` or empty -> 0, else the game era's level (Ancient 0 -> 1, Industrial 4 -> 3) [I:
that the EXE maps a row's era through the same table as a farm's; with it levels 2 and 4 have no rows and fall back to 1
and 3, as the record's row "steps down to the next lower era with a row", 2.6.1 (2) there]. At most 3 per level, table
order. **Refused**, each with a text naming the row (the producer writes one LOUD line per refused row, D118, never a
stand-in): a NULL model, a `.fxsxml` missing, unreadable or meshless, a model file the library could not lay out, an Era
text that is no era, a fourth row of one level. `rowFile(row)` hands out the row's laid-out file (buffers through
`resident`, parts through `files()[f].layout.parts`), `farmBuildingModel(file, state)` the file's model for a farm's
state (16.1; its parts in `files()[f].modelParts`). On the workstation's pure_vp install with the 0327 database copy
(CP + VP) [L offline, 2026-11-21]: 24 FARM rows; levels 0-4: 0, 3, 0, 3, 0; refused 0; level 1 rows 17, 18, 19
(`medieval_farm3/2/1.gr2`), level 3 rows 20, 21, 22 (`modern_farm2/3/1.gr2`); 3 packed parts each; every file has its
built, `HB_` and `PL_` model.

### 16.3 The record and the placement rule [C, `buildings.h`]

One `c5r::farmfields::FarmBuilding` per farm plot of the producer's input, which is the revealed-only table of 13.2 (a
farm built in the fog has no record until its plot is laid out for the active team, as its fields; the Architect's point
of 2026-11-21), in the input's order (ascending by y, then x):
- **plot, x, y; owner** (the plot table's owner, -1 none); **era** (the owner's `PlayerRecord::era`, -1 unknown or no
  owner); **eraLevel**: 0-2 -> 1, 3 -> 2, 4 -> 3, >= 5 -> 4, and **1 for a farm without an owner or with an unknown era**
  (the Architect's point). The player rows come from the feed's player table, which the DLL side refreshes only with
  `render_units=1` (`srv_remote_feed.cpp` `UnitsAtLeaveUpdate`) [C]: the first beta's set has `ServerUnits = 1`; without
  it every farm takes level 1 [C].
- **row** = `rows[rowLevel][pick % count]`, rowLevel the highest level <= eraLevel with rows (the EXE's step-down);
  **pick** = a hash of the game's seed and the plot (`buildingPick`: `plotSeed` of the seed xor "FRMB", mixed): the same
  on every load of a save, different between plots (D194: a plausible variant is enough; stock's `rand() % rows` differs
  per session). No row at the level or below: row -1, rowLevel 0xFF.
- **state** (the plot's `FarmState`) and **pillaged** (state 3; the record keeps its place, row and facing).
- **pos** (the decals' sample frame, which is the height field's) (**corrected 2026-11-23 by ticket 0657**, `buildings.cpp`
  `stockPlacement`; replaced: "the plot centre moved to the nearest point of the outlines of the server's own fields of
  that plot ..., clamped into the world rectangle", the reading 2.6.2 found wrong): **the stock placer's rule** on the server's
  own fields of that plot: among them the fields with |F| >= 100 (F = 0.5 x sum over the closed corner loop of x_prev x
  (y - y_prev), in L, which is the EXE's world frame shifted when sx = 1 and sy = 0.866), the one whose area-weighted
  centroid is strictly nearest the plot centre gives the position: that centroid (double; the EXE computes in float); the first
  in the layout's order wins a tie, so a Crops_Damage copy of the same outline never moves it. **field** = that field's index,
  **distance** = the move in L. No field of the plot takes part (art type 0, a plot no face is centred on, or only small
  fields): the **plot centre**, field -1, distance 0, and the building is still made (2.6.1 (2)). The position is **not**
  clamped (the EXE clamps only for its direction field read); the facing and the height are read at the clamped point.
  On the unit test's 16 x 12 farmland 63 of 64 records sit at a field's centroid and 355 fields are below the threshold [L
  offline, 2026-11-23]. The EXE's own building is put at this same point through the farm guard (ticket 0657, 16.6 and
  `cities-and-landmarks.md` 2.6.3), so a part B that draws from this record and the EXE's instance agree.
- **facing** = `fieldAngle` (the direction field of section 7 as ported in 13.1, at pos in L) + **quarter** x pi / 2,
  quarter a second seeded hash 0..3 (`buildingQuarter`; stock: `rand() & 3`), wrapped into [0, 2 pi): radians about +z,
  counter-clockwise from +x in L (= world x, y up to the field's sx = 1, sy = 0.866) [I: the model's own forward axis is
  part B's to check against stock].
- **z, hasHeight**: the producer has **no height field of its own**, so the height comes through **a hook part B fills**:
  `FarmFieldsProducer::setHeightField(shared_ptr<const HeightGrid>)`; with it z = `heightAtSample(grid, floor(pos clamped))` (clamped since 0657)
  (HeightAtGrid 0x6D8B60's truncating rule, as the units' `heightAtWorld`) [I: that the EXE's 0x530A10 samples the same
  way]; without it hasHeight is false and the drawer samples the height itself.
- Cost [I, not measured on the huge save]: one pass over the fields and a nearest-point scan of each farm's own fields
  (about 13 fields of 4-7 edges): well under a millisecond for 373 farms, on the render thread inside the `refresh()`
  that puts a production in force.

### 16.4 The contract for part B (ticket 0654) [C, `srv_farmfields.h`]

- **The list**: `std::shared_ptr<const srvrender::FarmBuildingList> FarmFieldsProducer::buildings()`, never null; a list
  is never changed after it is handed out (a change makes a new one), so the scene may keep the pointer across frames
  and compare `generation`. Fields: `generation`, `loadSeq` (the load it belongs to), `fieldsVersion` (the producer
  version of the fields it was placed on), `buildings` (16.3) and the counts `pillaged`, `withoutField`, `withoutRow`,
  `withHeight`.
- **The generation** moves on with every change of any record and with nothing else: a farm built, pillaged, repaired,
  removed or changing owner (a plot-table change), an owner's era change (`Batch::players`, `SrvFeed::CopyPlayers` at each
  table read, or `setOwnerEra`), a new row table (`setFarmRows`), a new or cleared height field (`setHeightField`). The
  model scene redraws only when told (the Architect's point), so part B re-reads the list whenever the generation differs
  from the one it drew. A production that leaves every record equal moves nothing.
- **The step**: the records are placed in the `refresh()` that puts a production in force, so a farm's record changes in
  the same render-loop step as its fields (tests `buildings_threaded`: never before them; `buildings_follow_the_fields`:
  in that call).
- **A new load** (`setWorld` with a new loadSeq) hands out an **empty list with its own generation** at once and clears
  the owners' eras (read again with the load's first table); only that load's productions fill it.
- **What a record carries for a draw with no tapped record to copy**: plot, owner, era level, row (the library's row id:
  `rowFile(row)` -> file, `farmBuildingModel(file, state)` -> model and parts), state and pillaged, pos, z (with the
  hook), facing. Not in it: the row's `Scale` (1 for all six), the frame transform (world x = pos x x sx - ox, world y =
  pos y x sy - oy: the height field's `sampleToWorld` rule), the fog (the layer's), the wrap copies.
- **The rows**: part B hands the library's table to the producer once its library is loaded: `ModelLibrary::farmRows`
  then `FarmFieldsProducer::setFarmRows`; until then every record has row -1. A record with row -1 is drawn as nothing and
  counted (D118).
- **The bundle player** restores the producer's input from `render_farmfields_state` (13.3), which carries no owners:
  a restored producer gives every record era level 1 (the Medieval rows) with the same positions and picks [C, test
  `buildings_restored_span`]. A bundle that should replay farm buildings as recorded needs the owners in the span text
  (a new optional field) or accepts level 1; part B decides with rule A.
- **Not in part A**: any drawing, the wiring in `srv_render_scene.cpp` (0652's until its merge), a default, the
  pillaged smoke, and the LandmarkShader ground decal and tree-cut stencil mesh of each farm model (the library does not
  pack them; stock draws them [I: not looked at separately in the pictures]): differences part B names.

### 16.5 Tests [L offline, workstation, 2026-11-21]

- `render/tests/farmfields/buildings_test.cpp` (9 cases): era levels; the nearest point on a square; on a 16 x 12 map
  with 64 farms of the real layout: every record on an outline of its own plot's fields (worst 0.000033 L, tolerance
  0.01 L) and no sampled outline point (every 0.02 L) nearer to the plot centre by more than 0.05 L (worst 0.000029 L),
  facing = field + quarter turn; era level and row with the fallback (no owner, Ancient, Renaissance, Industrial,
  Modern, unknown era; rows only at level 4, only at level 0, none); stable for a seed (two productions equal) and
  different between plots (all 3 rows, all 4 quarters, 64 distinct picks); pillaged (only that record changes), removed
  (the others equal but for their indices), without a field (the plot centre, field -1, still a row); the height hook;
  four refusals with an empty output; an empty input.
- `render/tests/farmfields/farmrows_test.cpp` (3 cases; the install and database through the landmark tests' fixture
  `render/tests/landmarks/landmarks_testkit.h`): an unloaded library refuses; the install's FARM rows (16.2) with each
  file's three state models; a row's model made resident.
- `server/tests/render/farmfields/test_farmfields_producer.cpp`, 4 new cases: `buildings_follow_the_fields` (one record
  per farm, equal to a direct `placeBuildings`; pillaged, repaired, removed, built again, era, owner: each one new
  generation in the same refresh as the fields; a non-farm change none; a new load's empty list),
  `buildings_threaded`, `buildings_rows_and_height` (two refused rows two LOUD lines, the same table again none, no
  table one LOUD line and every row -1, the batch's player rows, the height hook on and off), `buildings_restored_span`.
- Runs (2026-11-21, `C:/c5-64bit-work/0653/sh/`, scratch): `render/build.py --test -R "farmfields|landmarks"` 13/13
  plain (18:24-18:30) and 13/13 under ASan (0 reports, the canary reported, peak 565 MB; 18:31-18:58); the producer suite
  16/16 cases plain and under ASan (pytest 3 passed both, no AddressSanitizer report). The farm decals are unchanged:
  `bundle_playback_equals_recorded` gives the same 1,594 records and hash 8785656002009394310 as 4852 recorded on
  2026-11-18. Every builder of the changed shared sources linked on the merged head (11 of 11, 18:59-19:18, the
  render units of server/src/render and c5render): `server/build_server.py --config release` with the 0.1b pins of
  `C:/c5-64bit-work/keep/pins-0.1b` (128 s), `build_render_tests.py`, forest, fow, overlay scene, scenebundle, shadow
  scene, units, sceneshot, unitfeed, units extras; plus the producer builder and `render/build.py` (plain and ASan).
  Main merged afterwards (19:18, `origin/main` 0df7fb610) changed no file under render/, server/, shell/, options/ or
  models/.
- The scene library replayed (`server/tests/render/run_all.py --only offline-equals-live`, plain, 19:18-19:26, EXE
  4390913, the F: library's 16 bundles, every one with `render_farmfields` 0): 3 bundles passed (HUGE_0350,
  C1MOUNTAIN, C2ANATURAL) and the negative case; **13 were refused by the player's install guard of ticket 0614**
  (`FAILED 9 Install`: the bundles name the installs of c5h-a and c5h-c, which ticket 0615's agents held at the time),
  12 before any frame and C2BWONDER after its 8 frames had compared EQUAL. Of the 8 frames compared, 8 were equal in
  colour and depth; none differed. A full replay needs free instances or `sceneshot --asset-root`; part B's rule-A replay
  covers it. Nothing in this part reaches a draw (the records have no reader yet) [C].
