# Community report: Lake Victoria's thick river (ticket 0556)

Community report, unconfirmed (the owner, 2026-11-18, decision D187 [reworded for privacy]: to be investigated with caution, as unconfirmed). Report:
Lake Victoria's thick river does not handle standard river tributaries flowing into it (it turns into a normal
river), and it also turns into a normal river when it is too short.

Timebox: about 1 h of reproduction on Goldberg instances, stock route first, then the product route of the current
pins (batch 4). Work 2026-11-18 from 15:38 EST, workstation, pool instance `c5h-c` (profile mods548, mods cp, vp, eui).

## 1. What the data says (before any run)

- **Lake Victoria in the game data [C, the instance's `Civ5DebugDatabase.db`, VP 5.4.8].** `FEATURE_LAKE_VICTORIA`
  (ID 22) is a natural-wonder feature on a land plot: `NaturalWonder` 1, `Impassable` 1, `NoCity` 1,
  `AddsFreshWater` 1, `ArtDefineTag` `ART_DEF_FEATURE_LAKE_VICTORIA`. Its landmark row: `Feature_Lake_Victoria.fxsxml`,
  layout `SNAPSHOT`, `TerrainContour` 1, era and state `Any`. Nothing in the data gives it a river: the placement row
  (`Natural_Wonder_Placement`, `AvoidFreshWater` 1, `OccurrenceFrequency` 20) and VP's changes
  (`CoreFeatureChanges.sql`, `NaturalWonderChanges.sql`, `FeatureSweeps.sql`) are placement rules and yields.
- **No river code for it in the DLL [C, the DLL source at the 32-bit pin `i0-ship-batch4` e96457fd5].**
  `FEATURE_LAKE_VICTORIA` appears only in the enum (`CvEnums.h`, `CvLuaEnums.cpp`); no Lua map script of the
  install or the mods names it. Rivers are ordinary plot edges (`setWOfRiver` / `setNWOfRiver` / `setNEOfRiver`
  with a flow direction) in both the DLL and the map scripts.
- **So the "thick river" is drawn by the EXE** from the ordinary river edges next to the wonder: the river mesh
  is built once per load (`0x6D51D0` -> `0x560B40` -> generator `0x738D10`, `research/05-graphics-composition/
  water-system.md` 9.2) and the rivers never change in play (same file, section 3). On the product route the
  EXE still builds that mesh; which side draws it is in section 4.

## 2. Set-up

- Instance `c5h-c` (C:), the Steam DX11 EXE of the instance, Goldberg wrapper route, frame cap on, dialog watcher on
  (`hold`). **Stock route:** the 32-bit pin `i0ship-e96457fd-ia` (DLL `3EAA3290...`, batch 4) put in with
  `provision.py set-dll`, no proxy layout. Mods cp, vp, eui.
- Save: a copy `C5H-0556-TINY_0008` of `REL548-TINY_0008` (SHA-256 `b2ceb663...`, Polynesia, Tiny 56 x 36, turn 9).
- Scene (`tools/harness/evidence/0556/scene.lua`, through the Lua channel): an ocean rectangle x 20..35, y 19..30
  turned into flat grassland (`SetPlotType(PLOT_LAND)`, `SetTerrainType(TERRAIN_GRASS)`), revealed and visible; then
  per site Lake Victoria (`SetFeatureType`) and river edges laid out with `SetWOfRiver` / `SetNEOfRiver` /
  `SetNWOfRiver`: the main river leaves the wonder's plot L at its lower-right vertex and zigzags down-right (L's east
  neighbour's south-west edge flowing SE, then for A = SE(L), SE(A), ...: A's east edge flowing S, A's east
  neighbour's south-west edge flowing SE). A tributary of 2 edges joins at A's lower-right vertex along A's south-east
  edge (flow NE), coming up SW(A)'s east edge (flow N). Each layout was saved (`UI.SaveGame`) and the save loaded in a
  new process, because the EXE builds the river mesh only at a load. The river edges of each layout as the DLL held
  them: `tools/harness/evidence/0556/layouts/`.
- Pictures: harness window shots (PrintWindow, scale 0.5), Visual Checks issue 0556, all DIAGNOSTIC. D114: no popup
  over the map at any shot.

## 3. Runs

### 3.1 Stock route, layout 1 (2026-11-18 15:56-16:02 EST) [L]

| Site (wonder plot) | Case | Picture |
|---|---|---|
| 20,29 | river away from the wonder, 8 edges | normal river; the far (downstream, inland) end widens into a dark mouth |
| 24,29 | 8 edges away, 1 tributary at junction 2 | normal river, tributary joins normally, dark mouth at the far end |
| 28,29 | 8 edges away, tributaries at junctions 1 and 3 | normal river, dark mouth at the far end |
| 32,29 | 2 edges away | **thick river**: a wide channel in the lake's water colour from the wonder, blunt end |
| (20,23) | no wonder, 8 edges, reaches the coast | normal river into the sea |
| 24,23 | 8 edges flowing toward the wonder | normal river into the lake |
| 28,23 | 4 edges away | **thick river** (as 32,29, longer) |
| 32,23 | 1 edge away | no thick river: a short dark stub |

So the thick river exists in stock and is the EXE's: it was drawn for 2 and 4 edges flowing away from the wonder, not
for 1 edge, not for 8 edges with or without tributaries, and not for a river flowing into the wonder.

### 3.2 Stock route, layout 2 (2026-11-18 16:02-16:10 EST) [L]

Layout 1's rivers and wonders removed (`scene.lua` mode `clear`), 12 new sites, saved as `C5H-0556-SITES2`, loaded in a
new process. Pictures `s2/*.png` in Visual Checks.

| Site (wonder plot) | Case | Picture |
|---|---|---|
| 20,29 | 5 edges away | normal, dark mouth at the far end |
| 24,29 | 6 edges away | normal, dark mouth at the far end |
| 28,29 | 7 edges away (its last edge ends one plot west of the wonder at 32,25) | **thick river** over its whole length, running into the lake at 32,25 |
| 32,29 | 3 edges away | **thick river** |
| (20,25) | no wonder, 4 edges | normal, dark mouth |
| (24,25) | no wonder, 2 edges | normal, dark mouth |
| 28,25 | 4 edges away + tributary at junction 1 | normal river with a normal tributary, dark mouth |
| 32,25 | 3 edges toward the wonder | normal |
| 20,22 | 4 edges away (ends at the coast) | normal |
| 24,22 | 4 edges away + tributary at the end vertex | normal, both dark mouths |
| 28,22 | 2 edges away + tributary at the end vertex | normal |
| 32,22 | 4 edges toward the wonder | normal |

**What the thick river is [L, by eye at 3 wheel ticks in, `s2/A-right-zoom.png`].** It is not a wider river mesh: it
is a channel along the river's course in which the lake's own water surface shows (the same colour, waves and
reflections as the wonder's lake), with sloped banks, about a third of a plot wide. A normal river is the thin river
mesh over unlowered ground. So the thick river is terrain carved below the water height along the river, the same
way the wonder's own lake is a basin below the water height [I: the mechanism is section 5].

**Reproduced [L]:** the report's both halves are seen in stock. A river flowing away from Lake Victoria can be drawn
as the thick channel (2, 3, 4 and 7 edges in these layouts); with a tributary joining it (at junction 1 of 4 edges,
at the end vertex of 2 and 4 edges, or anywhere on 8 edges) it is a normal river; and with 1 edge it is a normal stub.
But length alone does not decide it: 5, 6 and 8 edges without a tributary were normal, and 4 edges were thick at
28,23 (layout 1) and normal at 20,22 (layout 2, a river that reaches the coast).

### 3.3 Stock route, layout 3 (2026-11-18 16:17-16:22 EST) [L]

Ten sites, saved as `C5H-0556-SITES3` (`s3/*.png`). Band A (wonders at y 29) and band B (y 26) all 4 edges away;
the band A rivers end at a vertex of a band B wonder (except 32,29), so they also flow **into** a wonder.

| Site | Case | Picture |
|---|---|---|
| 20,29 / 24,29 / 28,29 | 4 edges away, ending at a band B wonder's vertex | normal |
| 32,29 | 4 edges away, ending inland | normal (the same plot was thick with 2 and 3 edges in layouts 1 and 2) |
| 22,26 / 26,26 | 4 edges away, ending inland | **thick** |
| 30,26 | 4 edges away, ending at the wonder 31,23 | **thick**, wonder to wonder |
| (20,23) | ordinary water plot (joined the sea: `IsLake` false), 4 edges away | normal, dark mouth |
| (26,23) | ordinary one-plot lake, 4 edges away | no thick channel seen (the river is at the frame's edge) |
| 31,23 | 6 edges away, tributary at junction 2 (the wonder's branch is 4 edges) | normal everywhere |

Tally over the three layouts, rivers of 2 or more edges flowing away from the wonder without a tributary: 7 of 15
thick, with no rule by length (2-7 thick, 4-8 normal), plot (32,29 thick twice, normal once) or what the river ends
at. Every case with a tributary (6), the 1-edge case (1) and every river flowing into the wonder (3) was normal.

## 4. Product route, and which side draws what

### 4.1 Product route, layout 2 (2026-11-18 16:07-16:12 EST) [L]

The proxy layout of the batch-4 pins `x64-c29442e8-p58724e0` put on c5h-c with `provision.py set-proxy` (both logs'
build lines `repo 58724e072 dirty=0 dll_pin c29442e8c`, config release), `vpproxy.ini` `mode=on` as `provision.py`
writes it, no `VP64Options.ini` (every option at its default: `ServerRenderer` 0, the EXE draws the world), 32-bit
fallback the pin `i0ship-e96457fd-ia`. Same save `C5H-0556-SITES2`, same six cameras (`p2/*.png`).

Every site looks as on the stock route: the same two thick channels (28,29 with 7 edges and 32,29 with 3 edges; none
elsewhere) and the same normal rivers. Against the stock pictures of 3.2 at the same cameras: median difference 0, 95th percentile 3-15,
0.4-4.6 % of pixels above 16 (water animation, cloud and fog movement; `cmp.py`, not committed) [L]. So the product
route neither causes nor removes it.

### 4.2 Which side draws what

- **Default product route (`ServerRenderer` 0, the shipped thin client of increment 1) [C config, L picture].** The
  server runs the DLL and the game state; the EXE builds the height field and the river mesh at the load and draws the
  terrain, the lake water and the rivers itself, as in stock. The river edges the EXE reads come from the DLL in the
  server through the proxy (the same edges, section 2's dump).
- **The server's world renderer (`ServerRenderer` 1, increment 2's A'') [C design, I behaviour, not run].** The EXE
  still runs CreateTerrain at the load (D33 keeps the terrain bake: `water-system.md` 9.2, the river mesh is "built
  anyway"); the server draws the terrain from the **tapped** EXE height field (`render/src/heightfield/heightfield.cpp`,
  "the tapped height field") and the rivers from the **tapped** EXE river mesh (`render/src/water/river.cpp`, "over the
  tapped river mesh"), and its own water plane at the EXE's water height. Both inputs are the EXE's own load-time
  results, so the thick channel and the normal-river cases should be the same there [I]: confirming it needs the
  joined arm (composite, D33 keys, `VP64Options.ini ServerRenderer = 1`), which this layout does not ship and which
  was not set up in the timebox.
- So the behaviour belongs to the EXE's load-time terrain bake on every route; a fix in the server would have to change
  tapped data (section 6).

## 5. The mechanism in the EXE (static reading, 2026-11-18 16:15-16:35 EST)

Read-only headless Ghidra 12.1.4 on a scratch copy of the `civ5` project (`tools/harness/evidence/0556/gh.sh`, 0338's
`Dec.java`, registered through `quietlock.py run`); the decompiler text stayed in `C:/c5-64bit-work/0556/out/` (D83:
not committed). Behaviour and addresses only.

**The river carve takes its depth from a running minimum of the ground along the river [C].**

- CreateTerrain's river step (0x709B50 -> 0x6D51D0, `water-system.md` 9.2) first traces the rivers from the plot
  edges (0x5773C0, below), smooths and resamples them (0x6ECAD0, 0x484BD0), appends a mouth or a junction to each
  river's end (0x640090; 0x5568A0 sets the river record's byte +0x0C when the last edge has no continuation), then
  0x4954C0 samples the bake's height field at every centre-line point of each river and stores, per point, **the
  minimum of the heights from the river's first point up to that point** (the running value starts at a negative
  sentinel and only goes down). 0x636F70 and the mesh generator 0x738D10 use the same records; both skip a river
  of 3 points or fewer (`3 < count`).
- The carve 0x412680 (called by CreateTerrain after the river mesh, `asset-pipeline.md` 4.1 step 7) runs 0x4954C0
  again on the same rivers and builds two triangle lists with 0x5F3F50, whose vertices carry those per-point depths
  (same `3 < count` skip). One list goes through 0x4A7DD0 -> 0x7A1FD0 (blend toward a target height, flags the
  sample), the other through 0x5FC1C0 -> 0x46AEF0: per height sample inside a triangle, the new height is a lerp from
  the old one toward the interpolated depth minus a cross-section profile (a sine of the across-river coordinate),
  never above the old height, and never below `[ECX] - [0x9E9958]`. On the 0x412680 path `[ECX]` is the constant
  `[0x9EEDBC]` = 0.23534 (60/255) and `[0x9E9958]` = 0.06 [C, values read 16:24], so the bound is **0.1753 in
  normalised height, a fixed floor**, not "the old height - 0.06" as `asset-pipeline.md` 4.1 step 7 has it (this file
  is current on that point; the one-line correction there is outside this ticket's files and was handed to the MC
  with 0556's hand-back, 2026-11-18). That 60/255 is the water height is [I] (the water height is
  `TerrainSystem+0x277F4 * 0.2 * 255` at run time, `asset-pipeline.md` 4.1 step 2; not read). Read that way, a river
  bed is carved toward its running minimum but never more than 0.06 below the water: a river whose minimum is above
  the water keeps a bed above it, a river whose minimum is the basin's is dug to just under the water.
- So a river whose first centre-line points lie inside Lake Victoria's basin (the wonder's terrain piece
  `Wonder_Lake_Victoria`, flag 0x1000 of `asset-pipeline.md` 4.2, is a basin below the water height; that is the
  lake the wonder shows) carries the basin's depth along its whole length, and the carve digs the ground down to it:
  a channel below the water height, in which the water plane shows. That is the thick river. A river whose first
  points lie on normal ground keeps a normal depth and is carved only into a shallow bed under the river mesh.
- The river mesh itself (0x738D10) is not wider for the thick case; its width and its texture coordinates come from
  the bank polylines. The look is the terrain's.

**How a river is traced, and so which end is its first point [C].** 0x5773C0 scans the plot grid; at each river
edge not yet taken it walks back with 0x43FBE0 (step 0x4CE7E0) to the edge where the river starts, then forward with
0x4479E0 until there is no next edge or the next one is already taken. Both steps choose between the two possible
neighbouring half-edges with 0x5B5030, which tests a per-hex table of **directed** river half-edges (bits by
direction, `+0x9C` / `+0xCC` of the river object; 12 directions, `% 12`): the first candidate wins when it is set.
So the trace follows the river's flow as the EXE holds it [I: that the directed bits are filled from the DLL's flow
directions was not read, but the "away" and "toward" rivers behave as two different cases, section 3], and:
- a river that flows out of Lake Victoria starts at the wonder: its running minimum begins in the basin;
- a river that flows into it ends there: the basin is only its last point, the minimum reaches it at the end, and the
  river is normal (section 3: every "toward" case);
- at a confluence the walk back takes the first candidate branch; the other branch becomes a separate river that ends
  at the junction (the forward walk stops at an edge already taken, 0x640090 then appends the junction points with
  0x5369A0 instead of a mouth). Which branch is "first" is a fixed geometric order of the two half-edges, not the
  longer or the wonder's branch. In every tributary case here (all joining from the south-west side) the result was a
  normal river below the junction, so the walk back took the tributary: the river below the junction starts at the
  tributary's source on normal ground, and the wonder's part is its own short river ending at the junction [I for
  the branch order, L for the picture]. A wonder branch of 3 points or fewer is skipped altogether (no mesh, no
  carve). A tributary joining from the other side may lose the walk back to the wonder's branch, which would keep the
  channel below the junction [I, not run];
- a 1-edge river from the wonder is too short in the same way (`3 < count`); with its dead-end mouth points added
  (0x640090) a 2-edge river already passes the test (section 3: 2 edges thick).

**Why the same case is thick at one wonder and normal at another: random jitter of the river points [C for the
jitter, I that it decides].** Between the trace and the height sampling, 0x6D51D0 calls 0x54BAB0, which moves the
river centre-line points with the C runtime's `rand()`: every point (the first one included) has a 1-in-2 chance of
an offset of `rand() % 15 - [0x9EC8F4]` in x and in y, and the inner points get a second pass with `rand() % 10 -
[0xA0B0B4]`; 0x631990 then gives the banks random widths (`rand() % 5`, `rand() % 3`) away from the river's start,
fixed widths for the first points and a growing (quadratic) flare over the last ones when the river has no
continuation (the mouth; byte +0x0C of the record, set by 0x5568A0). `rand()` is seeded once per load with the map's
random seed at the start of CreateTerrain (`asset-pipeline.md` 4.1 step 1) and consumed by the coast jitter and every
river before this one, so the offset of a river's first point depends on the whole map, not on the river. The exit
vertex of the wonder sits at the basin's shore: whether the jittered first point (and the next few) falls inside the
basin decides whether the running minimum starts below the water height. That explains a coin-toss pattern
(7 of 15, section 3.3) that changes with every layout [I: the sampled heights were not dumped; a live read of the
river records' first points and of 0x4954C0's minima for a thick and a normal river would make it [C]].
The same jitter lies under every river of the map; only a river that leaves a basin below the water height (Lake
Victoria; by the code also an ordinary lake or the sea, which layout 3's two controls did not show) can turn into a
channel this way.

## 6. Result and fix proposal

### 6.1 Result

**Reproduced, in stock and on the product route alike [L]; the mechanism is the EXE's load-time river carve [C], the
variation between cases a random jitter [C for the code, I that it decides].**

- The "thick river" is a river channel carved to just under the water height, so that the water plane shows in it.
  It happens when a river's carve depth, the running minimum of the ground along the river from its first point
  (0x4954C0), starts inside Lake Victoria's basin, that is when the river flows **out of** the wonder and its jittered
  first points fall inside the basin. About half of such rivers (7 of 15 here, 2 to 7 edges).
- **Join condition:** a tributary joining the river makes the river below the junction start at the tributary's
  source (the trace's walk back takes the tributary), so below the junction it is a normal river; the wonder's branch
  above the junction becomes a separate short river, skipped when it has 3 points or fewer. Seen in all 6 tributary
  cases (junctions 1 to 3, tributaries from the south-west side).
- **Length condition:** a river of 3 centre-line points or fewer gets no mesh and no carve (`3 < count` in 0x636F70,
  0x5F3F50 and 0x738D10): 1 edge from the wonder is too short; 2 edges, with its mouth points added, is enough.
  There is no upper length limit: 7 edges were thick; the 5, 6 and 8 edge normals are the coin toss above.
- A river flowing **into** the wonder is always normal (its minimum reaches the basin only at its end).
- No crash, no box, no assert in any run (5 loads, stock and product). Nothing for COMMON 30.

### 6.2 Fix proposal for the MC (no product code in this ticket)

The behaviour is stock (the EXE's own bake, Firaxis's), cosmetic, and the same on every route of the patch. Options:

1. **No fix (recommended).** It is not a crash or a gameplay effect, the shipped routes draw what stock draws, and
   the pixel-faithful goal (D66) asks for exactly this picture. Close as reproduced, stock behaviour, with this file as
   the record.
2. **Later, only if the server ever bakes its own heights** (today it draws the tapped EXE height field and river
   mesh, section 4.2): an increment 4 Extras switch (D169/D170 category "bugfix", what the system tries to do is wrong)
   that makes the carve deterministic for rivers leaving a lake or a wonder basin: either start the running minimum at
   the first point outside the basin (never a channel) or seed it with the water height for every river leaving a
   lake (always a channel, the look the report seems to expect from "Lake Victoria's thick river"). Which one is a
   product decision for the owner; neither is possible on the EXE's own bake under the EXE rules (no code bytes, D4/D65;
   the jitter and the minimum are code, not data).
3. Not proposed: patching the jitter through data (the seed is the map's random seed and feeds the whole bake;
   changing it would change every river and coast of the map).

## 7. Evidence and restore

- Drivers and scene (committed): `tools/harness/evidence/0556/` (`v.py`, `wait_ingame.py`, `shots.py`, `scene.lua`,
  `mapdump.lua`, `riverdump.lua`, `popups.lua`, `gh.sh`; README there). River edges of each layout as the DLL held
  them: `tools/harness/evidence/0556/layouts/layout{1,2,3}.txt`.
- Pictures: Visual Checks, issue 0556, 23 shots (4 layout 1, 7 layout 2, 6 product, 6 layout 3), all DIAGNOSTIC (D112: window shots by PrintWindow at half scale
  on a test save with a terraformed studio; not the full shipped system's acceptance picture). Scratch copies in
  `C:/c5-64bit-work/0556/{s1,s2,p2,s3}/` (not committed).
- Saves (copies with our own names, not the owner's): `C5H-0556-TINY_0008` (of REL548-TINY_0008), `C5H-0556-SITES1..3`
  in the instance during the work; kept in `F:/c5-64bit-work/0556/saves/` and removed from the instance at the restore.
- Decompiler text: `C:/c5-64bit-work/0556/out/p1..p5.txt` (scratch, D83: not committed).
- Instance restore (2026-11-18 16:25-16:30 EST): `set-proxy --original` (16:13), `frontend.py restore`,
  `set-dll --previous` (back to `e589306e...`), the save copies removed after their copy to F:, `backup.py verify` of
  the standard set equal to the start backup, `provision.py verify c5h-c` ok (no errors, no changed mutable files),
  `leftovers` clean; claim released. No crash, no box, no assert in any of the 5 loads.
