# Live terrain: a tile turned into a mountain is drawn only partly, and not at all on a river (ticket 0717)

Written by the 0717 implementer (account B, branch `task-0717-live-mountain-partial`) from 2026-11-23 21:15 EST. This
file extends `live-terrain.md` (epic 0465 and 0615), which was outside the ticket's files: it was being changed by ticket
0631 at the same time. Section numbers there are cited as `live-terrain.md` N.N. Marks: `[C]` checked by this ticket
in the code or by an offline run on recorded inputs named here; `[L]` a live run of this ticket; `[I]` inferred.

## 1. The report

The owner's beta 3 play, 2026-11-23 evening (build 0.1b3-std-only, layout x64-1b87dd18-pd56fbd9, main d56fbd94f; the
owner's own Steam game; `[Performance]` AiDrainUnordered 0, AiSkipVisualDrain 0, AiUpdateTickMs 0,
GameEventsOneCrossing 0, HiddenEmbarks 1): tiles turned into mountains with IGE near rivers did not change at all, and
elsewhere often showed half a mountain and half the earlier tile, not covering most of the hex. Screenshot kept outside
git (D83): `C:/c5-64bit-work/reports/owner-20261123-beta3/ige-mountain-half.png`, SHA-256
8e3eee2dc02061ea93fcb7e50629156fc986c0b9f12056d4a55c909685998a20. Stock shows a flat-to-mountain change only after a
reload (`live-terrain.md` 2.1, 2.3), so the live mountain is the patch's own (epic 0615).

## 2. The cause [C]

The region producer (`render/src/liveterrain/producer.cpp`, `Producer::change`) redraws a changed plot with its own
blend of the new pieces, but keeps from the current field every sample it cannot reproduce, and feathers the new ground
to zero next to them. For a change that keeps every plot's land or water class (flat to hills or mountain, a natural
wonder added), two keeps cut into the changed hex (as built by ticket 4908, `live-terrain.md` 8.9):

1. **The river keep.** Every sample within 60 samples of the centre of any plot whose `riverEdges` is non-zero is kept,
   the changed plot's own included (only a land/water change exempts the changed hex, w7-ridge). A hex reaches about 37
   samples from its centre, so a changed plot with a river on any edge keeps its whole hex: **nothing changes**. A
   neighbour with a river keeps a disc of radius 60 around its own centre, 64 samples away: the disc reaches to within 4
   samples of the changed plot's centre, and the 32-sample feather beside it takes most of the rest: **half a mountain**.
   server.exe makes this worse: the feed carries only "a river on some edge", and `PlotTableFromFeed`
   (`server/src/render/srv_liveterrain.cpp`) gives every such plot all three edge bits (0x7), so every plot touching a
   river is a river-edge plot (ticket 0631 gives the real bits).
2. **The water keep.** Without the land/water step's region fill, the samples of coast and ocean hexes and every
   material-37 sample are kept, with the same 32-sample feather on the land side. A changed hex beside the coast is
   feathered across its whole width towards the water (the hex's inradius is 32 samples).

The river keep stood in for the river carve (`live-terrain.md` 7.4 step 7, 8.15 item 8: the EXE carves the river beds
after the coast strips; not ported). Its 60-sample radius was a first choice, marked `[I]` and "not measured"
(`live-terrain.md` 8.11).

### 2.1 Reproduced offline [C, 2026-11-23 21:26-21:45 EST, workstation, an indication for times]

`server/evidence/0717/cases.py` on 4902's recorded Tiny inputs (save `C5H-0301-4901-LT_0008`, Tiny 56 x 36, turn 9;
the prepared inputs of the kept folder `C:/c5-64bit-work/keep/liveterrain-derived-4903/tiny`, plot table with the
DLL's own river bits; map seed 0x3DFC9816; piece library of c5h-c's install, SHA-256 86faac1e...ce4c1c7a; coast styles
from that install's `TerrainModels.fpk`). Six flat land plots per class (seed 717), each turned into a mountain on a
fresh producer of the load, land/water on as in the product. The weight is the share of the new ground each sample of
the changed plot's own hex takes (1 = the new mountain, 0 = the old ground); "raised > 1000" is the share of the hex
whose height rose by more than 1000 u16 (about 0.8 world units).

| class | river bits | mean weight in the hex | hex at weight 1 | hex kept | hex raised > 1000 |
|---|---|---|---|---|---|
| the plot has a river on an edge | as server.exe gives them (0x7) | 0.00 | 0.00 | 1.00 | 0.00 |
| the plot has a river on an edge | the DLL's own | 0.04 | 0.01 | 0.91 | 0.08 |
| a neighbour has a river, no water within 1 | 0x7 | 0.25 | 0.03 | 0.48 | 0.48 |
| a neighbour has a river, no water within 1 | the DLL's own | 0.39 | 0.19 | 0.38 | 0.58 |
| a water neighbour, no river within 1 | either | 0.63 | 0.31 | 0.05 | 0.89 |
| no river within 1, no water within 2 | either | 0.95 | 0.90 | 0.01 | 0.98 |

The first rows are the owner's [reworded for privacy] report that nothing changes near rivers, the next ones the report of a half mountain.

## 3. How far the EXE's river step reaches [C, offline, 2026-11-23 21:30-21:36 EST]

`server/evidence/0717/carve_reach.py`: the producer's own whole-map blend (no river carve) with several seeds; where they
agree within 1 (the piece choice does not matter, D194) the blend stands for the EXE's blend before the carve, and the
tap minus it is the river step's work. Land samples away from water, marsh and natural wonders, binned by their
distance in world units (sample columns 1.0, rows 0.866) to the nearest real river edge (the DLL's bits: W of river =
a river on the plot's east edge, NW of river = south-east edge, NE of river = south-west edge; `CvPlot::IsRiverSide`
[C, the DLL fork]).

| distance to the river edge | Tiny: tap below the blend by > 1 | Tiny: median lowering (u16) | huge: below by > 1 | huge: median lowering |
|---|---|---|---|---|
| 0-2 | 61.7 % | 1,032 | 68.1 % | 459 |
| 4-6 | 49.7 % | 953 | 63.7 % | 103 |
| 8-10 | 37.0 % | 110 | 49.0 % | 38 |
| 12-14 | 25.2 % | 54 | 33.2 % | 20 |
| 16-20 | 10.3 % | 13 | 14.6 % | 15 |
| 20-24 | 2.6 % | 4 | 5.3 % | 20 |
| 24-28 | 0.2 % | 2 | 2.8 % | 59 |
| 32-64 | 0.0-0.1 % | - | 2.7-3.1 % | - |

(Tiny: 447,327 samples, five seeds; huge: 3,583,620 samples, three seeds, so its mask is weaker and its background is
the 2.7 % of the residual named in `live-terrain.md` 7.7 and 8.3.) Material 37 lies on 37 % of the samples at 0-2 and
none beyond 16 (the river's water material, `live-terrain.md` 8.10).

- **The carve reaches about 24 world units from the river edge, not 60 samples from a plot's centre.** Of the samples
  within 40 of an IsRiver plot's centre, only 30 % lie within 16 of a real river edge.
- Inputs: the Tiny tap `tiny_t0008_field.npz` e3023bfa...f2fdfda04c and huge `huge_field.npz` 0d2433e4...3a45cb05a
  (`F:/c5-64bit-work/0465/4902/`), seeds 00000001, 12345678, CAFEBABE, 0BADF00D, 7FFFFFFF (huge the first three).

## 4. The fix (as built)

In `Producer::change` (`render/src/liveterrain/producer.cpp`, constants and options in `producer.h`):

1. **The river band in place of the disc.** For each river edge (a segment between two hex corners) a band: within
   `kRiverCoreWorld` (2 world units, from the reload's own profile in 7.1; *corrected 2026-11-23 23:4x, was 6, chosen
   where the median lowering on flat land is about 1,000 u16*) the current ground is kept,
   and from there to `kRiverReachWorld` (24, section 3) the new ground rises in linearly. The band scales a raise only:
   a lowering is drawn as the change gives it, because the carve only lowers ("never above the old height",
   `community-lake-victoria-thick-river.md` 5). Materials take the new value where the scaled weight is at least 0.5,
   so the river's material-37 line stays. The band does not seed the 32-sample feather and is not applied on water
   hexes. **A land/water change keeps the 4908 disc** with its w7-ridge exemptions: that rule was validated by epic
   0615 (I3, the owner's known difference 1) and its keep is ticket 0631's; the band is only for the changes that keep
   every plot's class.
2. **Edges from the feed's flag.** `Options::riverEdgesFromAnyFlag`: the plot table's river bits are the feed's
   any-edge flag; an edge counts when both plots beside it carry the flag (every real river edge does; an edge between
   a river plot and a plot with no river does not). *Corrected 2026-11-24 (0717 finishing, after 0631 merged at
   105f4708d): server.exe passes it only for a table with records that carried no edges (`PlotTableIn::oldRuleRecords`
   > 0, a journal recorded before 0631); the live server's tables carry the DLL's own bits and the band lies on the
   plots' real river edges. Was: "server.exe passes it until ticket 0631 gives the DLL's own bits".*
3. **Land changes through the land/water step.** With land/water on (`enableLandWater`, the product), a change that
   keeps every plot's land or water class (`Options::landRegion`) is drawn as a land/water change whose coast lines did
   not change: the region bytes of the current lines, the blend that gives land pieces only land samples, and the coast
   strips of the load's meshes. The water keep and its feather are not used; `ChangeResult::landRegion` says so and
   `landWater` stays false (no coast moved: no `Waves_Reload`, no ice reload).
4. `Options::riverKeepDisc` restores the 4908 disc; with `landRegion` off it is the producer before 0717 (the runner's
   `--river-disc 1 --land-region 0`; with those options the cases above come out identical, [C]).

### 4.1 Results offline [C, 2026-11-23 21:43-21:50 EST, the cases of 2.1, the same inputs]

`cases.py --rule new` (the runner's defaults, `--river-any-flag 1` for the server-style table, as server.exe passes
it) against `--rule old` (`--river-disc 1 --land-region 0`, which reproduces 2.1's numbers exactly). "Water-region
samples changed" counts window samples whose region byte is water and whose height moved by more than 1 against the
tap (the old rule keeps them by construction).

| class | river bits | mean weight, old -> new | hex raised > 1000, old -> new | mean rise in the hex (u16), old -> new | water-region samples changed > 1 (new) |
|---|---|---|---|---|---|
| the plot has a river on an edge | 0x7 (server) | 0.00 -> 0.45 | 0.00 -> 0.70 | 0 -> 10,026 | 0 of 52,051 |
| the plot has a river on an edge | the DLL's own | 0.04 -> 0.62 | 0.08 -> 0.80 | 848 -> 12,512 | 0 of 52,051 |
| a river neighbour | 0x7 (server) | 0.25 -> 0.91 | 0.48 -> 0.97 | 4,163 -> 16,359 | 0 of 87,794 |
| a river neighbour | the DLL's own | 0.39 -> 0.98 | 0.58 -> 0.99 | 6,554 -> 17,395 | 0 of 87,794 |
| a water neighbour | either | 0.63 -> 0.93 | 0.89 -> 0.91 | 12,812 -> 16,080 | 2,342 of 223,741 (max 1,074; one case, (36,10)) |
| inland | either | 0.95 -> 0.95 | 0.98 -> 0.98 | 18,323 -> 18,359 | 0 of 167,926 |

- A plot with a river now rises as a mountain except along its river edges (the band's core and rise), as a reload
  carves it [I: the reload's own picture is the live check of section 7]. With the server's flag the band also lies on
  edges between two river plots that carry no river; ticket 0631's real bits remove that (the "DLL's own" rows).
- Beside the coast the weight is below 1 near the water because the strips (the beach) draw there, as in a reload.
- The one case with water-region changes, (36, 10): the re-match replaced a coast piece next to the changed plot (the
  producer's own seed, way 3, `live-terrain.md` 8.3), up to 1,074 u16 (about 0.8 world units) under the water.

## 5. Rule A and the bundles [C]

- None of the 16 scene-library bundles (`C:/c5-64bit-work/scene-library/bundles`) holds a live-terrain job: 14 carry a
  `render_live_terrain_journal` with 0 jobs and 0 generations, 2 carry none (`server/evidence/0717/journal_jobs.py`,
  2026-11-23). The producer's `create` is unchanged, so their replay cannot move.
- Bundles recorded with a land-only live change before this ticket (evidence of 4906, 4912, I3; not library members)
  would replay with the new rule and refuse on their generation hash. The MC's decision (#12300, 2026-11-23 21:5x): a
  journal mark that selects the old rule (`riverKeepDisc` on, `landRegion` off) for journals without it, written in
  `srv_liveterrain.cpp` (the journal's trailers) after ticket 0631 merges there; no gap note. Built 2026-11-24: section 8.
- The rule for class-keeping changes moved; land/water changes keep the 4908 disc exactly, so 4912's and I3's land/water
  numbers in `live-terrain.md` 8.17 and 11 stay valid.

## 6. Not verified / open

- **Both items closed 2026-11-24 by the finishing implementer** (branch `task-0717-finish` = this branch + main
  105f4708d): (2) the journal mark, section 8; (1) rule A 15:20-16:23 `[L]` (`allb_0717.sh 3`: run_all render-host
  213 passed, scenebundle-programs 4, scenebundle-py 30, offline-equals-live 17 of 17 on the C: library copy, no
  0xC0000142), and every builder again after the main merge and the mark (phases 1, 2, 5 plain; 6 and 7 under MSVC
  ASan: render ctest -R liveterrain 4 of 4 with the canary caught, test_liveterrain + icewaves 6 passed, run_all --asan
  render-host 213 passed, scenebundle-programs 4 of 4 with `sceneshot_test` run alone at `--timeout 3600`, 284 checks,
  after it hit the 1,500 s bound in the suite as runbook 2.3 says it does); phase 4 in the issue's log. The text below
  is the state at the first hand-back.
- **Open at the hand-back (2026-11-24 01:2x, MC-B #12503 answer):** (1) the rule A run (`bash server/evidence/0717/allb_0717.sh
  3`, run_all render-host + scenebundle-programs + scenebundle-py + offline-equals-live, about 60 min, in MC-B's rule A
  order); (2) the journal mark of section 5 once 0631 is on main (srv_liveterrain.h joins the files then, MC #12434
  answer). All builders other than rule A passed (allb phases 1, 2 and 5, 2026-11-24 00:07-00:22; phase 2's first
  liveterrain build failed on the test stub of the producer, which then took `Options`). Phase 4 (run_all's scene suites)
  was not run.
- The live check against a reload (the ticket's IGE run): section 7. It ran with core 6; core 2 is checked offline only.
- The core width 6 is a choice from section 3's medians; the carve's exact cross-section (a sine across the river,
  `community-lake-victoria-thick-river.md` 5) is not ported.
- With the server's any-edge flag the band also lies on edges between two river plots that carry no river (ticket 0631
  gives the DLL's own bits; it then drops `riverEdgesFromAnyFlag` in `srv_liveterrain_c5r.cpp`). *Done 2026-11-24
  (section 8): the flag is now set only for tables recorded before 0631.*

## 7. The live check (runbook) [I until run]

Product route, Tiny `c5h-9029-tiny-t0008` (SHA-256 b2ceb663...c29163, the save of 4902's inputs), a build-26577625 pool
instance (the owner's EXE build), server.exe, proxy and composite of this branch on the 0.1b pins (the owner's beta 3
DLL pin x64-1b87dd185), laid by `lay.py` with the installer's config.ini form. Four plots, each flat land turned into a
mountain by the test mod's `set --pt PLOT_MOUNTAIN` (CvPlot::setPlotType through Lua, as IGE does), chosen by
`server/evidence/0717/pick_live.py` inside the area `setup` makes visible: A (51,15) a river on its own edge, B (46,14)
a river neighbour, C (49,10) a coast neighbour, D (51,18) inland. Process 1 makes the four changes live (a capture
before and 2 s after each: the EXE's field, which holds the server's live windows (8.13), and a picture with the camera
on the plot); its save is reloaded in process 2, the reference.

```
bash server/evidence/0717/live_0717.sh build            # ~15 min, gated
bash server/evidence/0717/live_0717.sh dry
bash server/evidence/0717/live_0717.sh check <inst>     # after the claim by START-REQUEST to MC-B
bash server/evidence/0717/live_0717.sh prep <inst>
bash server/evidence/0717/live_0717.sh run <inst>       # three loads, about 25 min
bash server/evidence/0717/live_0717.sh end <inst>
bash server/evidence/0717/live_0717.sh compare          # regioncmp live vs reload per plot (inside, ring 1, ring 2)
```

### 7.1 Results [L, 2026-11-23 23:16-23:31 EST, workstation, c5h-new-a, EXE build 26577625]

Layout of branch `task-0717-live-mountain-partial` @2e33c8cd2 with the band's first core (6 world units): server.exe
2c6358e5..., proxy 6e9f97c9..., composite 24732b48..., x64 DLL b84aa3e9... (the 0.1b pins); laid by `lay.py` (backup by
SHA-256; restore ok, `provision.py verify --quick` rc 0, saves put back by hash). Save copy `C5H-0615-4912-K17PL_0008` of
b2ceb663... Captures and bundles on `F:/c5-64bit-work/0717/live1/` (outside git). No dialog box, no LOUD line.

- **All four changes went live** (server.log, load seq 3, changes 1-4): windows of 65,536-112,640 samples, producer
  127-158 ms, applied in about 20 ms on the render thread.
- **The hex fills, live as after the reload.** Share of the changed plot's own hex raised by more than 1000 u16 against
  the state before, and its mean height (`live_0717.py compare`, the `fill` block):

  | plot | class | raised > 1000: live / reload | raised > 10000: live / reload | mean height: live / reload |
  |---|---|---|---|---|
  | A (51,15) | a river on its own east edge | 0.71 / 0.95 | 0.44 / 0.69 | 26,776 / 32,199 |
  | B (46,14) | a river neighbour | 0.81 / 0.83 | 0.56 / 0.32 | 33,049 / 28,381 |
  | C (49,10) | a coast neighbour | 0.86 / 0.93 | 0.59 / 0.71 | 29,768 / 31,758 |
  | D (51,18) | inland | 1.00 / 1.00 | 0.86 / 0.86 | 35,660 / 35,680 |

  Before the fix the offline equivalents of A and B were 0.00 and 0.48 (2.1). Sample by sample live and reload still
  differ inside every hex (3,994-4,095 of 4,096 samples above tol 1, `compare.json`): the reload re-rolls the pieces
  (`live-terrain.md` 2.3, 8.5) and the producer uses its own seed (way 3); D, the inland case, has the same mean height
  live and reloaded to within 20 u16.
- **Pictures** (Visual Checks issue 0717, DIAGNOSTIC, D112: the product path laid by `lay.py`, 1080p-scale harness
  shots, camera on the plot): A before / live / reload `01_A_51_15_{before,live,reload}.png`, and the same for B, C, D.
  A live shows a whole mountain on the plot with the river along its east side, as the reload does; the owner's [reworded for privacy] reports of no change near a river and of a half mountain do not occur.
- **The band against the reload's own profile** (`band_profile.py`, plot A, the mean rise by distance to the real river
  edge, u16): reload 764 / 2,186 / 5,326 / 10,293 / 15,310 / 19,747 / 23,167 at 0-3 / 3-6 / 6-10 / 10-14 / 14-18 / 18-24
  / 24-32, plateau 22,746 beyond; live with core 6: 0 / 0 / 1,080 / 4,210 / 8,344 / 13,831 / 17,727, plateau 16,446.
  As fractions of each plateau the reload rises as `(d - 2) / 22` (0.03, 0.10, 0.23, 0.45, 0.67, 0.87, 1.0 against
  0.0, 0.11, 0.27, 0.45, 0.64, 0.86, 1.0), so **the core was set to 2** (from 6) after this run; the reach 24 stays. The
  lower live plateau is the producer's own pieces, not the band. The run with core 2 is offline only (7.2); a second
  live run was not made.

### 7.2 Offline with core 2 [C, 2026-11-23 23:4x EST, the cases and inputs of 2.1]

`cases.py --rule new` with the build of core 2 (`C:/c5-64bit-work/0717/new2_tiny`): hex raised > 1000 u16 on a plot
with a river 0.70 -> 0.80 (server-style bits) and 0.80 -> 0.86 (the DLL's own bits) against core 6; a river neighbour
0.97 -> 0.99 (server) and 0.99 (own bits, unchanged); coast 0.91 and inland 0.98 unchanged. Plot A live at core 6 had
0.71, the reload 0.95: with core 2 the offline share for that class moves by +0.10; the rest of the gap is the
server's any-edge flag (bands on edges between two river plots that carry no river; ticket 0631) and the producer's own
pieces. The producer suite passes (4 of 4 suites, 0 skipped) with the core-test sample moved to 1 world unit from the
edge.

## 8. The journal mark (rule A) [C, 2026-11-24, branch `task-0717-finish`]

Built by the 0717 finishing implementer (account B) after ticket 0631 merged (main 105f4708d), as the MC decided
(#12300, #12435).

- **Form.** An 8-byte trailer `"RBND"`, u32 1 (`kJournalRiverBandTag` 0x444E4252) right after the journal's feature
  names, before the map seed's (`"SEED"`, `live-terrain.md` 6.9) and the land/water mark's (`"LWAT"`). It is written
  only when `Journal::riverBand` is true, so a journal before 0717 is unchanged byte for byte. The seed's and the land/
  water mark's trailers stay the journal's last bytes, so a tool that finds them from the end
  (`render/evidence/0615/4912/scenes_4912.py`) reads new journals as before.
- **Who sets it.** The live scene always: `LoadIn::riverBand` is true by default and `LiveTerrain::onField` copies it
  into the journal. The player: `Replay::init` sets `LoadIn::riverBand` from the decoded journal.
- **What it selects** (`srv_liveterrain_c5r.cpp`, `C5rProducer::change`): with the mark, the band and the land region
  (section 4); without it, `riverKeepDisc = true` and `landRegion = false`, the producer before 0717 bit for bit
  (section 4 item 4). Independent of the mark, `riverEdgesFromAnyFlag` is on only for a table whose records carried no
  edges (before 0631); it does nothing with the disc.
- **Decoder.** After the names: if at least 8 bytes remain and the next tag is `RBND`, its version must be 1 (another
  version is refused, "river band mark has version N"); any other tag is left for the seed's trailer, whose rules are
  unchanged (a second `RBND`, or a cut one, is refused as not the seed's trailer). The bundle checker
  (`CheckLiveTerrainConfig`) takes both forms.
- **Test.** `test_liveterrain river_band_mark` (on the producer stub, whose `Options` gained `riverKeepDisc` and
  `landRegion`): the live change's options, the bytes with and without the mark, with the seed and land/water marks
  after it, the refusals, the checker, and a replay of each form (`s_lastOptions` legacy without the mark, the band
  with it; the replayed field equals the live one), plus a table from before 0631 (`riverEdgesFromAnyFlag` on).
- **Rule A** `[L]` 2026-11-24 15:20-16:23 on `task-0717-finish` @5b3d955d6: offline-equals-live 17 of 17 (none of the
  library's bundles holds a live-terrain job, section 5, so the mark changes no library replay); the bundle programs and
  the Python bundle suite pass with the new decoder.
- **Not verified:** a replay of the evidence bundles with land-only live jobs (4906, 4912, I3; on F:, not library
  members) through the mark; a live run with the DLL's own edge bits (7.1 ran with the any-edge flag, before 0631); the band on
  real edges is covered by the producer suite's own-bit cases (2.1's "own bits" rows, 7.2) only.
