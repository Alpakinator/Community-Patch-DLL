# Baked terrain tiles in the server: residency, bake-ahead, the radius option and invalidation (issue 3407)

Written 2026-11-13 by team-3400 W7 (epic 3400 scope 5, acceptance C and D). Marks: `[C]` from the DX11 EXE's listing
(`D:/c5-64bit-work/2900/2903/dx11.asm`, objdump of the 2903 copy, read-only) or constants read from the EXE file, `[L]`
read live from a test instance or measured with our own binaries, `[I]` inferred. Addresses are VAs at ImageBase
0x400000. D83: behaviour is described in words; no decompiler output is committed. Code: `shell/server/residency.h`,
`residency.cpp`, `standin_baker.h`, `standin_baker.cpp`, `tilegen_block.h`; tests `shell/tests/test_residency.py`; tools
`shell/tools/tilegen_feed.py`, `shell/tools/residency_drive.py`. Scratch (not committed): `D:/c5-64bit-work/3400/w7/`.

## 1. Answers

- **Which tiles are baked** [C + L offline]: the EXE's height class (tile+0x328, written by 0x7212B0 inside the
  per-tile job): the highest height sample of a window around the tile, in u16/255 units (no sz factor), compared with
  two constants read from the EXE file: below **2.0** (0x9DBBBC) class 1, below **30.0** (0x9F9134) class 2, else class 3.
  The window starts 4 samples after the tile's origin (+0xDC/+0xE0 + 4) and runs to origin + extent (+0xD4/+0xD8) + 3,
  both axes, modulo the sample counts held at TR8+0x1AA8/+0x1AAC. Offline check on 3401's huge reference probe
  (armA, `layers.json` + `heightfield.r16`): the window [+4, +67) reproduces +0x324 (the highest point) on 10,138 of
  10,752 tiles exactly, the tile's own 64 samples on 8,291; the rest differ by up to 10.3 units [I: the exact edge rule
  of the loop, not needed here]. Class ranges seen in that probe: class 1 0.0-1.80, class 2 2.08-29.98, class 3
  30.19-244.75 (4,729 tiles). The bake-ahead bakes class 2 and 3 (3401 [L]: 159 class-2 tiles cached); the in-play cull
  lists class 3 only. **The server bakes classes 2 and 3** ("land" in the code), with a wider window (the tile plus 4
  samples on every side), which can only add tiles: on the huge field 4,922 tiles (150 class 2 + 4,772 class 3) against
  the EXE's 4,888 (4.1).
- **What raises tile+0x1CC** [C]: section 3. The value is the global frame counter ([0x3010570]+0x33F74) at the
  moment of the change; +0x1D0 is the same counter at the last bake. A tile is stale when +0x1CC > +0x1D0.
- **Stock stale tiles** (3023's [I]): a road built by Lua raised +0x1CC on 16 tiles within 0.5 s, and stock left them
  stale (state 3, not re-baked) for 70.8 s while the camera stayed; it re-baked them when the camera left and came back
  [L, 3.3]. Not "until a restart": until the view moves away and back.
- **Residency in the server**: section 2; measured figures in section 4. Acceptance C's measure on the huge field with a
  recorded camera path at radius -1: 0 gray frames after a 12.2 s bake-ahead of 4,922 tiles (4.1). Acceptance D: the
  server re-baked the road's 16 tiles 0.55 s after the road (4.2). Both with the stand-in baker.

## 2. The server's residency (shell_server.exe --scene terrain --bake standin)

`shellres::Residency` owns every tile's t0/t8 (`c5r::bake::BakedTile` from a `c5r::bake::TileBaker`) and hands views to
`c5r::terrain::Renderer::setTileCache`. Until 3404's baker lands it runs over `shellbake::StandinBaker` (one flat BC1
colour per tile, the contract's sizes: 256 x 256, 6 mips, 43,680 B per cache, t0 and t8 on every tile = 87,360 B).

| Rule | Value | Where |
|---|---|---|
| Tile | the EXE's: one 64-sample plot (`--tile-plots 1`, forced; 128 x 84 = 10,752 on huge) | `WantTilePlots` |
| Land | class 2 or 3 (above) | `Residency::setField` |
| Order | visible land tiles first, nearest first (clip w of the tile centre), baked **before the draw**, up to `--bake-burst` (16, the EXE's settled budget) per frame (corrected 2026-11-16, ticket 0409: all of them, and the view's edge, when more than the burst have no bake at all, `terrain-bake-residency-first-draw.md`; before: always up to the burst); then breadth-first rings (Chebyshev, 8 neighbours, x wraps) from the visible tiles, or from the map centre before the first camera | `bakeVisible`, `computeRings`, `pump` |
| Background budget | token bucket, `--bake-rate` tiles per second (default 400), at most `--bake-burst` per loop iteration and `--bake-pump-ms` (4 ms) of CPU per call; run after the publish, so its GPU work never delays a published frame | `pump` |
| GPU budget (optional) | `--bake-gpu-ms-per-s F`: a second token bucket in ms of the baker's `BakedTile::gpuMs`, at most a quarter second's allowance saved up; 0 (default) = off. For the real baker, whose GPU cost (~1.2 ms per tile in the EXE, 3023) makes a tile count alone a poor budget; the visible pass before a draw is not limited by it | `pump` |
| Radius | `[Renderer] BakedTileCameraRadius` from VP64Options.ini through `options/` (`Vp64Opt::LoadFile`, path from `--options-file`, else `VP64_OPTIONS`, else Documents); `--bake-radius` overrides; -1 keeps all land; N keeps and bakes only tiles within N rings of the visible tiles and evicts the rest at once | `ResolveRadius`, `evictOutside` |
| Visible set | own frustum test per tile and wrap copy: the tile's box (one sample of margin, its height range +-1) against the clip volume, 8 x 8-tile blocks first | `setView` |
| Plain shader | only for a land tile without any bake (the renderer's fallback colour) | renderer |
| Stale | a tile with a bake older than its generation draws the old bake until re-baked (visible ones with the visible priority) | `needsBake` |
| Out of memory | the baker's Device error with 0x8007000E: radius -1 or > 5 drops to 5, else by one ring; tiles outside are evicted; at radius 0 baking stops (logged), unbaked tiles stay plain | `onOutOfMemory` |
| Failed tile | any other bake error: logged (first 8), counted, plain, not retried until its generation rises | `bakeOne` |
| Counter (acceptance C) | a drawn frame with at least one visible land tile without a bake = a **gray frame**; also gray tile-frames, gray frames after the bake-ahead finished, stale frames | `countFrame`, log `residency 1s:` and `residency totals:` |

Invalidation: every tile carries a content generation (1 at load). Sources:

1. **A new field of the same size** (a new tap copy or load): per tile an FNV hash of the heights and material bytes
   of the tile plus 3 samples (the list builder reads 2, the normal grid 3); a changed hash raises the generation. The
   renderer's caches are dropped by `setHeightField` and set again from the residency (`reapply`).
2. **The tile generation block** (`shell/server/tilegen_block.h`, `--tilegen NAME`): one u32 per tile in the EXE's order
   (x * tilesY + y), seqlocked; the first snapshot is the baseline, every later change raises that tile. Producer today:
   `shell/tools/tilegen_feed.py`, which copies the EXE's tile+0x1CC by PID (read-only). The product's producer is the
   DLL's plot-change message over the proxy route (epic 3300): the block's layout is the interface either way.

Why both: the stage-1 inputs (heights, material bytes, layer tables, style, bake-time constants) do not change when a
road or an improvement is built [I: the tap's samples are the height field; roads are decals of bake stage 2], so a
re-tap would not see a road. The EXE itself marks road and improvement changes through +0x1CC (section 3), so the
block carries the EXE's own signal until the DLL sends one.

## 3. Writers of tile+0x1CC and +0x1D0 [C]

Found by a search of the listing for stores to `[reg + 0x1cc]` and kept where the base is a terrain tile (from the tile
array TR+0x1AE0 / count TR+0x2318, or a tile constructor):

| Writer | What it does |
|---|---|
| 0x4387D0 (store at 0x438901) | **Invalidate the tiles of a world rectangle**: converts the rectangle to tile x/y ranges (clamped to the grid), and for each tile: +0x1CC = the frame counter, the layer count +0x1F8 = 0, +0x300 = 1, and the tile's job is queued again (0x657CD0 with the job set at TR+0x1AD8: height range, class and layer list recomputed); then the job set is started and, when the fourth argument is set, TR+0x2348 = the frame counter. Callers: 0x49B925 in 0x49B5A0, whose only string is the source file name `.\App\Graphics\TerrainDecalSystem.cpp` (the terrain decal system: roads, railroads, farm fields; its callers include the decal and tree-cut asset loaders 0x65A150 / 0x69F250) - **the decal path, which the live road (3.3) exercises** [C for the file, I that this is the road's call]; 0x67915E in 0x678FD0, called from the map-script parser (0x4518D0, "Parse Map Scripts"), a Lua context (0x4CC210) and the DLL script system (0x60D180, `CvDLLScriptSystem.cpp`) [I: height or terrain edits from scripts]; 0x68F233 in 0x68F1B0 (walks 0x34-byte records with a float rectangle at +0x24 / +0x2C) and 0x6B5C65 in 0x6B5A50: no strings, not identified. All four reach the renderer through the pointer at 0xD8C584. *Corrected 2026-11-15, epic 0166 (terrain-decals.md 5.1) [C, L]: 0x67915E lies in **0x678FF0** (0x678FD0 is a two-line thunk that hands 0x678FF0 to a dispatcher; 0x678FF0 is called from 0x61ECE0); **0x68F1B0 is RemoveDecal** (it invalidates the removed decal record's own rectangle, third and fourth arguments 0) and 0x49B5A0 AddDecal invalidates the added record's clamped rectangle, so every decal add and remove passes 0x4387D0 with exactly the record's rectangle (P8; live: a road and a farm reached the records within 0.4 s); **0x6B5A50** (from 0x6A74D0) and 0x678FF0 are **terrain edits**: both walk a per-plot byte mask, edit the marked plots' terrain through 0x775130 (0x6B5A50 only where the height-field value lies between 13,000 and 18,000) and invalidate the mask's bounding rectangle with the third argument 1. Replaced: "0x67915E in 0x678FD0", "0x68F233 in 0x68F1B0 ... and 0x6B5C65 in 0x6B5A50: no strings, not identified"; the decal path's "[I] that this is the road's call" stands as [I] for 3.3's road (not traced there).* |
| 0x669660 (store 0x669680) | every tile: +0x1CC = the frame counter (whole map invalid); called from 0x479320 |
| around 0x4A8340 (store 0x4A836C) | sets TR+0xB14 from an argument, then every tile's +0x1CC = the frame counter |
| 0x679B3B, 0x6C49ED, 0x57A8D2 | tile set-up / reset (1, 0, 0 with +0x1D0 alike) |

+0x1D0 is stamped by the bake wrapper 0x627810 (3023). So "+0x1CC > +0x1D0" is exactly "changed since the last bake".

### 3.3 Stock: tiles that stay stale

- [C, 3023] The cull checks the content generation only in bake state 5; a tile baked in state 1 (bake-ahead) or 3
  (baked for view) is not re-baked when +0x1CC rises while it stays in view (3023 added "until it is evicted or the
  game reloads"; the live road below shows a third way out: leaving the view and coming back).
- [L, 3401's probes, re-read here] Right after loading the huge save (stock config, current pin, run A 03:37 and run
  B 03:48 EST): **11 and 12 of 3,083 cached tiles are stale**, all in state 3 (now and requested), e.g. tiles (105..106,
  28..29) with +0x1CC 60 against +0x1D0 57 and (109..111, 26..28) with 66-67 against 58-60: they were invalidated a few
  frames after their bake during the load and stay that way. Tiny (run A 03:42): 2 of 1,303 cached tiles stale, both in
  state 1 ((6, 7) and (6, 8): +0x1CC 370, +0x1D0 226-227).
- **[L, live road, 2026-11-13 06:31-06:39 EST]** Goldberg instance c5h-a, current pin `i0ship-bbfacca1-ia`
  (E589306E...), stock config (`[DisableSystems] Terrain = 0`, no view-mask marker), CP/VP/EUI, save copy
  `C5H-3407-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, SHA-256 c499d3c6...), pid 20684, default zoom. Driver
  `D:/c5-64bit-work/3400/w7/road_check.py` (scratch) with `shell/tools/tilegen_feed.py --period 0.25 --watch-out`:
  camera on plot (99, 24) next to Thebes (101, 25) through `UI.LookAt`, 15 s to settle, then
  `SetRouteType(ROUTE_ROAD, -1)` on plots (99, 24) and (99, 25) at 06:34:11.976 (both returned 0 = road).
  - **The road raises +0x1CC at once**: within the feed's first poll (0.49 s after the call), 16 tiles (x 99-101, y
    23-28, without (101, 25) and (101, 26)) went from +0x1CC 57-74 to **2581** (the frame counter), a rectangle around
    the two plots: the rectangle invalidation 0x4387D0 [L that it fires for a road; which of its four callers is the
    route path is still [I]].
  - **The tiles stayed stale while the camera stayed**: all 16 in state 3 (now and requested), cache handles unchanged
    (e.g. tile (99, 24) handle 8413), +0x1D0 still 70-74 for **70.8 s**, until the driver moved the camera away (40
    plots east, 10 s) and back. Then all 16 were re-baked within the next poll (+0x1D0 17,490-17,496, same handles, i.e.
    re-baked in place, not evicted) [L].
  - So 3023's [I] is **confirmed for a camera at rest and corrected for a moving one**: a changed tile in state 3 is not
    re-baked while it stays in view; it is re-baked when it re-enters the view after leaving it (here after 10 s away).
    It does not need a reload. That is not the owner's [reworded for privacy] report of a wrong picture that lasts until a restart, in its strict sense, but it is "wrong until the
    camera leaves and comes back", which a player who watches a worker build a road sees as the road missing [I that
    the stale cache lacks the road decal: the screenshots (`D:/c5-64bit-work/3400/w7/liveD/*.png`, not committed) are
    dominated by animated units and smoke, and a pixel diff did not isolate the road; not judged].
  - Whether a stale tile in state 1 (bake-ahead, outside the view) is re-baked when it first enters the view: not
    tested [I: likely the same path].

## 4. Measurements

### 4.1 Acceptance C offline: a recorded camera path on the huge field [L, own binaries; stand-in baker]

Setup (2026-11-13 06:24-06:28 EST, RTX 4080, shell build of this branch at 6e6646b, no game running; the quiet-machine
lock was free but other teams' jobs may have run, so no timing claim is made from these runs): `shell/tools/
residency_drive.py --tap-file D:/c5-64bit-work/3400/ref/C5H-3401-HUGE_0350/armA/heightfield.c5hf` (the huge save
`C5H-3401-HUGE_0350`'s field read by PID on the current pin, 3401) `--gt-dir D:/c5-64bit-work/i2-ground-truth/huge`
(the EXE's own camera of epic 3000, default zoom, translated in x/y) `--radius R`, server target 1280 x 720, 60 camera
blocks per second: 40 s still (bake-ahead), 4 pans of 3 s (0.4 tiles per frame), 24 jumps to class-3 tiles (seed 3407)
of 1 s each, 8 of them revisits. Defaults: rate 400/s, burst 16, pump cap 4 ms. Raw: `D:/c5-64bit-work/3400/w7/
driveC_r{-1,5,0}/` (server.log, summary.json).

| Radius | Land tiles (class 2+3) | Bake-ahead | Resident at the end | Frames drawn | Gray frames | Gray after the bake-ahead | Evicted | Video local (start -> end) | Server private (start -> end) |
|---|---|---|---|---|---|---|---|---|---|
| -1 | 4,922 (150 + 4,772) | **4,922 tiles in 12.2 s** | 4,922 = 410.1 MB | 4,402 | 5 (first frames) | **0** | 0 | 806.8 -> 1,434.4 MB | 1,101 -> 1,795 MB |
| 5 | 4,922 | 434 tiles in 1.1 s | 153 = 12.7 MB | 4,449 | 66 | 61 | 8,594 | 806.8 -> 873.1 MB | 1,096 -> 1,166 MB |
| 0 | 4,922 | 104 tiles in 0.3 s | 42 = 3.5 MB | 4,452 | 78 | 72 | 3,106 | 806.8 -> 833.9 MB | 1,100 -> 1,125 MB |
| 5, `--bake-burst 1000 --bake-pump-ms 50` (07:36, build of 7366dbb) | 4,922 | 434 tiles in 1.0 s | 153 = 12.7 MB | 4,439 | **0** | **0** | 8,837 | -> 882.6 MB | -> 1,191 MB |

- **Radius -1 meets acceptance C's measure on this path: zero gray frames after the bake-ahead** across the pans and
  the 24 jumps with revisits. The 5 gray frames are the first frames after start (the bake-ahead's first 16 visible
  tiles per frame, 4 ms cap). Radius 5 and 0 show gray frames after every jump into unbaked ground with the default
  burst, as the option's text in `docs/OPTIONS.md` says; with `--bake-burst` above the visible count and a 50 ms pump
  cap, radius 5 had **no gray frame at all** on the same path (last row: every jump's visible tiles, up to ~110, baked
  before its first draw). The price is the bake time inside those frames: with the stand-in 0.06-0.18 ms per tile, with
  the real baker about 1.2 ms of GPU per tile [I, 3023], i.e. up to ~130 ms in a jump's first frame: a frame-time
  question for the real baker (not measured here).
- **The residency's visible set equals the renderer's**: in 77 of 78 one-second lines of each run the renderer's drawn
  tile count (`terrain 1s: tiles=`) equals the residency's `visible=` (the other differs by 2: the two lines are
  snapshots of different frames). The renderer's `nocache` is also non-zero in about 21 s of each run: it counts
  class-1 (deep water) tiles, which neither the EXE nor the server bakes; the gray counter counts land tiles only.
- **Land tiles**: 4,922 with the server's window (tile + 4 samples), against the EXE's 4,888 (4,729 class 3 + 159 class
  2 in 3401's probe of the same save): the wider window adds 34 coastal tiles.
- **Video memory: 9,844 separate BC1 textures cost about 627 MB, not 410 MB** (1,434 - 807, radius -1): each 43,680-byte
  texture takes a 64 KB allocation [I: the driver's granularity; 9,844 x 65,536 B = 645 MB]. The EXE avoids this with
  its array pool (6,144 slots of one texture array, terrain-bake-records.md 3.2). The real baker (3404) or the residency
  should allocate t0/t8 as slices of texture arrays: proposed contract change, see 5.
- Server private memory grows by 693 MB at radius -1 with the stand-in (immutable textures with initial data; [I] the
  driver's system-memory copy of each texture). The real baker writes its caches on the GPU (no initial data), so this
  figure must be measured again with it.
- The stand-in's bake costs 0.06-0.18 ms CPU per tile (max 4.6 ms, the first ones). The background budget of 400
  tiles/s is therefore only a stand-in figure: with the EXE's GPU cost of about 1.2 ms per tile (3023, 3.4), 400/s
  would take about half of the GPU. The budget must be set from the real baker's `gpuMs` (proposal in 5).

### 4.2 Acceptance D: a road built by Lua is re-baked by the server [L]

Same live run as 3.3. Beside the game, `shell_server.exe --scene terrain --bake standin --bake-radius -1 --tap-file
<3401's huge tap of the same save> --tilegen Local\VPTileGenW7` (this branch's build at 6e6646b, hidden desktop, no
camera: the bake-ahead ran from the map centre and finished 4,922 tiles in 12.5 s), fed by `tilegen_feed.py --section
Local\VPTileGenW7` (the EXE's +0x1CC of all 10,752 tiles every 0.25 s, ReadProcessMemory). Server log
`D:/c5-64bit-work/3400/w7/liveD/server.log`:

| Event | Time (EST) | After the road |
|---|---|---|
| `SetRouteType` returned (Lua channel) | 06:34:11.976 | 0 |
| EXE's +0x1CC raised on 16 tiles, seen by the feed | by 06:34:12.47 | <= 0.49 s (first poll) |
| server: "tile generation block seq 4: 16 tiles changed, re-bake requested" | 06:34:12.522 | 0.55 s |
| the 16 tiles re-baked and set (totals: raises 16, rebakes 16, latency n 16) | in the pump that read the block (the code: raise, then pump with a full bucket of 16 tokens); the next 1 s line, 06:34:13.223, shows bakes 4,922 -> 4,938 | 0.55 s [C + L]; at the latest 1.25 s [L] |

**Re-bake within 0.55 s of the road with the stand-in baker** (1.25 s by the 1 s counter line alone), bounded by the feed's period (0.25 s) plus one sweep
of the 10,752 tiles by ReadProcessMemory, plus the bake itself (the real baker's ~1.2 ms GPU per tile [I, 3023] x 16
tiles, visible ones first). The EXE in the same run kept the same 16 tiles stale for 70.8 s (3.3). Corrections made
after this run: the server's latency was measured from its own poll (hence "latency max 0 us" in that log); since
2026-11-13 06:45 the signal time is the producer's `writeQpc` from the block and the end time is taken after the
cache is set, so the counter includes the hand-over [C, code; not re-run live].

### 4.3 Tests [L, own binaries]

`shell/tests/test_residency.py`, 27 tests (product level: `shell_server.exe` on the hidden desktop, log, counters and
exit code asserted): whole map resident and its order (visible first, rings ascending, never class 1), visible tiles
baked before the first draw, the gray counter when the budget is too small, radius 0 and 5 from the options file with
eviction after a camera move, re-bake on a raised generation (block), re-bake of only the changed tile after a new
tap of the same size, a tile that fails to bake (plain, retried only after its generation rises), out of memory
(radius -1 -> 5 -> ..., inside the budget) and out of memory at radius 0 (baking stops), options file with a bad
value and `--bake-radius` over it, four block errors (size, magic, version, missing), eleven bad command lines (exit 2)
and `--bake standin` outside the terrain scene. 2026-11-13 06:19-06:24: with `test_terrain_scene.py`, **119 passed, 0
skipped** (165 s); ASan build (`shell\build.cmd asan`, MSVC `/fsanitize=address`): **27 passed, no ASan report**,
peak commit 592 MB (06:29-06:30). Final, with the GPU budget and its test (29 tests; build of 7fc0f8f + main merged,
07:38-07:44): **121 passed, 0 skipped** (172 s); ASan **29 passed, no report** (`D:/c5-64bit-work/3400/w7/asan-logs/`
empty), peak commit 596 MB.

Video memory in that run's end line was 15 MB for the server process while the game ran [I: WDDM demoted the idle
server's textures under the game's pressure; not investigated].

## 4b. Wave 3 (2026-11-13 08:05-): the library, the seam, the real baker

**The library.** The residency moved from `shell/server/` into `render/` as `c5r::bake::Residency`
(`render/include/c5r/bake/residency.h`, `render/src/bakeresidency/`, tests `render/tests/bakeresidency/`), with the
stand-in as `c5r::bake::StandinBaker` (`standin.h`). Its calls are those of epic 3300's seam
`srvrender::TileCacheSource` (`server/src/render/srv_tile_source.h` on 3300's branch, agreed by the team-3400 lead):
`onField(dev, grid, tilePlots, loadSeq, map)`, `onPlotsChanged(plotIndices, count)`, `beforeDraw(ctx, Renderer&,
view, budgetMs)`, `idle(ctx, budgetMs)`, `release()`, `statsLine(out, n)`, plus `raiseTile` for the tile generation
block. A host adapter in server.exe forwards the seam's calls; render/ includes nothing of server/. `shell_server.exe`
keeps only thin calls (`--bake pipeline|standin`), and the block's reader became header-only (`tilegen_block.h`), so
`shell/build.cmd` lost the two residency sources and gained nothing.

- **Sink at beforeDraw.** The seam hands the renderer only to `beforeDraw`, so caches baked in `idle` are marked and
  set at the next `beforeDraw`; the gray-frame count is taken there, after the caches are set, for the frame about to
  be drawn.
- **onPlotsChanged** maps a game plot (index y * plotsX + x, the map without the grid's 2 + 2 margin rows [I]) to its
  tile (x, y + 2) and raises the tiles within 2 columns and 3 rows: the live road of 3.3 invalidated x 99-101, y 23-28
  for plots (99, 24) and (99, 25), which this rectangle covers [I for the general rule].
- **Re-bakes set `BakeRequest::areaChanged`** (the EXE makes t8 then, 3404); `Err::OutOfMemory` replaces the message
  test; the baker's `gpuMs` feeds the GPU budget (PipelineBaker still returns 0, so `--bake-gpu-ms-per-s` has no effect
  with it yet).

**BC1 texture arrays, and why the renderer still gets Texture2D copies [L].** t0/t8 live in BC1_TYPELESS arrays of 256
slices (10.7 MB per chunk), handed to the baker as `BakeTarget`; a baker that returns its own textures (PipelineBaker
today) is copied into the slice. The EXE's terrain pixel shaders declare t0/t8 as `Texture2D`; binding a
`Texture2DArray` view to such a slot is undefined by the D3D11 specification. Test `array_slice_view_reads_as_texture2d`
(RTX 4080, 2026-11-13): it does read the slice on this driver (and a control through a `Texture2DArray` declaration and
a Texture2D copy read the same colour), but shipping on undefined behaviour across players' GPUs is not acceptable,
so the residency hands the renderer a **Texture2D draw copy** (a pool) for each tile within one ring of the view,
refreshed when re-baked: 436 textures for the huge path below (about 29 MB with the 64 KB allocations).

**Two slices per tile from the first bake, re-bakes in place (issue 0137, 2026-11-13) [C, L].** Re-bakes set
`areaChanged`, with which the pipeline always makes t8 (the EXE's tile+0x300), so a tile baked without t8 gained a t8
slice at its first re-bake; the arrays grew chunk by chunk with the AI's turns (+11.0 MB of server private per 256-slice
chunk, 3311's run) up to 2 slices per land tile. Since 0137 a resident tile holds its t8 slice from the first bake even
when the bake made none (`ResidencyStats::t8Held`, `t8_held=` in the stats line) and a re-bake writes into the tile's own
slices: the arrays are at 2 slices per land tile after the bake-ahead (huge save 9,844 slices, 39 chunks, 415.9 MB) and do
not change with re-bakes. Test `rebake_same_tiles_10000_times_memory_flat`; figures in `renderer-in-server-design.md` 4.15 (its private-bytes readings under load: 4.15.1, issue 0173).
*(The table below was measured before 0137: "33 chunks = 351.9 MB" for 3,485 tiles with t8 of 4,918.)*

**Tests.** `render/tests/bakeresidency/residency_test.cpp` (13 tests, C++ product tests in the render suite, with
`c5r::terrain::Renderer` as the sink): every option and input check with its Err; whole map resident in arrays (no
stand-in textures of its own); visible first and the gray counter; radius 0 eviction after a move; `raiseTile` and
`onPlotsChanged` (35 tiles around one plot, an index past the map ignored); a field change re-bakes one tile; a failed
tile; out of memory through the arrays' cap and at radius 0; out of memory of the baker without arrays; the GPU budget;
the array-view test. 13/13 passed 2026-11-13 08:24. `shell/tests/test_residency.py` adapted (37 tests, incl. the
pipeline on 3401's Tiny tap and its two refusals): 37/37 passed 08:27. Final (branch at f6dd9a9, 08:36-08:54):
`test_residency.py` + `test_terrain_scene.py` **129 passed, 0 skipped**; `render/build.py --asan` (MSVC
`/fsanitize=address`, canary reported) `c5r_bakeresidency_test` **passed, 0 ASan reports**, peak commit 607 MB; shell
ASan build `test_residency.py` **37 passed, no report** (the shell ASan variant links the non-instrumented c5render,
so the library itself is covered by the render ASan run).

**Acceptance C offline with the real baker** (2026-11-13 08:29-08:38, RTX 4080, branch at 078de1c, `--bake pipeline`
with the install `pure_vp` read-only and the EXE's shaders; stand-ins inside the pipeline as 3404 lists them; same
path as 4.1 but 90 s still for the bake-ahead; no hold: no timing claim; raw `D:/c5-64bit-work/3400/w7/driveP_r*/`):

| Radius | Bake-ahead | Resident | Gray frames after the bake-ahead | Arrays | Video local (start -> peak) | Server private (start -> end) |
|---|---|---|---|---|---|---|
| -1 | **4,918 tiles in 50.4 s** (4 failed, below) | 4,918: 350.0 MB of BC1 (3,485 tiles with t8) | **0** of 7,284 frames | 33 chunks = 351.9 MB, + 436 draw copies | 806.8 -> 1,281.7 MB (+475) | 1,156 -> 1,665 MB |
| 5 | 434 tiles in 5.1 s | 153 at the end (11.2 MB); 3,140 bakes, 2,987 evicted | **1,063** of 6,915 | 32.0 MB at the start view | 806.8 -> 942.5 MB | |
| 0 | 104 tiles in 1.4 s | 42 at the end (3.5 MB); 2,364 bakes, 2,322 evicted | **1,119** of 6,865 | 10.7 MB at the start view | 806.8 -> 919.3 MB | |

- The bake costs **7.7 ms of CPU per tile on average (max 23.8 ms)** in the server thread (recording the passes; the
  GPU time is not reported by the pipeline yet). With the 4 ms budget per call that is about one tile per `idle`, about
  100 tiles per second: the bake-ahead of the huge map takes 50 s, not the stand-in's 12 s, and at radius 5 / 0 every
  jump into unbaked ground shows gray for seconds (1,063 gray frames after the bake-ahead at radius 5). **Radius -1
  is the only setting without gray tiles after the bake-ahead with the real baker.**
- **Video memory: arrays cut the caches from ~627 MB (separate textures, 4.1) to 352 MB of arrays** for 350 MB of
  BC1, plus ~29 MB of draw copies. The peak rise of 475 MB includes the pipeline's own resources (blend targets, the
  layer textures it loads). The video figure fell to 649 MB at 08:30:43 while the arrays were still held [I: WDDM
  demoted the idle server's allocations under another process's pressure, as in 4.2].
- **4 tiles fail with the real baker**: (111, 43), (112, 43), (111, 44), (112, 44): `NotFound: texture
  Generic_Gibraltar_H.dds not in the install` (the slope-projected Gibraltar material of 2.4). Plain until their
  content changes; for 3404 (the pipeline's texture lookup). *(Corrected 2026-11-13, one-off 9047: fixed since 3404
  section 9 (terrain-bake-comparison.md); the fresh clone of main 3f24adb bakes all 4,922 tiles of this field, 0 failed,
  in 16.6 s (terrain-bake-verification.md 6). Was: "4 tiles fail with the real baker".)*

## 5. Not verified / open

- **The real baker.** Everything above ran on the stand-in (flat BC1 per tile). With 3404's `TileBaker`: the background
  budget should come from its `gpuMs` (`--bake-gpu-ms-per-s`, implemented and tested with the stand-in's simulated
  cost; its default value is to be chosen from a measurement), the bake-ahead duration and the private / video memory must be measured again, and frame-time impact
  measured under the quiet lock (PERFORMANCE.md); none is claimed here.
- **Texture arrays.** Separate 43,680-byte textures cost ~64 KB each (4.1); t0/t8 as slices of BC1 texture arrays
  would bring radius -1 from about 627 MB to the 413 MB of D88. Contract proposal for `baker.h`: `BakeRequest` gains a
  target (array texture + slice) or `TileBaker` gains an allocator hook; the residency would own the arrays.
- **Out of memory in the contract.** The residency recognises OOM as `Err::Device` whose message holds `0x8007000E`.
  Proposal: `c5r::Err::OutOfMemory` (status.h, render/) or a documented message rule in `baker.h`.
- **The product's signal.** The tile generation block is fed by a PID reader today. On the proxy route the DLL should
  send plot changes (route, improvement, feature, terrain) over the IPC (epic 3300), and the server maps a plot to
  the EXE's tile rectangle (the 16 tiles of 3.3 show the EXE invalidates more than the plot's own tile); or the
  composite DLL's bake tap publishes +0x1CC. Not decided here.
- **Gray frames at radius 0 / 5** after jumps (4.1) follow from burst 16 and the 4 ms pump cap; whether visible tiles
  should be exempt from the cap is a frame-time question for the real baker. (2026-11-16, ticket 0409: they are now,
  when more than the burst have no bake: `terrain-bake-residency-first-draw.md`.)
- **Live acceptance C with the game's own camera** (D33 route or the proxy route of 9040) was not run: the offline
  path uses the EXE's camera matrices from the ground truth, translated; the live run is prepared by `residency_drive.py`
  for the offline part and goes to 3408.
- ~~The callers of 0x4387D0: 0x49B5A0 (TerrainDecalSystem.cpp) is the likely road / improvement path [I]; 0x68F1B0 and
  0x6B5A50 are not identified~~ identified 2026-11-15 by epic 0166 (section 3, terrain-decals.md 5.1): 0x49B5A0 AddDecal,
  0x68F1B0 RemoveDecal, 0x6B5A50 and 0x678FF0 terrain edits. *Corrected 2026-11-18 by epic 0465 (4900, `live-terrain.md` 4.1) [C, static]: 0x678FF0 is the Nexus terrain editor's 32 x 32 material brush, not a map-script or plot-type edit; 0x6B5A50 is the marsh stamp (from `GameplayFeatureChanged`); 0x678FD0 is an unrelated Lua registration. Replaced: "terrain edits (map scripts, plot type changes)".* Still: no breakpoint was set to confirm which one the live
  road of 3.3 took.
- The height class window's exact edge rule (94 % of huge tiles reproduced, 1).
- *2026-11-13 (3408): "Live acceptance C with the game's own camera ... was not run" is answered by section 6.*

## 6. Live acceptance with the game's camera (issue 3408, 2026-11-13) [L]

Written by team-3400 W8. Driver `shell/tools/bake_live.py` (tests `shell/tests/test_bake_live.py`), evidence
`shell/evidence/3408/` (camera scripts, per-step CSVs; no pictures), raw runs and pictures in
`D:/c5-64bit-work/3400/w8/` (not committed). Machine: the owner's workstation, RTX 4080, game window 2554 x 1411, Goldberg
instance c5h-b, CP/VP/EUI, default zoom. Other teams' builds, suites and games ran on the machine during every run
(quiet-machine lock not taken: no frame time is claimed; gray frames, bake-ahead durations and counts are not timing
claims of the PERFORMANCE.md kind).

### 6.1 The camera script (same for every arm)

`bake_live.py script` on the live game, once per save, seed 3408: the start at the active player's capital, 4 pans of 10
plots (east, north, west, south; one `UI.LookAt` per plot every 0.15 s), then 24 `UI.LookAt` jumps, every third one back
to an earlier jump's plot (8 revisits, never the plot just before). Huge (`C5H-3408-HUGE_0350`, copy of
`C5H-VP8P-HUGE_0350`, c499d3c6..., turn 350): start (101, 25), jump targets from 300 revealed land plots on a 3-plot
lattice. Tiny (`C5H-3408-TINY_0008`, copy of `c5h-9028-tiny-t0008`, b2ceb663..., Game.GetGameTurn 9, map 56 x 36): at this
turn too little is revealed for 24 jumps (1 revealed land plot on the 3-lattice), so its script takes any land plot on a
2-plot lattice (127 plots, `--lattice 2 --all-land`); many of its cameras look at unrevealed (black) land. Scripts:
`shell/evidence/3408/script_huge.json`, `script_tiny.json`. Each step ends with the EXE's camera (`gt_reader`) and a
screenshot (PrintWindow, no focus change).

### 6.2 Stock baseline (current pin, stock configuration)

2026-11-13 08:17-08:27 EST, `i0ship-bbfacca1-ia` (E589306E...), stock config (`[DisableSystems] Terrain = 0`, no
view-mask marker), `bake_live.py run --arm stock`: after each `UI.LookAt` the EXE's pool is read by PID every 0.1 s
(pool_watch.py's sample: the visible list TR+0x1AF0 and each visible tile's colour cache tile+0x198) until all visible
tiles have held a cache for 2 s. "Time to all cached" = from the `LookAt` call to the first sample after which every
sample had all visible tiles cached. Per step: `shell/evidence/3408/stock_huge_steps.csv`, `stock_tiny_steps.csv`.

| Save | Steps | Steps with a visible tile without a cache | Most visible tiles without a cache (of ~121) | Time to all cached, jumps + revisits |
|---|---|---|---|---|
| Huge t350 (pid 48748) | 29 | **28**: start 1, all 16 jumps, **all 8 revisits**, 3 of 4 pans (max 4) | 107 (a jump); revisits up to 56 | n 24: median **0.60 s**, min 0.26, max **1.34 s** |
| Tiny t9 (pid 27336) | 29 | **0** | 0 (34-43 visible) | 0 (always cached) |

- Every revisit on the huge map found gray tiles again (8 of 8, 1-56 tiles): the pool (about 3,072 tiles,
  terrain-bake-records.md 3.2) does not keep the places the script visited 3-10 jumps before. The Tiny map's land fits
  the pool, so stock shows no gray tile there after load.
- The times agree with 3023's 0.57-0.66 s at default zoom on the same GPU (terrain-bake-records.md 3.4); D86's "up to 10
  s" is the slow-GPU and moving-camera case, not reproduced on this machine at default zoom.

### 6.3 The server arm: route and pins

**D33 on the proxy route of 9040** (proxy-route-compositor.md): the EXE loads the x86 proxy, the proxy loads the
composite DLL (`crashlogs\shell.helper`) and writes the D33 view mask (`crashlogs\viewmask.enabled` = `clear=0x1`;
viewmask.log: view 0xFFFFFFFF -> 0xFF040662 at boot, the load's 0x801 cleared at the first Update). Pins:
- proxy: **9040's off-pin build** (repository fbbe6a1, `proxy_shell.cpp`), `CvGameCore_Expansion2.dll` 8F10D815...,
  with its `VP64\server.exe` 77877FB1... and x64 DLL 12B7F4FB... (9036's release build of the pinned x64 source 76992d235,
  not the bytes of `x64-76992d23`). The pinned proxy `x64-76992d23-p1607d42` cannot be used: repository commit 1607d42
  does not contain 9040's `proxy_shell.cpp` (`git merge-base --is-ancestor d132c7b 1607d42b7` false), so it neither loads
  the compositor nor writes the view mask. Fallback 32-bit DLL beside the proxy: E589306E (the current i0-ship pin).
  vpproxy.ini `mode=on local_answers=1 local_verify=0 lua_mirror=0` (as 9040).
- `shell_composite.dll` (40D7AB13...) and `shell_server.exe` (E0AB8FAF...) built from this branch at 16a2bfd
  (`shell\build.cmd`); composite `mode=composite insert=tagged tag_rule=after tag_match=clear_bb camera=1 tap=0 bake_tap=0`.
- The server takes the height field from **3401's tap file of the same save** (`--tap-file
  D:/c5-64bit-work/3400/ref/C5H-3401-HUGE_0350/armA/heightfield.c5hf`, read live by PID on the current pin) instead of the
  live tap, so that it can be restarted for each radius in one game process (the live tap is published once per load);
  the camera is the game's own, live, through the composite DLL's camera block.
- `bake_live.py run --arm server` starts `shell_server.exe --scene terrain --bake B --bake-radius R --fallback constant
  --constant-rgba 0xFFFF00FF` on a hidden desktop, waits for `residency: bake-ahead complete`, then runs the script with
  4 s per step and ends the server. Gray frames are the residency's own counter (a drawn frame with a visible land tile
  without its bake); video memory is the server process's DXGI `QueryVideoMemoryInfo` local `CurrentUsage` (its `mem
  periodic` line), next to the residency's own count of resident bytes.

### 6.4 Acceptance C live: results

| Save | Baker | Radius | Bake-ahead | Frames during the script | Gray frames during the script | Resident | Video local (server, DXGI) | Server private |
|---|---|---|---|---|---|---|---|---|
| Huge | stand-in | -1 | **4,922 tiles in 12.97 s** after the field | 75,654 | **0** | 4,922 = 410.1 MB | 806.8 MB at start, 1,485.0 MB 20 s later (peak); 384-503 MB once the game drew (WDDM demotion, below) | 1,216 -> 1,949 MB |
| Huge | **pipeline** (W7 f6dd9a9 + W4's wave-2 PipelineBaker, stand-ins inside the pipeline as 3404 lists them) | -1 | **4,918 tiles in 57.98 s** after the field (4 failed) | 90,885 | **2,709, all from the 4 failed tiles** (below); 0 from any other tile | 4,918: 350.0 MB of BC1 in 351.9 MB of arrays | 806.8 -> 1,352.5 MB (peak); 613.6 MB at the end | 1,176 -> 1,739 MB |
| Huge | pipeline | 5 | 409 tiles in 4.77 s | 24,789 | **2,013**: 20-98 after each jump / revisit (median about 64), 924 at jump 18 (Gibraltar), 0 in 3 of 4 pans | 296 at the end (21.8 MB, 32.0 MB of arrays); 8,934 evicted | 806.8 -> 994.5 MB | 1,188 -> 1,355 MB |
| Huge | pipeline | 0 | 86 tiles in 0.96 s | 40,408 | **2,687**: 24-98 after every step incl. all 4 pans, 1,218 at jump 18 | 86 (7.2 MB, 10.7 MB of arrays); 3,143 evicted | 806.8 -> 964.0 MB | 1,200 -> 1,336 MB |
| Tiny | pipeline | -1 | **1,352 tiles in 16.26 s** (0 failed) | 142,291 | **0** | 1,352: 102.1 MB of BC1 in 106.6 MB of arrays | 175.1 -> 447.3 MB (peak); 174.0 at the end | 326 -> 597 MB |
| Tiny | pipeline | 5 | 395 tiles in 4.56 s | 70,695 | **0** (the 5 rings hold most of the small map's land around each camera) | 398 (28.2 MB); 302 evicted | 175.1 -> 347.4 MB | 331 -> 490 MB |
| Tiny | pipeline | 0 | 104 tiles in 1.15 s | 73,726 | **84** (start 35, pans 15 + 29, two jumps 20 + 20) | 104 (6.9 MB); 171 evicted | 175.1 -> 298.3 MB | 336 -> 433 MB |
| Tiny | **pipeline, exact inputs** (W4 d597edf + W7 2bbf7a1, build `shell-build-q` 0D9CC24E...) | -1 | **1,352 tiles in 5.02 s**, 270.3 ms of baker GPU time in all (0.20 ms per tile) | 118,533 | **0** | 1,352 (102.2 MB; 106.6 MB of arrays) | 175.0 -> 456.9 MB | 328 -> 605 MB |

- With W4's exact-input defaults the pipeline reports its GPU time and bakes about 3 times faster on the CPU side: Tiny's
  bake-ahead 5.0 s against 16.3 s with the wave-2 pipeline (10:53:31-11:00 EST, game pid of that launch, same route);
  huge: 4,918 tiles in **19.39 s** with 1,262.6 ms of baker GPU time (0.26 ms per tile; 10:45 EST, pipeB, before that
  run's EXE crash, 6.6), against 58.0 s.

- Tiny (09:09:54-09:22:07 EST, game pid 7988, same build and route, tap file `C5H-3401-TINY_0008/armA`): stock showed no
  gray tile on this map at all (6.2), and the server at radius -1 and 5 shows none either; radius 0 shows a few dozen
  gray frames after 5 of 29 steps.

- Radius 5 and 0 (09:01:16-09:06:2x, same game process as the radius -1 run): every jump into ground outside the kept
  rings shows gray for a few dozen server frames (the server draws once per camera block, about 250 per second here, so
  roughly 0.1-0.4 s per jump [I: frames / rate]) at the default burst 16 and the 4-ms pump cap, with the real baker's
  8 ms per tile. As offline (4b), **radius -1 is the only setting without gray tiles after the bake-ahead**; 5 and 0
  trade 330 MB of video memory for gray after each jump, which is still shorter than stock's 0.26-1.34 s on this GPU
  [I: the units differ, frames counted against tiles sampled every 0.1 s].

- **Pipeline arm, radius -1** (08:54:43-09:01:16 EST, game pid 51692, shell_server fff9acc3... built 08:47 from this
  branch with W7's branch f6dd9a9 merged; `--bake pipeline --game-dir <c5h-b install>`): the bake-ahead took 58.0 s at
  about 85 tiles per second (8.2-8.3 ms CPU per tile; the pipeline reports no GPU time yet: `0.0 ms of baker GPU
  time`). Every gray frame after it (2,709 of 90,885) fell on one camera, jump 18 to plot (111, 46), at 2.0 gray tiles
  per frame (5,428 gray tile-frames): the tiles (111..112, 43..44) whose bake fails with `NotFound: texture
  Generic_Gibraltar_H.dds not in the install` (4b), two of which that camera sees [L for the counts and the failures, I
  that the two gray tiles are those: the counter does not name tiles]. Those tiles stay plain until their content
  changes. **So with the real baker acceptance C holds at radius -1 for every tile the pipeline can bake, and fails
  on the 4 Gibraltar tiles until 3404's texture lookup finds that material** (the stock EXE bakes them: the stock
  arm's step 18 had every visible tile cached after 0.94 s).

- **Radius -1 meets acceptance C on the live route with the stand-in baker**: 0 gray frames over the 4 pans, 16 jumps
  and 8 revisits (75,654 server frames, 08:32:17-08:35:39 EST); the only gray frames of the run (4 frames, 127 tile
  frames) were the first frames after the server started, before the bake-ahead's first visible pass. Stock at the same
  cameras: gray tiles after 28 of 29 steps (6.2).
- Video memory: the resident set costs about 678 MB of local video memory with separate textures (1,485 - 807; 4.1's
  "64 KB per texture"), then the driver demoted the idle tile textures while the game drew in the foreground process:
  433 MB from 08:33:38 on, 384-503 MB afterwards, with no gray frame (the residency still holds every texture; a demoted
  texture is paged back on use [I]). The residency's own count (410.1 MB) is the figure of D88.
- Private bytes rose by 733 MB with the stand-in's immutable textures (4.1 saw 693 MB offline).

### 6.5 Acceptance D live (stand-in baker)

Same run, after the script: `tilegen_feed.py --pid <EXE> --section Local\VPTileGen3408 --period 0.25` (the EXE's
tile+0x1CC into the server's tile generation block), camera on (99, 24), a road on (99, 24) and (99, 25) by
`SetRouteType(ROUTE_ROAD, -1)` at 08:35:45 (both returned 0).
- **Under D33 the EXE still raises +0x1CC for a road**: 8 tiles, x 99-100 and y 24-27, from 57 to 74,754 (the frame
  counter), all in state 1/1 with +0x1D0 31-34 (baked once by the load's bake-ahead) [L]. The same road in the stock
  configuration raised 16 tiles (x 99-101, y 23-28, 3.3): the rectangle differs [I: not traced; the decal system's
  rectangle may depend on what is drawn]. The EXE never re-baked them (its terrain is off under D33; the feed's
  `never re-baked` lines), so under D33 the server can use +0x1CC as the signal but not the EXE's bake.
- The server logged `tile generation block seq 4: 8 tiles changed, re-bake requested` **0.49 s after the road** and
  re-baked all 8 (rebakes 8, raises 8); latency from the feed's write to the cache set: max 6.5 ms, mean 5.6 ms. So
  **re-bake within 0.5 s of the road**, bounded by the feed's 0.25-s period and one ReadProcessMemory sweep.
- **With the real baker** (pipeline arm, same route, road at 09:00:07 on the same two plots of a fresh load): the same 8
  tiles raised; the block line came **0.52 s after the road**, all 8 re-baked (rebakes 8), latency from the feed's write
  to the cache set max **71.2 ms**, mean 39.9 ms (8.2 ms CPU per tile with the pipeline, the visible ones first). So
  **acceptance D with the real baker: re-baked within 0.6 s of the road** (0.52 s signal + at most 0.07 s bake), of which
  the signal path (the 0.25-s feed period plus one sweep of the EXE's 10,752 tiles) is the larger part; the product's
  signal (the DLL's plot message, 5) removes the feed's period.
- Tiny, real baker (road at 09:14:35 on plots (49, 12) and (49, 15) next to the capital (51, 14)): the EXE raised 6
  tiles; block line 0.22 s after the road, 6 re-bakes, latency max 66.4 ms (mean 41.8): **re-baked within 0.3 s**.
- Tiny, exact-input pipeline (road at 10:58:05, same two plots, fresh load of the same save copy): the EXE raised 10
  tiles this time (6 before: the rectangle is not the same on every load [L], cause not traced); block line 0.32 s after
  the road, 10 re-bakes, latency max 34.5 ms (mean 21.8): **re-baked within 0.4 s**.

### 6.6 Not verified / open (3408)

- *2026-11-13 (issue 3313, team 3300 W13): the same camera script on the **product path** (server.exe's own Residency and
  pipeline baker, D96 pins, Overlay on) at radius -1 / 5 / 0 is in `renderer-in-server-design.md` 4.13 [L]: radius -1 **0
  gray frames after the bake-ahead** over the 4 pans, 16 jumps and 8 revisits (4,922 tiles in 16.5 s, 0 failed: the
  Gibraltar tiles bake now), lockstep 99.994 % exact; radius 5: 1,071 (14 of 16 jumps, 7 of 8 revisits, no pan), 88.9 %
  exact; radius 0: 1,291 (every step), 93.8 % exact. No EXE crash at the first pan (bug 0127 fixed by D93).*

- **W4's exact-input defaults** (3404 wave 3, d597edf, merged into 3408's branch directly at 09:38; the team branch was
  still 16a2bfd): Tiny radius -1 ran in full with them (0 gray frames, 6.4); on huge both attempts (pipeB 10:45, pipeB2
  11:03) lost the EXE at the first pan (next bullet) after the bake-ahead (19.4 / 20.6 s), so the full huge script at
  radius -1 / 5 / 0 rests on the wave-2 pipeline (pipeA). Gray-frame counts do not depend on the inputs; the
  bake-ahead's duration and the picture do.
- **The 4 Gibraltar tiles** (111..112, 43..44) are the only gray tiles at radius -1 with the real baker: 3404's texture
  lookup for `Generic_Gibraltar_H.dds`.
- **An EXE access violation (0xC0000005)** ended one stand-in run at radius 5 (2026-11-13 08:50:17 EST, game pid 5444,
  14 s into the script, first pan, D33 proxy route of 6.3); no dump was written (the harness wrote none and
  `%LOCALAPPDATA%\CrashDumps` has no file of that time). **A second one** (same code 0xC0000005, game pid 3208,
  10:47:25 EST, 178 s after the proxy's boot) ended the first exact-input pipeline arm (pipeB, radius -1) also right
  after the **first pan** of the script (east from the capital (101, 25) to (111, 25)): the step's screenshot / plot-list
  calls stopped answering after the pan; again no dump. **A third** (pipeB2, game pid of the 11:00 launch) the same
  way: the EXE stopped presenting at 11:03:44.5 (the server's camera blocks fell from ~250/s to 0; composite stats
  lines end 11:03:44), right as the first pan began. So **3 of the 5 huge game launches on this route** (08:46, 10:41,
  11:00) lost the EXE at the script's first pan; the other 2 (08:28: stand-in radius -1; 08:51: the pipeline at -1, 5
  and 0, three servers in one game) and both Tiny launches (09:07, 10:51) passed the same pans [L]. The stand-in arms
  at radius 5 and 0 were therefore not completed; the pipeline arms cover those radii.
  **Cause (bug 0127, settled by one-off 9046, 2026-11-13) [C + L]:** not the baker, the residency or the server. Each
  EXE died at the moment presenting stopped (x64 server logs: pid 5444 08:50:16.8, pid 3208 10:45:59.2, pid 63048
  11:03:44.9); there is no hang before the fault. All three faulted at EXE VA 0x574B03, the strcmp in the EXE's
  SerialEventHexHighlight handler 0x574A00: under D33 the Overlay system is off, Highlights.xml never loads, and a
  named-style highlight that the game's own UI sends on the pan inserts a style record built from uninitialised stack
  and compares its first word. Fix recommended to the MC: keep Overlay on in D33. Details:
  `research/02-exe-internals/disable-systems-and-masks.md` section 5, "Hex highlights with the Overlay system off can
  crash the EXE (bug 0127)" (9046's branch, pending the MC's merge).
  *(Corrected 2026-11-13 by the team-3400 lead from 9046's result. Replaced: "the render thread hangs first, then the
  process faults ... with 0xC0000005 about 80 s later"; the 80 s were `bake_live.py`'s own timeouts (10 + 10 + 60 s)
  after the death, and the cause was given as not known.)*
- The pipeline reports no GPU time (`0.0 ms of baker GPU time`), so `--bake-gpu-ms-per-s` has no effect and the GPU
  cost per tile is not known live.
- Frame time with the residency (the visible bakes before a draw at radius 5 / 0, the background pump) was not measured:
  no quiet-machine hold was taken (PERFORMANCE.md).
- The server's video figure is the process's DXGI local usage, which the driver lowers by demoting idle allocations
  while the game draws (6.4); the residency's own count is the stable figure.
- The stand-in radius 5 / 0 rows and a Tiny picture comparison are missing (6.4 and terrain-bake-comparison.md 8).

## 7. A live terrain edit of the same field (ticket 4909, 2026-11-19) [C, tests; L in live-terrain.md 8.14.5]

- `Residency::onFieldRegion(dev, grid, rects, map)`: the same field edited in place inside rects (a live terrain window,
  epic 0465). Only the tiles whose read window (64 + 2 x 4 samples) meets a rect are compared, by `onField`'s own rule
  (one shared `scanTile`), so every tile ends as `onField`'s comparison leaves it; no tile is marked dirty (the terrain
  renderer's `updateHeightRegion` keeps its caches) and the bake scheduling (tokens, bake-ahead, first-draw counters)
  runs on. Bit-equal to `onField` by digest on synthetic fields and on 4903's recorded chains (live-terrain.md 8.14.4);
  0.9-2.3 ms instead of 37-49 ms on Tiny and 204-291 ms on huge (offline, DIAGNOSTIC; live, the whole `ResidencySource::onField` step took 55-98 ms on Tiny and 450 ms on huge).
- The pipeline baker's EXE height pages are kept by the grid's address and `h.data()`, so after an in-place edit a
  re-baked tile read the old page (live-terrain.md 8.14.2). `PipelineBaker::heightsChanged(grid, boxes)` drops the pages
  a box reaches (all without boxes); `ResidencySource` calls it after `onFieldRegion` (the window's rects) and after
  every whole `onField`.

## 8. Idle bakes fitted between cameras (ticket 0703, 2026-11-23) [C: code; L: logs, tests, runs as marked; I: marked]

Written 2026-11-23 by the 0703 implementer (account B, branch `task-0703-camera-preempts-bake`). Input: 0689's split
(pan-coherence.md 5.8, 5.9). Reader: `server/evidence/0703/persec0703.py` (per second: the residency's bakes, pre-draw
bakes, idle slices, yields, the near pairs' up-res and moving frames beside `pass 1s`'s predraw / idle / wake maxima).

### 8.1 What holds the camera in 0689's runs [L: L1 / L2 render.log of 2026-11-23 06:13-06:24, c5h-new-a, Tiny t18, uncapped]

- **Every idle slice of those runs ended with a camera waiting.** At the runs' end: L1 1,520 idle slices, 1,456 yields;
  L2 1,514 slices, 1,466 yields. A yield's bound (`yield_wait_*`: the time from the last check that found no camera to the
  check that found one, so at most the bake in progress): mean 3.20 / 3.40 ms, p99 7.25 / 36.12 ms, max 17.6 / 36.1 ms.
  The bakes: 1,713 / 1,701, `bake_cpu_mean_ms` 3.21 / 3.41, max 22.7 / 36.1 ms (first uses: a tile's height page, layer
  or decal textures; `pipeline: first use` lines of 7.4-25.4 ms). Uncapped, the EXE's next camera comes 1-4 ms after the
  server's pass (5.8: present interval p50 3.3-6.1 ms), so a 3.4-ms bake started in that gap is a 3.4-ms wait.
  Most of those bakes are the load's bake-ahead (about 1,450 land tiles) and the decal generations' re-bakes; in the
  moving seconds 4-71 idle bakes a second remained.
- **The moving `predraw` tail is the near pairs' up-res, not BC1 bakes.** In every second with a predraw maximum of 3
  ms or more after the first draw, `predraw_bakes` (BC1 bakes before the draw) is 0; the zoom-out seconds made 16 / 94 /
  14 (L1) and 23 / 89 / 12 (L2) state-5 up-res bakes (`near 1s: upres`) with predraw maxima of 7.8-12.0 ms. The up-res is
  the EXE's per-frame rule (1 a frame while the camera moves, 2 settled; residency.cpp `nearFrame`): which tile is drawn
  from its near pair in a frame is part of the picture, so it cannot leave the frame's pass without changing it. L1's
  first-move pass (frame 5371, predraw 22.7 ms, 1 up-res) came 28 ms after a new decal set (generation 2, the forest's
  412 decals) was applied in that `beforeDraw` [I: the set's apply or the up-res bake's first use of its textures; not
  split]. One BC1 bake cannot stop half-way: `PipelineBaker::bake` is one call (prep, page, textures, blend, normals,
  style, post, copy; `PipelineStepTimes`).

### 8.2 The change [C: `server/src/render/srv_residency_source.{h,cpp}`]

`ResidencySource::idle` (the host's background slice after each pass) decides when a bake may start; the residency
library (`render/src/bakeresidency/residency.cpp`) is unchanged.
- **The next camera** is expected at the end of the last drawn frame's pass (the first `idle()` call after a live
  `beforeDraw`) plus the gap the cameras kept after a pass: the 25th percentile of the last 16 gaps (pass end to the next
  live `beforeDraw`) in which no bake ran. Under exact lockstep (D242) the EXE publishes its next camera after it
  presented the server's frame, so the gap after the pass, not the frame period, is the quantity that repeats.
- **A bake** is expected to take the 75th percentile of the last 32 BC1 bakes (`DecalBaker::bake`'s wall time; idle,
  before the draw and held-frame bakes alike).
- **The decision** (`BakeFitBudget`, pure): window = expected camera - 0.3 ms - now. Window >= one bake: the slice runs
  with its budget cut to (window - one bake), so `Residency::idle`, which starts a bake while the slice's time is within
  its budget, starts its last bake early enough to end inside the window. Window shorter: no bake (the host then waits
  on `<base>.Cam` with its 2-ms bound, since the slice baked nothing). One bake is admitted anyway once 250 ms passed
  since the last slice that baked (`kBakeFitMaxDeferMs`), so the bake-ahead goes on at about 4 bakes a second at worst.
  Fewer than 4 bake samples: the slice as before; fewer than 4 gap samples: the bakes wait (their gaps become samples).
- **When.** `C5_RENDER_BAKE_FIT` (environment, read once; `ResidencySourceConfig::bakeFit` overrides): **0, the
  default: off** (the slices before 0703; set after the A/B of 8.4 and 8.5, 2026-11-23 18:3x; the MC's word decides). 1:
  while the camera moves (a moving frame, `ResidencyView::moving`, within the last 300 ms); at rest the slices are as
  before, so a load's bake-ahead keeps its pace. 2: always (an A/B arm). Only with the host's yield
  (`render_bake_yield=1`, the default); `settle()` (the offline players' only residency path) never fits and takes no
  samples, so a scene bundle's replay bakes as before: rule A is unchanged by construction.
- **What a frame draws is decided as before**: a visible land tile without a bake is baked before the draw; only the
  time at which a background bake (bake-ahead, a stale tile's re-bake) runs moves, which was already a matter of timing.
- **Lines**: `residency: idle bakes fitted between cameras: mode N (...)` at init (or `off (render_bake_yield=0 ...)`,
  and a LOUD line for a bad value); `residency fit 1s: mode moving gap_ms bake_ms admitted cut deferred deferred_gaps
  forced legacy fit_bakes gap_samples bake_samples | tot ...` in render.log once a second in which a call had work (a
  bake made or background work pending).

### 8.3 Tests [L: the render-host rig, 2026-11-23, workstation, build of this branch]

`server/tests/render/test_bake_fit.py` (6 cases, the rig of `test_bake_yield.py`: stand-in bakes of 8 ms, a 100-ms
budget, cameras looking off the map so every bake is an idle one): with moving cameras every 25 ms the slices are cut
to the window (gap estimate 25.7 ms, bake estimate 8.2 ms) and 2 of 17 slices ended with a camera waiting, against at
least half of them with `C5_RENDER_BAKE_FIT=0` (the test's bound); at rest every slice is the old one; with render_bake_yield=0 the fit is off; a bad
switch value is named; with cameras every ~2 ms no bake fits and the bound admits at most one bake per 250 ms.
`test_bake_yield.py` and `test_residency_source.py` pass unchanged.

### 8.4 Workstation A/B, uncapped (INDICATION, grade C) [L: 2026-11-23 16:48-17:13 EST, c5h-new-b, EXE 26577625]

Build of `f4ca978b8` (a branch commit before the hand-back's squash; `server/src` is identical in the handed-back
commit) (`C:/c5-64bit-work/0703/ship`: server.exe `b1a5b5d0...`, proxy `d1130083...`, composite
`18fb9d8c...`), the 0.1b pins; Tiny `C5H-0167-TINY_0008` (t9; the T18 save is only on c5h-new-a, which was cand5's), the
reporter's `[Renderer]` set, `lockstep_mode=exact` from the load, uncapped (`--timing`), hidden; per launch 0691's keys
(150 / 300 / 600 / 1200 ms) and drags (300 / 1000 ms), 1 repeat, then the wheel zoom-out and 60 s still
(`server/evidence/0703/ab0703_ws.sh`, which drives copies of 0691's / 0692's scripts). Four launches, interleaved:
fit0-1, fit1-2, fit0-3, fit1-4 (`C5_RENDER_BAKE_FIT` in the launch's environment; the init line confirms the mode).
The workstation ran other teams' jobs (4 heavy jobs registered at 15:51): an indication, not a timing claim (COMMON 23).
Readers: `server/evidence/0703/ab0703.py` (per server frame, pooled by arm), `split0689.py`, `pacing_report.py`.
Outputs in `C:/c5-64bit-work/0703/live2/` (not in git).

| per server frame, ms (pooled, 2 launches an arm) | fit 0 (before 0703) | fit 1 |
|---|---|---|
| moving frames | 1,382 | 1,473 |
| moving wake - publish p50 / p90 / p99 / max | 0.03 / 2.13 / 3.61 / 33.9 | 0.02 / 1.84 / 15.2 / 17.3 |
| ... of them key pans: p90 / p99 / max; over 5 / 10 ms | 2.04 / 3.61 / 19.5; 0.56 / 0.28 % | 1.38 / 3.72 / 8.6; 0.34 / 0 % |
| ... drags: max | 0.23 | 0.06 |
| ... the wheel zoom-out (about 98 frames): p90 / p99; over 10 ms | 2.75 / 33.9; 2.1 % | 15.7 / 17.3; 32 % |
| moving ready - publish p50 / p90 / p99 (per launch) | 4.15-4.22 / 9.6-10.0 / 21.2-24.1 | 3.95-4.16 / 7.6-7.9 / 25.6-29.7 |
| moving present interval p90 / p99 (per launch) | 16.1-16.3 / 25.3-30.2 | 14.1-14.2 / 29.9-33.3 |
| moving frames with ready - publish over 6.06 ms (per launch) | 27.3 %, 20.2 % | 15.7 %, 13.7 % |
| still wake - publish p99; over 2 / 5 ms | 2.96; 3.5 / 0.42 % | 3.85; 7.3 / 0.74 % |
| `pass long` (>= 20 ms) with predraw on top, whole launch | 44, 43 | 3, 11 |
| displayed interval (F2) p99 / p99.9; intervals over 50 ms | 23.5-27.4 / 62-68; 29-30 | 20.8-21.7 / 49-53; 17-19 |

- **No idle bake fits a camera gap here.** The gap estimates while moving were 0.1-0.6 ms (uncapped: the EXE's next
  camera comes right after the pass), the bake estimates 4.3-4.9 ms. So in moves every bake waited (deferred 777 / 524
  calls) except the bound's (24 / 19 forced bakes); `cut` happened 5 times in one launch. The deferred work ran after the
  move in the old slices: on the zoom-out, 6 idle bakes in the 2 moving seconds and 161 in the 3 still seconds after
  (fit 1, 17:11:08-12), against 103 during and 65 after (fit 0, 17:05:41-44).
- **What moved**: the moving frames' p90 (ready - publish 7.6-7.9 against 9.6-10.0 ms; frames over 6.06 ms 14-16 %
  against 20-27 %), the key pans' wake tail (max 8.6 against 19.5 ms), the long passes in the zoom-out (a pass of
  predraw plus an idle bake no longer passes 20 ms: 3 / 11 against 44 / 43), and the displayed interval's p99.9 and its
  count over 50 ms. **What got worse**: the still frames' wake (over 2 ms 7.3 against 3.5 %: the deferred bakes land
  there), and the wheel zoom-out's wake (a third of its ~99 moving frames over 10 ms against 2 %), which carries the
  moving p99.
- **The zoom-out's wake is not an idle bake** [L the numbers; I the mechanism]: in fit1-4's zoom second 17:11:09 the
  idle stage's mean was 0.26 ms a pass while the wake's mean was 9.66 ms and predraw's 6.1 ms (77 up-res bakes that
  second). The EXE publishes its next camera (at UpdateViews) before it waits for the server's frame, so a long pass
  (the near pairs' up-res in predraw, 11.8-15.4 ms maxima) holds the next camera's wake by itself. Shorter passes (no
  idle bake in them) let the EXE run more frames during the zoom, so more of them meet a long up-res pass; the n is
  small (about 98 frames an arm, two launches) and the zoom's speed differs between launches. 0689's "the idle bake
  holding the wake" (pan-coherence.md 5.8) is true for the key pans and the rest, not for the zoom-out's tail.
- The capped case is 8.5 (the laptop).

### 8.5 Laptop A/B, the 30 fps cap with vsync (one pair, grade B-C) [L: 2026-11-23 18:09-18:24 EST, laptop host clock]

After 0710 (capture jobs wake and hold the laptop's display; before it, 10 probes 16:46-17:17 were refused, rc 3).
Branch at `50f56d2a0` (main merged for 0710; `server/src` as handed back), SHIP `C:/c5-64bit-work/0703/ship2` (server.exe
`5e0850f5...`, proxy `0f459ede...`, composite `18fb9d8c...`); EXE 4390913 on the laptop, TB-HUGE_0350 t350, tb1, the
reporter's `[Renderer]` set, `lockstep_mode=exact`, the 30 fps cap with vsync, test-only `render_pass_long_ms=8`;
`capture_job.py --arm product --cases keys,drag,wide --repeats 1`, `tb.py --env C5_RENDER_BAKE_FIT=<arm>`
(`server/evidence/0703/laptop0703.sh`; fetch and layout `fetch0703.sh`, marks from capture.json `marks0703.py`; reports
`reports0703.sh`, `ab0703.py`). Jobs `20261123-180912-1f69` (fit 0) and `20261123-181645-6b71` (fit 1); fetched to
`C:/c5-64bit-work/0703/lap/`. The laptop ran nothing else (one job at a time).

| per server frame, ms | fit 0 | fit 1 |
|---|---|---|
| moving frames | 646 | 619 |
| moving wake - publish p50 / p90 / p99 / max; over 5 / 10 ms | 0.04 / 1.48 / 15.7 / 33.9; 4.5 / 2.3 % | 0.04 / 7.84 / 24.2 / 29.1; 10.8 / 9.1 % |
| ... key pans: p90 / p99; over 10 ms | 3.18 / 20.7; 3.8 % | 16.0 / 25.6; 16.4 % |
| ... drags: max | 0.14 | 0.31 |
| ... wheel zoom-out: p90 / p99 | 3.01 / 17.3 | 3.93 / 15.9 |
| moving ready - publish p50 / p90 / p99; over 33.3 ms | 20.7 / 31.3 / 47.2; 7.3 % | 21.0 / 33.4 / 51.6; 10.0 % |
| moving present interval p50 / p90 / p99 | 33.4 / 42.7 / 66.3 | 33.4 / 44.0 / 59.9 |
| still wake - publish p90 / p99 | 2.17 / 15.6 | 4.75 / 15.9 |
| F2 (displayed interval) p99 / p99.9; over 50 ms | 69.5 / 223; 235 | 71.2 / 124; 237 |

- **No window here either.** While moving, the gap estimates were 0.14-3.0 ms for most of the run (the laptop's pass
  is about 20 ms and the EXE's own work about 14 ms, 5.9: the next camera comes right after the pass), the bake
  estimates 3.4-12.9 ms; only in a few seconds (gap 11.9-23.6 ms) did bakes fit (59 admitted, 9 cut in the whole run,
  against 865 deferred calls and 28 forced bakes).
- **Mode 1 made the moving frames' wake worse on the laptop** (key pans: over 10 ms 16.4 against 3.8 %), with the
  moving ready - publish tail (p99 47.2 -> 51.6 ms, over 33.3 ms 7.3 -> 10.0 %) [L, one pair]. Mechanism [I]: the
  key series holds a key, then waits about 1 s; 300 ms after the last moving frame mode 1 runs the deferred backlog in
  the old slices (laptop bakes of 10-13 ms, `legacy` 13-37 calls in the moving-flagged seconds), and the next key's
  first frames meet those bakes. With mode 0 the same bakes are spread one per pass through the moves.
- **Decision taken in the code: the default is 0 (off)**; the mechanism stays for an A/B (`C5_RENDER_BAKE_FIT=1|2`).
  Under exact lockstep with the server as the slower side there is no idle time after a pass to fit a bake into: any
  idle bake costs its length in some frame, so only less bake work, or bake work off the render thread or that can stop in between (8.6),
  shortens frames.
- PERFORMANCE.md's interleaved series (6 runs per arm) was not run: one pair already showed mode 1 not better; the
  driver takes `ARMS="0 1 0 1 0 1"` if the MC wants grade A.

### 8.6 Candidates the numbers point at (not built) [I]

8.4 and 8.5 show that, under exact lockstep with the server's pass as the slower side, the render thread has no idle
time after a pass in camera moves: work can only be removed from it, done while the camera is still, or cut into
smaller pieces.
1. **The bake's CPU part on a worker thread** (`render/src/bake/pipeline.cpp`, `render/src/bakeresidency/`): of a
   typical 3-4 ms bake, prep (layer list, slope classes, blend vertices: 1.5 ms) and normals (0.6 ms) are CPU work on
   the render thread (`PipelineStepTimes`, 8.1); done ahead on a worker for the next tiles in ring order, the render
   thread keeps the draws and copies (blend 0.1, post 0.3, gpu timer 0.4 ms). Same bytes. Worth: about half of every
   idle and pre-draw BC1 bake, and of the near pairs' state-5 bakes, which run the same pipeline.
2. **Pre-bake the near pairs' content while the camera is still** (`render/src/bakeresidency/nearcache.cpp`,
   `residency.cpp`): the up-res keeps the EXE's per-frame rule (which tile becomes state 5 in which frame); the pair's
   texels for the tiles around the view, baked from the same inputs before a move, would let the moving frame only
   switch the tile to its pair. Same bytes per frame. Worth: the moving predraw tail, 7.8-15.4 ms per-second maxima on
   the workstation zoom-outs (12-94 up-res a second) and 3.17 ms per moving frame on the laptop against 1.93 still (5.9).
   Cost: pool entries held for tiles that may never be up-res'd (pools 256 x 256, 512 x 64), the guess of which tiles
   come next. At rest the frames cost the same under lockstep, but the picture does not change in them.
3. **A bake that can be split at its step boundaries** (`render/src/bake/pipeline.cpp`): the yield check between
   steps would bound one camera's wait by the longest step instead of the whole bake. It moves no work out of the
   frames, it only spreads it: worth for the first-use bakes (7.4-36.1 ms), which are single hitches.
4. **Run the near pairs' up-res after the ready, not before the draw** is NOT a candidate: the frame that switches a
   tile to its pair must have the pair's texels (same bytes), so the bake precedes that draw.

### 8.7 Not verified

PERFORMANCE.md's interleaved series (one pair on the laptop, two pairs on a loaded workstation); a quiet workstation;
a capped workstation run (pass about 4 ms against 33 ms: the one regime where gaps exist, 5.8's H1); the huge save on
the workstation; mode 2; the mechanism of 8.5's key-pan regression (the deferred backlog meeting the next key) is an
inference from the per-second lines, not traced per frame.

## 9. Prep ahead: the bake's CPU products made on a worker (ticket 0712, 2026-11-23) [C: code; L: as marked]

Written 2026-11-23 by the 0712 implementer (account B, branch `task-0712-prep-ahead`), from B1 of the Architect's
frame-jobs note (`research/07-architecture-decisions/architect-2026-11-23-frame-jobs.md` section 6) and 8.6's candidate 1.

### 9.1 The design [C]

- **The product** (`c5r::bake::PrepProduct`, `render/include/c5r/bake/pipeline.h`): what a bake computes on the CPU from
  the map's inputs and the tile alone: the layer list, the tile's window, the slope classes, the blend vertices (48 B
  each, 67 x 67) and indices, the grid normals (67 x 67 x 3 floats). About 0.33 MB a product [I: vector sizes; the
  measured figure goes in 9.3]. Not in it: the height page (a GPU texture; 0.0x ms when kept), the own constant block
  (needs the page's offsets; ownMs small in 8.1's lines), the style (per request), needSecond (per request:
  areaChanged and the override), the decals (post's input).
- **One code path**: `prepCpu(grid, tables, tile, normals source, slopes source, out)` runs buildLayerList, tileWindow,
  the slope classes, fillBlendVertices, buildBlendIndices and the normals in the order the bake ran them before 0712; the
  bake computes inline through the same call. So a product is the inline value for the same inputs by construction;
  the tests compare them byte for byte anyway (9.2). The grid normals now come before the blend pass inside the bake
  (they were after it); they are CPU-only, so the order changes no output, only where `normalsMs` falls in the step list.
- **The worker** (`PrepAhead::Thread`, `PipelineConfig::prepAhead`): one thread of the baker, started at the first
  `prepRequest`, with the render thread's MXCSR (the frame-jobs note's R7). Two lanes (0: the near pairs' next up-res,
  1: the visible tiles beyond the burst and the idle order); each request replaces its lane's queued jobs; lane 0 first.
  It reads the grid and tables in place and calls only the pipeline's own sources (`makeExeGridNormals`,
  `makeExeSlopeClasses`: stateless); with a caller's normal or slope source prep ahead is off (`prepOn` false).
- **The cache**: keyed by tile, content generation and the map's epoch; bounded (`prepCacheProducts`, 32), the least
  recently made or used evicted first. **The bake's side** (`prepTake` in `bakeBody`): a cached product of the request's
  tile and generation (hit); a product in flight waited for, at most the running mean of the inline prep (0.5-20 ms
  bound; a timeout computes inline); a queued job of that key is taken off the queue and computed inline (claimed);
  else inline (miss). `PipelineStepTimes::prepAhead` names which (0 off, 1 hit, 2 waited, 3 inline).
- **Invalidation (the writers' join)**: `prepInvalidate` drops both queues, bumps the epoch, waits for a job in flight
  (no timeout, R3) and empties the cache; afterwards the worker reads nothing until the next request. `setMap`,
  `heightsChanged`, `warm` and `init` call it; the server's writers of the grid call `ResidencySource::prepJoin` before
  they write: `Scene::liveTerrainJoin` (before tapPoll's `m_grid = std::move(g)`), `Scene::liveTerrainFinish` (before
  the in-place `ApplyWindow` loop) and `Scene::loadField` (the offline players' field). The tables never change after
  init; the art types and styles are not prep inputs (per request). A missed writer would make a product stale under
  the same generation; `C5_RENDER_PREP_VERIFY=1` (TEST ONLY) checks every hit against the inline computation, logs a
  LOUD line and uses the inline products.
- **The hints** (`Residency::nextBakes`, `render/src/bakeresidency/residency.cpp`): `NearFrameResult::next` (the sorted
  list's next `kNearNext` = 4 tiles after the frame's up-res candidates, by the candidate rule: the next frames' up-res
  if the camera goes on), the visible tiles still needing a bake after beforeDraw, and idle()'s order from its cursor
  (the stale pass first with staleFirst), at most 6, each with the tile's generation now. Read only. The source asks
  after each live beforeDraw (both lanes) and after each idle() (lane 1); `settle()` asks nothing.
- **Switch**: `C5_RENDER_PREP_AHEAD` (0 default, 1 on; `ResidencySourceConfig::prepAhead` overrides; a bad value: LOUD,
  0). The stand-in baker has no pipeline: off with a line. Init line `residency: prep ahead: mode N (...)`.
- **Step timing per bake kind** (the ticket's first item): `pipeline steps 1s: prep_ahead=M | <kind> n= total=
  total_max= prep= prep_max= normals= page= own= tex= blend= style= post= copy= gpu= wait= hit= waited= miss=
  first_use= | ... | prep tot requested= replaced= made= failed= discarded= hits= waits= timeouts= claimed= misses=
  evicted_unused= invalidations= join_ms= join_max_ms= worker_ms= cache= cache_kb= cache_kb_max= verified= mismatches=`
  in render.log once a second with bakes; kinds predraw (beforeDraw's BC1 bakes), idle, held (0424), near (the state-5
  up-res), settle. Means per bake in ms; the prep totals are cumulative.

### 9.2 Tests [C: written; results in 9.3 once run]

`render/tests/bake/bake_test.cpp`: `prep_cpu_is_the_bakes_cpu_steps` (every tile of a wrapping 6 x 5 field, EXE and
stand-in sources, against the steps one by one; negatives), `prep_cpu_on_threads_equals_serial` (4 threads),
`game_prep_ahead_bakes_bit_for_bit_what_inline_bakes` (random tiles, the seam columns, generations 1-3, t8 made and not,
a miss, a generation mismatch, a batch, an edit between request and use with the join, a forgotten join caught by the
verify check, setMap), `game_prep_ahead_negatives_lanes_and_bounds`. `render/tests/bakeresidency/residency_test.cpp`:
`near_next_names_the_next_frames_upres`, `next_bakes_follow_the_idle_order_and_the_visible_set`.
`server/tests/render/test_prep_ahead.py` (in run_all's render-host suite): mode 1 with verify on the Tiny tap, the
default off, a bad value, the stand-in.

Results [L, 2026-11-23/24, workstation, build of `1bd5fb47b` + the thread test's fix]: the whole render ctest passes
(0 skipped), ASan bake + residency ctest 0 reports; `prep_cpu_is_the_bakes_cpu_steps` 30 tiles equal; the game test's 18
cases bit for bit from the cache (t8 made 9, not 9); the forgotten join caught by verify (1 mismatch, inline used); a
product is **315 KB** (the cache of 32: about 10 MB, `cache_kb_max=10191` live). run_all render-host 209 passed (the 4
of `test_prep_ahead.py` among them), scenebundle-programs 4, scenebundle-py 28. Rule A (offline-equals-live,
2026-11-24 03:42-04:09): 17 of 17 bundles in both modes (prep ahead 0, and 1 with verify); a hand replay of the huge
bundle in mode 1 + verify: 8 of 8 frames colour and depth EQUAL, no LOUD line. The players bake mostly in `settle()`,
which requests nothing, so rule A exercises the worker only in the frames' own beforeDraw [C].

### 9.3 Step timing per bake kind, and the series (12 runs, 6 per arm, interleaved; grade B) [L: oldpc, 2026-11-24 00:25-02:13 EST]

The laptop was down from 2026-11-23 23:25 (ssh timeout); MC-B moved the series to oldpc (`laptop0712.sh` with
`HOST=oldpc TBQ=--quiet`; `capture_job.py --arm product --cases keys,drag,wide`, TB-HUGE_0350 t350, the reporter's
`[Renderer]` set, lockstep exact, `render_pass_long_ms=8`; server.exe `4c4c3d82...`, proxy and composite of 0703's
ship2, the same for both arms). Every job's quiet record says `undisturbed=False` (foreign cores 0.13-0.19: another
ticket's headed job or a game outside the testbed). capture.json's mark times on oldpc are the job's perf_counter,
not QPC (marks at 178 s against the CSV's 599,320 s), so split0689 runs without marks there (`fetch0712.sh`).

Order of the runs: 0 1 1 0 0 1 1 0 0 1 1 0 (jobs 20261124-002539-1822 .. 20261124-020547-5154; per-run table and the
reader's output: `server/evidence/0712/oldpc_ab0712.txt`, `ab0712.py`). Per arm, 6 runs: mean (sd) [95 % interval];
B - A with Welch's 95 % interval and the smallest detectable difference (2.8 SE).

| oldpc, moving seconds (split0689 without marks) | p0 (off) | p1 (prep ahead) | p1 - p0 |
|---|---|---|---|
| predraw ms per drawn frame | 1.177 (0.080) [1.093, 1.261] | 1.071 (0.021) [1.049, 1.093] | -0.106 [-0.193, -0.019], detectable 0.095 |
| **predraw, p90 of the per-second mean** | 2.128 (0.272) [1.842, 2.413] | 1.707 (0.222) [1.475, 1.940] | **-0.420 [-0.744, -0.097]**, detectable 0.401 |
| predraw, p90 of the per-second max | 5.94 (0.84) | 5.74 (0.55) | -0.20 [-1.14, 0.75], not detected |
| pass busy per drawn frame | 8.52 (0.55) | 7.93 (0.38) | -0.59 [-1.22, 0.04] |
| ready - publish p90 (CSV) | 10.57 (0.69) | 10.09 (0.32) | -0.48 [-1.21, 0.26] |
| near (up-res) bake, mean ms | 2.681 (0.093) | 1.462 (0.051) | **-1.218 [-1.321, -1.116]** |
| ... of it prep + normals (+ wait) | 1.880 | 0.686 | -1.193 [-1.250, -1.136] |
| idle bake, mean ms | 2.371 (0.084) | 0.885 (0.062) | **-1.486 [-1.583, -1.389]** |
| ... of it prep + normals (+ wait) | 1.763 | 0.280 | -1.484 [-1.528, -1.439] |
| bakes per run: hits / inline (all kinds); products made; cache max | - | 7,121 / 364; 9,257; 10,191 KB | |

- **Step timing per kind, prep ahead off** (p0 runs): an idle bake 2.3-2.4 ms, of it prep 1.5 and normals 0.4; a near
  up-res 2.6-2.7 ms, of it prep 1.4-1.8 and normals 0.35-0.5; page, own, textures under 0.1 after the load; post 0.3-0.5,
  the GPU timer 0.2-0.3. No BC1 bake before the draw in the moving seconds (the predraw stage's bakes are the near
  pairs' up-res, as 8.1 found on the workstation).
- **Mode 1**: hits 96 % of all bakes (p1-2: 6,937 hits, 1,181 waits, 347 inline), the near kind 62-66 % (p1-2: 165
  hits, 4 waits, 98 inline; p1-3: 212, 0, 109: the up-res tiles the hints did not name), idle bakes -1.49 ms, near
  bakes -1.22 ms: about what prep and normals cost (the GPU steps stay). Byte identity live: no `LOUD` line in any p1
  run's render.log (verify was off in the series; the rig test runs it). 953 products evicted unused in a run (the
  idle lane names 6 tiles after every slice, more than is baked).
- **Why the predraw moved less than the bake** [I]: under the moving camera the predraw stage holds at most one up-res a
  frame (the EXE's rule, 8.1), and a third of them are not hinted; 1.2 ms off a 2.7-ms bake on about 2 of 3 up-res
  frames gives the measured -0.42 ms at the p90 of the per-second mean. The per-second max moves less (its frames are
  the unhinted ones and the first uses).
- **Against the ticket's bar** (moving predraw p90 down 1.5 ms or more, set for the laptop): on oldpc the base
  predraw p90 is 2.1 ms in all, so 1.5 ms is out of reach there; the laptop arm is 9.4. Grade B: every oldpc job
  `undisturbed=False` (foreign cores 0.13-0.19).
### 9.4 The laptop arm (12 runs, 6 per arm, interleaved) [L: laptop, 2026-11-24 03:02-04:21 EST]

The laptop came back at 02:58; the same driver with `HOST=laptop` (marks work there: capture.json's clock is QPC
seconds on that host), same SHIP, order 0 1 1 0 0 1 1 0 0 1 1 0, jobs 20261124-030214-591f .. 20261124-041434-2cbb, all
rc 0, one job at a time (not quiet jobs; the laptop ran nothing else). 30 fps cap with vsync, as 0703's pair (8.5).
Table: `server/evidence/0712/laptop_ab0712.txt`.

| laptop, moving | p0 (off) | p1 (prep ahead) | p1 - p0 (Welch 95 %) |
|---|---|---|---|
| predraw ms per drawn frame (pass 1s) | 3.355 (0.079) | 3.779 (0.233) | **+0.424 [+0.178, +0.670]** |
| predraw, p90 of the per-second mean | 6.81 (0.96) | 7.16 (1.15) | +0.35 [-1.03, +1.73], not detected (1.7) |
| predraw, p90 of the per-second max | 25.3 (2.4) | 23.7 (1.9) | -1.5 [-4.4, +1.3], not detected |
| pass busy per drawn frame | 29.08 (1.05) | 27.04 (0.67) | **-2.03 [-3.20, -0.86]** |
| ready - publish p90 per frame (CSV, the marks' moving frames) | 44.24 (1.55) | 42.21 (0.78) | **-2.03 [-3.70, -0.35]** |
| ready - publish p50 (CSV) | 24.93 | 23.53 | about -1.4 [I: means of the per-run p50] |
| insertion - publish p50 (the EXE's own work, CSV) | 14.93 | 15.68 | about +0.75 [I: means of the per-run p50] |
| present interval p90 (CSV) | 62.6 | 64.0 | about +1.4 [I], within the runs' spread (60.4-67.8) |
| near (up-res) bake, mean ms | 5.79 (0.36) | 3.70 (0.25) | **-2.08 [-2.50, -1.67]** |
| idle bake, mean ms | 6.33 (0.31) | 2.76 (0.17) | **-3.57 [-3.91, -3.22]** |
| server frames per moving second (pass 1s) | 32.8-35.6 | 26.9-29.4 | fewer redraws [I] |

- **The bakes got as much faster as their CPU products cost, and more** (idle -3.6 ms against prep + normals of 3.5):
  near up-res hits 67-69 % (p1-2: 176 hits, 1 wait, 78 inline), all kinds 4,447 hits + 3,542 waits + 336 inline in
  p1-2; the worker's CPU 30 s a run (about 0.08 of a core).
- **The frame**: ready - publish p90 -2.0 ms and the pass's busy per drawn frame -2.0 ms are the gains that reach the
  frame. **The predraw stage per drawn frame rose 0.42 ms** [L], and the server drew fewer frames per moving second
  (27-29 against 33-36) while the CSV's moving presents were the same (536-562 a run in both arms): mode 0 draws more
  passes with the last camera (a re-baked visible tile asks for a redraw, `takeVisibleChanged`), so per-drawn-frame
  stage means are not like with like across the arms [I: not traced per pass]. The EXE's own work (insertion -
  publish) rose about 0.75 ms at the median [I: the worker beside the EXE's threads on 8 logical processors; not
  separated from run spread].
- **The ticket's bar is not met on either machine**: moving predraw p90 is not down by 1.5 ms (laptop +0.35, not
  detected; oldpc -0.42). The up-res bake's CPU part is gone where hinted, but the predraw stage of a moving frame is
  more than that bake (one up-res at most a frame, a third unhinted) and the rest moved with the load. The default
  stays 0 (off); the switch stays for the MC's decision.
- **Next candidates** [I]: name more up-res tiles (more than kNearNext = 4, or the tiles whose wanted size changes);
  keep the inline products in the cache too (a tile's BC1 bake and its up-res share the product); fewer idle hints.

### 9.5 Beside 0714's height pages at the field (re-merge, 2026-11-24) [C: merged source; L: tests as marked]

Main's 0714 (terrain-bake-slow-first-use.md 9: `PipelineBaker::warmPages`, `C5_RENDER_PAGE_WARM`, default 1) landed in
the same files as this section's change (`pipeline.{h,cpp}`, `srv_residency_source.{h,cpp}`); git merged them without
a conflict, and the two do not share state [C]: the prep worker reads the grid, the layer tables and its own normal and
slope sources (`prepCpu`), never the height pages; `warmPages` builds height pages on the render thread inside
`onField`, after `setMap`, through the same `pages()` source the bakes use. The only hand-resolved conflicts were the
tests' tail of `render/tests/bake/bake_test.cpp` (both blocks kept, 0712's first) and the section README.
Results on the merged head are in `server/evidence/0712/acceptance.txt` item 6.
