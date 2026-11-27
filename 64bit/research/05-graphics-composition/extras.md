# Increment 4 "Extras": premises and findings (epic 0454)

Epic 0454 (Extras A: the flags, the messages, the road generator and its subtasks) of the Architect's proposal
`research/07-architecture-decisions/architect-2026-11-16-increment-4-extras-epic.md` (sections 4.4, 5 and 8). This file
holds what the epic's packages establish about stock and about the extras; the design stays in the proposal.

## X0 premises (ticket 4930, 2026-11-19)

Package X0 checks each defect and each stock display the proposal names before any work on it (proposal section 8:
"no project record shows them"). **Stock** here is the stock EXE (`CivilizationV_DX11.exe` build 4390913, SHA-256
`04cfdc71...`, the c5h-* Goldberg copies made before Firaxis' update of 2026-11-19) with the pinned 32-bit DLL
`i0ship-26234371-ia` (SHA-256 `3c2f47da...`) and **without** the patch's proxy or server; the two Extras category
switches do not exist there and play no part. Every picture is DIAGNOSTIC, never ACCEPTANCE (D112); popups were closed
by Lua before each one (D114).

### X0.0 Verdicts

| # | Premise (proposal 4.4 / 8) | Verdict | Mark | Where |
|---|---|---|---|---|
| 1 | Stock roads and railroads show loops and odd curves | **holds**: closed rings of track, pretzels, sharp kinks, dead ends that run on to the plot's far edge, tracks crossing off any junction; in every one of 14 recorded sessions of the huge save | [L] | X0.1 |
| 2 | Stock draws no bridge where a road or railroad crosses a river; with the crossing technology a bridge would be new | **fails** with the technology: stock draws the `Bridges.gr2` model at a crossing once the active team has bridge building (VP `TECH_ENGINEERING`); **holds** without it (nothing at the crossing) | [L] | X0.2 |
| 3 | City buildings and trees stand on roads | **partly**: models stand on the track (a coastal city's lighthouse, an improvement's hut, resource animals, the Great Wall across rails); trees are kept off the track's centre but their crowns overhang its edges | [L] | X0.3 |
| 4 | Units stand inside mountains and wonder models | **partly**: a unit on a mountain plot stands on the slope with the horse's legs inside the rock; no unit by a wonder model was seen (none stood on or next to one in view) | [L] one case | X0.4 |
| 5a | Stock does not show based aircraft beside a city or on a deck | **fails** for a loaded save: stock draws based and carried aircraft (4980 masks, extras-units.md 4980); **holds** once anything changes the aircraft's plot: an aircraft rebased in live, or left on a plot whose units changed, is not drawn, and the same state after a reload is (4985 re-scope, extras-units.md; stock and the server alike) (corrected 2026-11-20 by 4980 and the 4985 re-scope; was: holds, drawn by neither) | [L] 0442, 4980 | X0.5 |
| 5b | Stock does not show a city's garrison | **holds**: removing the garrisoned unit of a city changed nothing in the picture | [L] | X0.5 |
| 5c | Stock draws ships in a city on top of the city centre | **fails**: stock does not draw a ship that stands in a city at all (removing it changed nothing); ships next to the city are drawn on their own water plot | [L] | X0.5 |
| 5d | The stock interface does not show enemy aircraft orders (patrols for the own team only, E7) | **holds**: the interface shows a count and the names of the aircraft on any plot the active team sees (any owner), and the attack preview counts the visible interceptors in range, but no mission, activity or range of another player's aircraft | [C] | X0.6 |

Instance, saves, pins and visual-check ids per premise are in its section. The session log of the live run is in
`tracker/issues/4930-extras-a-x0-premises.md`.

### X0.1 Roads: the gallery (premise 1)

**Sources.** The road layout is the EXE's own in stock and in the joined product alike: the product copies the EXE's
route-curve decal records (`terrain-decals.md` 0.2, 2.2), so a recorded image of those records shows the stock layout
of that session. The layout is random per session (P6, `terrain-decals.md` 3.1), so the defects were counted over 14
sessions of one save, the huge save `C5H-VP8P-HUGE_0350` (SHA-256 `c499d3c6cf83b8baacb57eb950302b68e0aff80510acd5cdbdbdfdd0535188c3`,
turn 350, 128 x 80, cp+vp+eui, Vibrant Textures present), under its copies `C5H-0301-HUGE_0350`, `C5H-0166-HUGE_0350`
and `C5H-4930-HUGE_0350`:

| Sessions | What | Configuration |
|---|---|---|
| 11 | the `decals/load0003.dtap` member of every huge bundle of the scene library (`F:/c5-64bit-work/scene-library/`: `bundles/`, `archive-*`, `retired/`, `staging-v7-trees`), recorded 2026-11-15 to 2026-11-19 on the workstation, instance c5h-a | the joined product (`render_decals=1`); x64 DLL pins `04b1211b8` (9 sessions), `4f4daac70`, `1fd51353e`; i0 pins `26234371e`, `39f17f754`, `5d18d3d6c` (each bundle's `scene.json` `pins`) |
| 1 | 4101's stock census `census_huge_stock.json` with its vertex dump (`F:/c5-64bit-work/0166/4101/`, 2026-11-15 01:12) | stock, DLL `i0ship-26234371-ia` |
| 2 | this ticket's live sessions **L1** (in game 21:32) and **L2** (in game about 21:55, a reload of the same copy), census by `shell/tools/decal_probe.py` (read only) and the active team's revealed routes by `lua/revealed.lua` | stock on c5h-b (C:), DLL `i0ship-26234371-ia`, 2026-11-19 |

The EXE draws the **revealed** route of the active team, not the plot's route: the save's getter dump has 1,017 route
plots, the revealed table 790 (feed) / 784 (live); with the revealed table 770 of the 789 plots under a route curve are
route plots and 20 revealed route plots carry no curve, mostly routes under construction (route state 4/5) or
pillaged. Analyses use the revealed table of the same session (the bundle's `feed.bin` PLOTS table, or `revealed.lua`).

**How a route curve is built [C from the records, L].** One route-curve decal per route plot and direction of travel
(1,354-1,358 per session for 784-790 plots); each is a strip of three vertex rows (two edges and the centre: the texture's
v row in the middle) running from a plot's edge midpoint, 32 samples from its centre, through the plot to another edge
or towards the centre; u runs along the strip within one of four texture regions (road and railroad bands). Centre
lines joined where two strips meet give a graph of 1,518-1,536 nodes per session.

**Kinds and counts** (`render/evidence/0454/4930/gallery.py`; one row per session in `huge_loads.tsv`):

| Kind | Rule (sample frame: a plot is 64 samples wide) | Per session (14) |
|---|---|---|
| loop | a cycle of at most 4 strips that encloses no plot centre: a closed ring of track | 17-30 |
| kink | one centre-line vertex (about 5 samples apart) turns by more than 60 degrees | 31-58 |
| hairpin | a strip's net turn exceeds 150 degrees (the track doubles back in one plot) | 0-3 |
| overshoot | a dead end not on a city plot (roads end at the city ring) more than 10 samples from every revealed-route plot centre: the track runs on to the far edge of its last plot, or towards a plot without a route | 156-181 |
| odd junction (cross) | two strips cross away from any node (an X where no junction is) | 80-122 |
| odd junction (node) | four or more strips meet more than 12 samples from a plot centre | 7-16 |

**P6, what recurs [L].** Of the 334 cases of session L1, 204 recur in L2 within 40 samples. By kind: the **overshoots
are deterministic** (131 of 159 in at least 13 of the 14 sessions); the **loops are random per session** (6 of L1's 25
recur in L2), yet some corners carry a ring in almost every session (69,35 and 75,27 in 12 of 14, 104,40 in 11); of
the 345 corners where three revealed-route plots meet pairwise, 174 show a ring in at least one of 13 sessions and 3
in 11 of them (`pick.py`, 2026-11-19). So rings belong to such corners: the EXE joins each pair of the three plots with
its own curve, and when the curves miss the plot centres they close a ring of track around the corner [I: the rule
behind the random choice was not decoded]. Kinks and crossings recur in a part of the sessions (`huge_cases.tsv`,
column `recur_of_14`, which counts L1 itself).

**The gallery: 27 cases of session L1**, each found in L1's own census and pictured in L1 (stock, c5h-b, zoom default
+3 wheel ticks, interface hidden by Lua, units of the active player asleep, `live_4930.py shots --cams cams_live_L1.json`,
2026-11-19 21:40-21:46). Diagrams of the records per case: `drawplots.py` (`F:/c5-64bit-work/0454/4930/analysis/diagL1/`; every session's case files in `analysis/loads/`).
The second picture is the same camera in session L2 (21:56-22:01), for P6.

| Case | Kind | Plot | Figure | Sessions with it (of 14) | In L2 | Visual check L1 | Visual check L2 |
|---|---|---|---|---|---|---|---|
| lg01 | loop (an oval ring of track in a field) | 69,35 | perimeter 174 | 12 | yes | vc-20261119-214102-workstation-c232 | vc-20261119-215653-workstation-6801 |
| lg02 | loop | 75,27 | 151 | 12 | yes | vc-20261119-214113-workstation-a214 | vc-20261119-215707-workstation-d11f |
| lg03 | loop (teardrop by a trading post) | 104,40 | 132 | 11 | no | vc-20261119-214126-workstation-8aa7 | vc-20261119-215719-workstation-5b95 |
| lg04 | loop (interlaced rings, a pretzel) | 87,29 | 220 | 4 | no | vc-20261119-214137-workstation-b720 | vc-20261119-215732-workstation-816b |
| lg05 | loop | 59,20 | 208 | 8 | no | vc-20261119-214149-workstation-7320 | vc-20261119-215744-workstation-2e96 |
| lg06 | loop | 48,31 | 142 | 8 | yes | vc-20261119-214200-workstation-441f | vc-20261119-215755-workstation-e984 |
| lg07 | loop | 49,34 | 140 | 8 | yes | vc-20261119-214212-workstation-5ea2 | vc-20261119-215807-workstation-eb86 |
| lg08 | loop | 50,47 | 136 | 7 | no | vc-20261119-214225-workstation-ffc4 | vc-20261119-215824-workstation-3b89 |
| lg09 | overshoot (railroad through farms to the coast; a lighthouse stands on it) | 78,25 | 35.5 from a centre | 14 | yes | vc-20261119-214236-workstation-4a6c | vc-20261119-215847-workstation-4a76 |
| lg10 | overshoot | 81,26 | 31.9 | 14 | yes | vc-20261119-214249-workstation-b497 | vc-20261119-215859-workstation-063e |
| lg11 | overshoot | 82,21 | 21.0 | 13 | no | vc-20261119-214301-workstation-2251 | vc-20261119-215910-workstation-b543 |
| lg12 | overshoot | 84,33 | 35.5 | 14 | yes | vc-20261119-214315-workstation-0b90 | vc-20261119-215921-workstation-2409 |
| lg13 | kink | 93,38 | 66.5 deg | 4 | no | vc-20261119-214327-workstation-3fdb | vc-20261119-215931-workstation-c302 |
| lg14 | kink (sharp S-bends through farms) | 109,22 | 87.3 deg | 4 | yes | vc-20261119-214337-workstation-c6e4 | vc-20261119-215941-workstation-7f97 |
| lg15 | kink | 95,36 | 68.9 deg | 3 | no | vc-20261119-214350-workstation-b220 | vc-20261119-215952-workstation-97f8 |
| lg16 | kink | 95,34 | 70.2 deg | 2 | no | vc-20261119-214401-workstation-24c0 | vc-20261119-220002-workstation-83a6 |
| lg17 | kink | 84,50 | 63.6 deg | 7 | yes | vc-20261119-214412-workstation-3cff | vc-20261119-220014-workstation-45d7 |
| lg18 | kink | 47,37 | 97.0 deg | 6 | yes | vc-20261119-214424-workstation-fa84 | vc-20261119-220024-workstation-dcaf |
| lg19 | odd junction (cross; rails crossing under the Great Wall) | 98,36 | 6.0 from the centre | 9 | no | vc-20261119-214435-workstation-0d9a | vc-20261119-220034-workstation-2bfe |
| lg20 | odd junction (cross) | 53,33 | 20.1 | 13 | yes | vc-20261119-214447-workstation-784b | vc-20261119-220046-workstation-a888 |
| lg21 | odd junction (cross) | 97,34 | 10.6 | 8 | no | vc-20261119-214458-workstation-7f8a | vc-20261119-220058-workstation-f66e |
| lg22 | odd junction (cross) | 52,28 | 11.0 | 12 | yes | vc-20261119-214510-workstation-4808 | vc-20261119-220109-workstation-5947 |
| lg23 | odd junction (cross) | 85,35 | 12.9 | 7 | no | vc-20261119-214521-workstation-3d3b | vc-20261119-220120-workstation-2385 |
| lg24 | hairpin (a tangle of track) | 48,25 | net 156 deg | 2 | no | vc-20261119-214533-workstation-9315 | vc-20261119-220135-workstation-c36f |
| lg25 | hairpin | 93,48 | net 150 deg | 1 | no | vc-20261119-214547-workstation-1541 | vc-20261119-220151-workstation-f749 |
| lg26 | odd junction (4 strips meet off the centre) | 97,32 | 4 strips | 3 | no | vc-20261119-214558-workstation-96c6 | vc-20261119-220205-workstation-3f5a |
| lg27 | odd junction (node) | 72,40 | 4 strips | 2 | no | vc-20261119-214610-workstation-d4ac | vc-20261119-220215-workstation-339d |

P6 by eye [L]: lg01's oval ring is there in both sessions with a slightly different outline (L1 / L2 pictures of the
table); at lg03 L1 draws a teardrop ring and L2 a near-triangle of straighter tracks joining the same three plots (the
triangle is not a "loop" by the rule above). Plots east of x = 77 are visible to the active team; the others are revealed under fog (the track is drawn there too,
under the fog's grey). Looked at by eye [L]: lg01 is a closed oval of track with nothing inside, lg04 two interlaced
rings, lg09 a railroad running through farm fields to the shore with the lighthouse model standing on it, lg14 S-bends
of near right angles, lg19 rails crossing beside a fort and under the Great Wall, lg24 a tangle of crossing tracks.
**The test set for 4933** is `render/evidence/0454/4930/huge_cases.tsv`: every case of L1 (334: kind, plot, point in the
sample frame, figure, recurrence, in L2, gallery id). An earlier picture series (`cams_huge.json`, 24 cases picked from
the 2026-11-19 bundle before the live census, the same session L1, 21:34-21:40, from vc-20261119-213428-workstation-b2f5
to vc-20261119-214037-workstation-4aca, at the same zoom) shows the same kinds; since the layout is random it pictures
only the cases L1 happened to share with that bundle.

### X0.2 Bridges (premise 2)

Answered by 4871 (epic 0561, c5h-b, stock with `i0ship-e96457fd-ia`, catalogue save `C5H-0301-4871_0009` of
`C5H-0301-CAT2_0009`, 2026-11-18; `landmark-renderer.md` 8, `city-renderer.md` P6) and cited, not re-run (lead-0454): a
road or railroad across a river gets **no** geometry while the active team lacks bridge building, and the
`Bridges.gr2` model (three IND pieces bent along the crossing) once it has it (VP `TECH_ENGINEERING`); road and railroad
get the same bridge. Visual checks of 4871: without bridge building vc-20261118-172402-workstation-cade and
vc-20261118-173928-workstation-5063 (`P6_road_nobb`), the control vc-20261118-173907-workstation-30e3 (`P6_none`), with
it vc-20261118-173211-workstation-49d5, vc-20261118-173953-workstation-c666 (`P6_road`),
vc-20261118-174014-workstation-f86a (`P6_rail`) and vc-20261118-174242-workstation-f1db.
Seen again on the huge save in this ticket's L1 [L]: bridge decks over the river at the railroad crossings by
(85,34) (vc-20261119-214013-workstation-e7d8), (79,26) (vc-20261119-214025-workstation-fb74) and (84,35)
(vc-20261119-214037-workstation-4aca); the active team has Engineering at turn 350. One deck at (85,34) lies beside the
track's curve rather than under it [L, by eye; not measured]. So `FixRoadBridges` adds nothing stock lacks with the
technology; what is open for X4 is whether the generator's own bridges must follow its own curves.

*Answered live by ticket 0806 (2026-11-26, a beta 4 report "bridges are displaced and don't connect to roads";
city-renderer.md 13.11) [L]:* with `RoadLayout = 1` they must: the server's routes replace the EXE's route decals, but
each bridge stays the EXE's CPU-built mesh bent along the EXE's curve of that session, so on the huge save (c5h-new-a,
beta 4 hotfix 1 build, 7 crossings) decks lie with no road at one or both ends or slanted across our straight track. With
`RoadLayout = 0` every server-drawn bridge joins the server-drawn route (the EXE's own layout and bridge). Two stock
sessions of the same save differ at the same crossings as much as stock and product (route curves and bridge bends random
per session; at one crossing session 1 drew no bridge over two route crossings, session 2 drew two). Beta 5 carries a
`RoadLayout` help line and an OPTIONS.md note that bridges keep the game's placement; moving them onto our roads is ticket
0808 (`FixRoadBridges`' place). *(That help line and note were replaced by ticket 0808 the same day, below.)*

*Closed by ticket 0808 (impl-0808, account B, 2026-11-26, branch `task-0808-roadlayout-bridges`; design
`docs/extras/roads.md` 4h):* with `RoadLayout` 1 or 2 the server now turns and moves each of the EXE's CPU-built bridges
onto our road's crossing of the same hex edge and leaves out a bridge whose edge our layout does not join. No option of
its own (it is part of `RoadLayout`); `RoadLayout = 0` makes no producer and so no placer: nothing changes.

- **What the EXE's bridge holds [C, 4871's P6_road export re-read, `server/evidence/0808/bridges0808.py`'s decode]:** the
  instance rows (VS cb0) are the identity rotation and a translation with z = 0 (P6_road: (768.9, -448.5, 0)); the heading
  and the deck height are in the vertices (model x -5.8..5.8 along the span, y -1.85..1.85 across, z 9.52..13.45). So a
  re-placement is a turn about z and a move in x / y, the height kept.
- **The frame [C, roads.h / cut.h]:** iso = world + (ox, oy), with the height field's ox / oy (the cut grid's rule); a plot is
  64 iso samples. **[L]** on the VP55 huge reference: every EXE bridge's centre lay 2.9-9.7 iso samples from a hex edge's
  middle (the EXE's curves cross at the edge middles, X0.1), so the nearest edge middle within 20 names its crossing.
- **Live run [L, 2026-11-26 18:24-18:54 EST]:** c5h-55-c (mods55, the EXE build of 2026-11-19, C:), a copy `C5H-0808-HUGE_0350` of the VP55
  huge reference `VP55-HUGE_0350-B` (87fbdfec..., turn 350, the active team has bridge building), removed after; the
  branch's own server.exe (`e35e8330...`) and shell_composite.dll (`505dc985...`) laid by lay.py, restored by SHA-256,
  verify clean, released; `[Renderer]` ServerRenderer, ServerUnits, ServerCities, ServerLandmarks, ServerFarmFields,
  ServerCombat = 1. Driver `server/evidence/0808/b0808.sh` (0806's, with 0806's `bridge_look.lua`): arm **base**
  (RoadLayout 0, 7 crossings) and arm **roads** (`EnableVisualAdditions = 1`, `RoadLayout = 1`, 10 crossings: NE x3, E x4,
  SE x3, so every hex-edge axis). Pictures in Visual Checks (issue 0808, DIAGNOSTIC, every entry described,
  vc-20261126-182856-workstation-30e1 .. vc-20261126-185010-workstation-272e); local copies `F:/c5-64bit-work/0808/live/shots`,
  logs `.../live/logs`, not committed.
  - base: no `roadlayout:` and no `models: bridge` line; the game's bridges join the game's roads at every crossing in view.
  - roads: `roadlayout: bridges: placer 1 in force (... 825 route plots, frame 4096.0 2327.8)`; the models line at the end
    ` bridges seen=180410 moved=99657 dropped=80753 kept=0 dup=0 fitfail=0 nobuf=0 max_shift=9.7 max_turn=20.7` (counts per
    drawn frame and bridge; `cpu ... resolved=17 nogeom=0`); no LOUD, no failure. By eye at the 8 crossings not under a
    city: every bridge lies on our road or rail, along it, and joins both banks, on the E axis (crossings 4-7), the NE axis
    (1-3) and the SE axis (8); the game's bridges on roads our tidy layout does not draw (crossing 1's second bridge,
    crossing 2's NE road) are gone. Crossings 9 and 10 lie under a city (not judged).
  - The residual: the mesh keeps the EXE's bend along its own curve (a slight S visible on the E bridge of crossing 5).
- **First attempt, a wrong instance [L]:** on c5h-new-a (mods548) the branch's pins-5.5 build stopped in 10 s on the x64
  DLL's check `CvDatabaseUtility.cpp:350` (server.log): main's server build carries the VP 5.5 DLL, so its live runs need a
  mods55 instance (c5h-55-*), and the 5.4.8 huge save `VP8P-HUGE_0350` does not load there; use `VP55-HUGE_0350-B`.
- **Rule A (MC #17980):** satisfied by the existing `render_roadlayout` keys: a bundle recorded with the layout on carries
  its input, the model tap's rows and the bridge buffers (modelgeom), so the player re-places the bridges the same way; no
  bundle recorded here (no library bundle holds a bridge buffer: only HUGE_0350 has a modelgeom member, with no 20-byte
  buffer). `server/tests/extras/test_roads_scenes.py` 12/12 (rule A E2 off-unchanged and E9, 2026-11-26, 30 min).
- **Not covered:** a crossing our layout draws where the EXE drew no bridge this session (no river data on the server);
  `ServerCities = 0` (the EXE draws its bridges itself); `RoadLayout = 2` live (offline tests only, the jitter included);
  the ANC (earlier era) pieces; the reflection pass (target 1 only was seen in the detail lines).

### X0.3 Buildings and trees on roads (premise 3)

Session L1 [L, by eye]: the lighthouse model of a coastal city stands on the railroad that runs through the farms at
(78,25) (vc-20261119-214236-workstation-4a6c; also vc-20261119-213555-workstation-d581); an improvement's hut
stands on the track by (85,34) and the mine's conveyor spans it (vc-20261119-214013-workstation-e7d8); resource animals
(bison) stand on the rails by (98,33) (vc-20261119-214001-workstation-3a7f); the Great Wall crosses railroads near
(101,25) and (98,36) (vc-20261119-213938-workstation-a076, vc-20261119-214435-workstation-0d9a). Trees: on route plots
with forest or jungle the trees are kept off the track's centre, but palm fronds and crowns overhang its edges
(98,33 and 85,34, same pictures; forest roads 90,20 vc-20261119-213949-workstation-36c1). Units standing on the
track (workers) are units, not part of this premise. So the premise holds for models of cities, improvements, resources
and the Great Wall, and only weakly for trees.

### X0.4 Units inside mountains and wonder models (premise 4)

The huge save has two units on mountain plots (feed of the 2026-11-19 bundle; `feedunits.py`); one is visible to the
active team: player 2's `UNIT_SPANISH_CONQUISTADOR` (id 11196) on (87,41). Session L1, per-unit mask (0442's method:
picture, remove the unit by Lua on the save copy, picture): the unit is one mounted figure on the mountain's flank whose
horse's legs are inside the rock (vc-20261119-215118-workstation-417a before, vc-20261119-215133-workstation-6355 after)
[L]. Whether the unit's other figures, if it has any, are wholly inside the mountain was not checked [I]. No unit stood
on or next to a wonder model in view (Kilimanjaro at (82,22): the nearest unit is a worker on (82,21),
vc-20261119-213852-workstation-07ef; the one unit on a natural-wonder plot, (94,46) Crater, is not visible to the
active team). Premise 4 for wonders stays open; a staged case (a unit set next to a wonder by Lua) is the cheap next
check for X5.

### X0.5 Based aircraft, garrisons and ships in a city (premise 5a-c)

- **Based aircraft and cargo** [L, 0442, 2026-11-16, c5h-g, stock `i0ship-26234371-ia`]: fighters based in a city and
  fighters carried by a carrier are drawn by neither stock nor the server (vc-20261116-211305-workstation-9d7c,
  vc-20261116-211303-workstation-6a4e; `unit-server-scene.md` 12.3). *(Corrected 2026-11-20, ticket 4980 [L]: that holds for
  aircraft created or based live in the session; after a load, stock draws based aircraft among a city's buildings, an
  intercepting fighter as its parked formation and carried aircraft beside the carrier, and so does the server (mask pairs
  vc-20261120-040312-workstation-511e/-040319-...-e53b, -040416-...-283f/-040423-...-acdd, -040520-...-c396/-040527-...-f571;
  `extras-units.md` 4980). Civilians in a city (worker, settler, great general) are drawn too (-040738/-040744,
  -040842/-040849); a ship in a city is not. Stock's drawing of aircraft was not the same in every session: *(cause named 2026-11-20 by the 4985 re-scope [L], `extras-units.md`: the DLL's `updateCenterUnit` hides every aircraft on a plot whenever that plot's units change; after a load they show until then; a live rebase is never drawn where it arrives, the reload of that state is; the server follows the same messages)*.)* The huge save has no aircraft (853 units, land and
  sea only), so none was pictured here.
- **Garrison and ships in a city** [L, session L1, per-unit mask]: Giza (97,21), the active player's city, holds a
  `UNIT_DESTROYER`, a garrisoned `UNIT_MACHINE_GUN` and a `UNIT_ARCHAEOLOGIST` (`near_units.lua`). Removing them one by
  one changed only the same animated spots in every pair of pictures (two 50-px cells with 1,382 and 894-949 changed
  pixels in each step), never a unit-shaped area: stock draws none of the three
  (vc-20261119-214955-workstation-596d, -215009-...-540e, -215023-...-52de, -215036-...-0758). Akhetaten (94,16): a
  workboat and a garrisoned machine gun, the same steps at a far zoom (vc-20261119-214831-workstation-0f64, -214845-...-ac59,
  -214859-...-ba69): nothing visible changed. Ships on the water plots next to a city are drawn there (the destroyer on
  (93,17) beside Akhetaten, vc-20261119-213904-workstation-f9c9).
- So `GarrisonVisible` and `ShipsMoored` add what stock does not show at all; the proposal's wording "on top of the city
  centre" for ships does not describe stock.

### X0.6 What the interface shows of enemy aircraft (premise 5d) [C]

Read in the installed mods of c5h-a (2026-11-19) and the DLL fork (Community-Patch-DLL, `CvLuaPlot.cpp`, `CvPlot.cpp`,
`CvUnit.cpp`):
- EUI's `UnitFlagManager.lua` (VP - EUI Compatibility Files) puts one airbase flag on every plot with air units that
  the active team sees (`plot:IsVisible(activeTeam, true)`), whatever their owner; the flag's text is the count and its
  tooltip `plot:GetAirUnitsTooltip()`, which lists the aircraft's names under `TXT_KEY_STATIONED_AIRCRAFT`
  (`CvLuaPlot::lGetAirUnitsTooltip`: every unit of the plot with `DOMAIN_AIR`). No activity or mission is read.
- CP's `EnemyUnitPanel.lua` (the attack preview of the player's own air attack) shows
  `TXT_KEY_EUPANEL_VISIBLE_AA_UNITS` with `pMyUnit:GetInterceptorCount(pToPlot, pTheirUnit, false, true)`:
  `CvPlot::GetBestInterceptor` with visible-only counts the enemy units on plots the attacker's team sees whose
  `canInterceptNow()` holds and whose `GetAirInterceptRange()` reaches the target; for an air unit `canIntercept()`
  requires `ACTIVITY_INTERCEPT`. So the count does include enemy aircraft on patrol, lumped with anti-air land units,
  as one number for that target only.
- The activity text `TXT_KEY_MISSION_INTERCEPT_HELP` appears only in `UnitPanel.lua` and `MilitaryOverview.lua`, both
  for the active player's own units.
So the interface never shows which enemy aircraft patrol or where their range lies; E7 (patrols drawn for the own team
only) matches stock. Not checked live: whether some other VP screen (e.g. the city's info panel) mentions enemy air
units.

### X0.7 Tools and files

`render/evidence/0454/4930/` (2026-11-19): `dtap.py` (a `.dtap` image or a stock census as records), `routes.py`
(centre lines), `gallery.py` (kinds), `feedplots.py` / `liveplots.py` (the revealed route table of a session),
`allloads.py` (every recorded session of a save), `pick.py` (choice, recurrence, the corner rule), `summary.py` (the
TSVs), `drawplots.py` (diagrams of the records), `feedunits.py` (units on mountains and wonders from a bundle),
`live_4930.py` (the stock arm: prep, launch, census, shots, near, mask, stop, restore), `lua/` (`revealed.lua`,
`near_units.lua`, `kill_one.lua`), camera lists `cams_huge.json` and `cams_live_L1.json`, tables `huge_loads.tsv` and
`huge_cases.tsv`. Pictures, censuses and vertex dumps stay on `F:/c5-64bit-work/0454/4930/live/` (not committed, D83).

## X3 FixRoadLayout: the generator and the candidates (ticket 4933, 2026-11-19/20)

Written by W4 (team-0454, account B), branch `w4-4933`. Design: `docs/extras/roads.md`. Code: `render/src/extras/`
(`c5r::extras::roads`), tests `render/tests/extras/roads_test.cpp`, tool `c5r_roadgen`, evidence scripts
`render/evidence/0454/4933/`. Not wired into server.exe (that is X3b, ticket 4934): nothing draws differently by default.

### X3.1 What the generator is

Our own design from the feed's revealed route table (no decode of the EXE's route builder, D83): which neighbours a road
joins (every adjacent pair of route plots, less the edges that would close a ring around no plot without a route,
decided per triangle cluster in a fixed rank order: city edges, railroad pairs, straight continuations, fewer
triangles, a hash), then per plot a through piece, spurs and dead ends that stop at the plot's centre, drawn as strips
in the recorded route curves' texture bands (priority 0.7, `RoadsandRails_new_*`), one decal per piece. A pure function
of the table: the same save gives the same bytes on every load.

**The look taken from stock's records [C, `stockbands.py` on the huge bundle 140930, 1,376 route curves]:** the band
(v rows) per route state (0 road, 1 railroad, 6 / 7 with a trade route: wider bands), the u span of a texture repeat,
13-20 samples per repeat, half widths 2.2-4.3 samples (iso); the table is in `docs/extras/roads.md` 1. Pillaged routes
(states 2/3) use other u regions (rubble pieces): drawn with the plain band for now [C that they differ; the look not
reproduced].

### X3.2 Results on the huge save (4930's gallery rules) [C, 2026-11-19 23:40-23:55]

Input: the revealed route table of bundle `C5H-0301-HUGE_0350_20261119-140930_workstation` (`feed.bin`; moved to
`F:/c5-64bit-work/scene-library/archive-huge-before-4888/` by 4888's swap), city marks from 4930's plots dump of the
same session (`plots_20261119-140930_workstation.json`: the feed has no city mark). 784 route plots (790 revealed route
plots less 6 under construction), 1,109 candidate edges, 358 on no route triangle, 76 triangle clusters (the largest 75
edges), **345 edges left out by the ring rule, 0 empty faces**. 1,033 pieces (decals), 263,700 vertices (stock: 1,376
decals, 227,640 vertices). `routes.py` + `gallery.py` (4930) on the composed image, the same rules and plots file as
for stock; `check334.py` looks up the 334 cases of `huge_cases.tsv`:

| Layout | loop | kink | hairpin | overshoot | cross | node | 334 cases with a defect within 48 samples |
|---|---|---|---|---|---|---|---|
| stock, this session (gal_20261119-140930) | 22 | 41 | 1 | 167 | 93 | 9 | (the cases themselves) |
| stock, 14 sessions (huge_loads.tsv) | 17-30 | 31-58 | 0-3 | 156-181 | 80-122 | 7-16 | |
| (a) straight | 0 | 0 | 0 | 0 | 0 | 0 | 0, PASS |
| (b) spline | 0 | 0 | 0 | 0 | 0 | 0 | 0, PASS |
| (c) spline + jitter | 0 | 0 | 0 | 0 | 0 | 0 | 0, PASS (final build 2026-11-20 01:05; the picture run's image still had 2 overshoots: the two isolated route plots' stubs, centred on the jittered pass point, 10.1 and 12.3 samples from the centre against the rule's 10; stubs are now centred on the centre) |

Final run [C, 2026-11-20 01:06, `render/evidence/0454/4933/sweep.sh`]: all three styles 0 gallery cases on the whole map,
check334 PASS (0 of 334), 0 empty faces, 0 crossings, 0 loose ends.

Earlier runs of the same day, for the record: a first version with one decal per plot (the pieces of a plot in one
decal) gave the gallery 19 kinks of 63-64 degrees in style (a): its junction corners were not rounded (60 degrees in the
iso frame read as 63-64 in the sample frame), and one hairpin in (b) at a city (a U-turn through a city plot); since then
a junction's through piece is rounded too, its spurs end on the arc, and a city has no through piece (all spokes).

Our own checks (`checkLines`, `countEmptyFaces`) on the same layouts: 0 crossings between pieces, 0 loose ends, largest
turn between centre-line steps 12.4 (a), 6.4 (b), 6.7 (c) degrees; 0 empty faces.

### X3.3 Tests [C]

`c5r_extras_roads` (14 cases at the time, plain clang-cl Release, 2026-11-19 23:25, all pass; 15 with round 2): band rule; grid neighbours (every
direction and its opposite, 64 iso samples apart, wrap); a triangle is not a ring (2 of 3 edges); the ring around a
plot without route is kept, the flower around a route plot becomes a tree, a city in the middle gets a star; filled discs
of radius 1-5 give spanning trees; **negatives**: a hand-made closed triangle and a rhombus rim are each found as one
empty face, a hand-made crossing and loose ends are found by `checkLines`; 12 random maps (60 x 40, half wrapped, 30
random walks and railroad blobs): 0 empty faces, every route plot with a route neighbour joined, every route plot owns a
decal and only route plots do, 0 crossings, 0 overshoots, all three styles; dead ends stop at the centre (no strip past
it); determinism (two productions byte-equal, all styles); local re-lay (366 one-plot changes on a 70 x 46 random map:
the changed decals lie within 2 plots of the change wherever no route triangle is within 3 plots, at most 7 plots away
inside triangle clusters); empty input draws nothing and composing nothing keeps the tap's set (E6), invalid inputs
(size, band, map size, a network of another map, half an edge, options out of range) are refused with the output empty;
compose with a tap set (route curves out, ours in, walk and area blocks kept and valid, tile queries list ours;
negatives for a rectangle outside the world, a bad vertex run, a texture index, the owner table, a bad world); the
strip's v values are the band's rows and u inside its span.
ASan: X3.6.

### X3.4 Cost [L indication, workstation, not under the quiet lock; PERFORMANCE.md: no claim]

Whole huge map (784 route plots), `c5r_roadgen --repeat 9`, single thread: produce (network + layout) median 17.6-34.6 ms
by style and run (min 16.8, max 39.1; the network alone 5.5 ms median), compose with the tap's 17,633 decals 28.0 ms.
A one-plot change today re-runs the whole map and diffs by content (`roadChange`). **Sweep** (`sweep.sh`, 2026-11-20
01:06, quiet check exit 1: a busy machine): 200 one-plot changes spread over the map (every k-th plot on or beside a
route: a route removed or a road added), per style: the farthest plot whose decals changed **median 1-2 plots, p95 3,
max 4-5** (172-190 of 200 within 2 plots; the rest inside triangle clusters); plots re-laid median 2-3, max 11-14;
produce + diff median 25.5-44.2 ms, p95 46-48 ms; whole-map produce median 16.2 (a), 25.6 (b), 17.7 (c) ms (n 21).
So the re-bake a change causes is local (a few plots' rectangles); the computing is whole-map and cheap enough for a
worker thread, or an incremental re-lay per cluster later (X3.6).

### X3.5 The candidate pictures (A1) [L offline, DIAGNOSTIC, 2026-11-19 23:53 - 2026-11-20 00:49]

`render/evidence/0454/4933/candidates.py` (with `bundlecams.py`, `plotsbin.py`, `tinyplots.py`, `check334.py`):
offline player `sceneshot` built from this branch (`C:/c5-64bit-work/4933/ss`), install c5h-d (Goldberg copy, EXE
04cfdc71..., PE stamp 0x546CD0A8, build 4390913; VT present as the bundle's asset set), the four VP mod folders copied
from the recording instance's MODS (the instance's mods database lists none active now, so the player found no mod folder
and refused `Corn_Random_D.dds`; a copy without the database makes it take every folder). RTX 4080. **Contact sheets
in `F:/c5-64bit-work/0454/4933/candidates/` (`index.html`, `index.json`), one per case: stock's recorded route curves,
(a), (b), (c), the same camera.** Visual Checks `--issue 4933`, 17 entries vc-20261120-004258-workstation-dfcd ..
vc-20261120-004925-workstation-cc90, each with its stock picture as reference.

- Cases: 15 on the huge bundle 140930 (4930's choice from this bundle's own curves, `cases_huge.json`): loops rc01-04
  (75,27; 104,40; 97,39; 99,32), overshoots rc05-07 (78,25; 82,21; 90,37), kinks rc08-10 (93,34; 104,34; 54,38),
  crossings rc11-13 (96,35; 109,22; 52,28), the hairpin rc14 (48,25), a four-strip node rc15 (94,32); and two views
  of a hand-placed network on the Tiny bundle 123934 (no library bundle has a road network: every Tiny bundle is turn 9
  with one route plot; `tinyplots.py`: a road with a railroad branch, a three-plot corner, a two-wide railroad strip, a
  railroad yard around a city, a ring of road around a plot without route, a dead end, an isolated plot).
- **What the working copies change, the same in every arm [C, `bundlecams.py`]:** the camera is the bundle's last
  recorded block moved so its axis meets the ground at the case (rotation and projection kept; distance x 0.6, Tiny wide
  x 1.7); the fog's mask shares the overlay pair's window (srv_fow_scene.cpp), recorded around the EXE's camera, so a
  moved camera saw only clouds (L, 2026-11-19 23:50): the window is moved with the camera in `frames.bin` and the
  overlay's texture replaced by a transparent one (no borders or highlights); every plot record of `feed.bin` revealed
  and visible (no fog); bake-ahead radius 6 (`--set render_bake_radius=6`: the same picture as the whole map on rc13,
  byte-equal, 53 s against 95 s a shot). `render_fow=0` refuses to replay (the fog's plot read is part of the feed's
  order) and `render_fow_level=1` alone still draws the clouds outside the window.
- By eye [L]: stock's rings, teardrops, S-bends, tracks running on to the coast and crossings off any junction are gone
  in all three styles; (a) reads as straight track with small rounded turns; (b) and (c) as smooth curves, (c) slightly
  irregular. **Trees** stand on our roads where stock had none and stock's cleared lanes stay (X3.6). Units, cities and
  improvements are as recorded.

### X3.5a Round 2: straight first (the maintainer's review, MC #7497 / D209, 2026-11-20)

The maintainer on sheet `huge_rc14_hairpin_48_25`, no style picked: a straight sequence with a sudden interruption is a bug; the
break should sit in the curved pieces, a train wants to go straight as much as possible; curved pieces shorter; another
set of choices.

**What the abrupt ends on rc14 were [C, `c5r_roadgen --dump 43,20,52,28`, round-1 network].** The diagonal railroad
(route state 7, railroad with a trade route) from the city at (49,23) through (49,24) stopped at **(48,25), a route plot
whose other four route neighbours ((49,25), (49,26), (48,26) roads with trade routes, (48,24) a pillaged road) were all
left out by the ring rule**: the rank put a railroad pair and mixed pairs below same-kind pairs, so in this dense
cluster the tree took the road links and the rail line was cut into a two-plot stub, its single piece running straight
into the centre of (48,25). The road stub top left is the same kind of fault ((48,26): one link kept, three road
neighbours left out). So all three styles showed it: **a generator fault (the rank), not a missing route, not a road /
rail change.** In the 784-plot network such runs (a plot with one edge and a route neighbour left out, ending straight)
numbered **115** under round 1's geometry. Band changes along a straight run (a road plot between railroad plots) remain
where the route type changes, as the game's state is; they are not abrupt ends.

**The fix and the variants [C].** Design: `docs/extras/roads.md` 4a (rank: the straight run first; style `line`; the
checker's `abruptEnds` and `longCurves`). Under the new rank (48,25) keeps its NW-SE link and the rail runs on straight
(the same dump). Huge save, the whole map [C, 2026-11-20 01:40, `candidates.py --round 2`]:

| Variant | abrupt ends | curves > 40 | longest curve | straight share | gallery cases (loop, kink, hairpin, overshoot, cross, node) | check334 |
|---|---|---|---|---|---|---|
| round 1 (a) straight, new rank | 115 | 0 | 33.3 | 0.942 | 0 | PASS |
| round 1 (b) spline, new rank | 115 | 10 | 47.9 | 0.890 | 0 | PASS |
| (d) tight curves 10, merging branches 16 | 0 | 0 | 27.5 | 0.866 | 0 | PASS |
| (e) wider curves 22, merging branches 22 | 0 | 0 | 38.5 | 0.818 | 0 | PASS |
| (f) tight curves 10, square joins | 0 | 0 | 23.6 | 0.919 | 0 | PASS |

All three: 0 empty faces, 0 crossings, 0 loose ends. A first build of the merges gave 10 crossings (two branches on one
side of a through-line merging towards each other, and merges into a bent through-line); both are now square joins.
Tests: `straight_first_rule` (round 1's straight style counts 2 abrupt ends on a corner of three plots, the Line style
0; a zig-zag drawn as a spline with tension 1.6 has 20 long curves, the Line style none and a straight share above 0.6;
two parallel rows keep the lower row whole), the random maps also require 0 abrupt ends and 0 long curves for the
Line style; 15 cases pass.
**Pictures [L offline, DIAGNOSTIC, 2026-11-20 01:43-02:23]:** `F:/c5-64bit-work/0454/4933/candidates2/` (`index.html`),
the same 17 cases and treatment as X3.5 (stock's pictures reused: the same cameras and recorded records), one sheet per
case: stock, (d), (e), (f); Visual Checks `--issue 4933` vc-20261120-021750-workstation-ee3d ..
vc-20261120-022301-workstation-a1f5 (17). By eye [L]: on rc14 the rail line runs on straight through the junction, the
branches merge with short curves in (d) and (e) and meet square in (f); no straight stop in view.
Local re-lay under the new rank (the rank looks up to 4 plots along an axis): the unit
test's largest reach 10 plots inside triangle clusters, still 2 away from them.

### X3.6 Findings for X3b (4934) and X4, open

- **Trees follow stock's roads, not ours [C code, L in the pictures].** The server's forest clears its trees by the
  EXE's cut mask (`srv_cut_tap.*`, trees.md 13.9; recorded in the bundle as `forest.bin`), which the EXE stamps along
  its own route curves; the EXE goes on laying out its roads under `FixRoadLayout`. So our roads run under tree crowns
  where stock had no road, and stock's cleared lanes stay empty (candidate pictures, e.g. rc01). Fix: our pieces
  stamped into the forest's cut source in place of the EXE's route stamps (X3b, or X4 `FixRoadClearance`).
- **The feed has no city mark [C].** `PlotRecord` flags bit 4 is never set (srv_feed.h, 3303), so the product has no
  city plots for the hub rule (cities get spokes, no through road). The candidates took the marks from 4930's plots
  dump of the same session. Without marks the generator works and only loses that rule; a source is open (a DLL
  message kind of X2, or a pure getter).
- Pillaged routes (states 2/3) are drawn with the plain band; stock's rubble look (other u regions) is not reproduced.
- A one-plot change re-runs the whole map (tens of ms on huge) and diffs by content; an incremental re-lay of the
  affected triangle cluster and its two-plot ring is possible if the cost matters (E8, X3b).
- Not wired: option id 18, the scene producer (the farm producer's pattern), the `render_config` record, the live
  smoke (X3b). *Wired by 4934 (X3.7) except server.exe's switch and the live smoke.*

### X3.7 X3b as built (ticket 4934, 2026-11-20)

Design and files: `docs/extras/roads.md` 4c. Summary: style (e) (D212), option row 18 `FixRoadLayout` (bugfix, default 0
for now), `srvextras::RoadLayoutProducer` behind `SceneRoads` (factory in `srv_extras.cpp`), the scene's compose order
roads -> farm -> forest, the bundle's `render_roadlayout` / `render_roadlayout_state`, the player's `--option
FixRoadLayout`, the trees' cut by MC #7664 option (A) after premise P1 (trees.md 14.1 [L]: the route stamp is bit 0x02,
shared, about 12 samples wide, no reference count).

- **Tests [C, 2026-11-20 04:3x]:** `run_extras_test.py`: test_extras 83/83, the injected failure fails, test_roads_producer
  121/121, plain and under MSVC ASan with no report (the product style; the band rule with revealed / headless / pillaged / trade / under construction; the span text
  round trip and 7 refusals; plot-at-sample; the producer threaded and not: first production, its change with the tap's
  route curves, live re-lay on a changed band, no production on an unchanged band, railroad -> road, the table taken away
  and a wrong-sized table (E6: the roads in force kept, LOUD once), the footprint's bits, a new load; determinism across
  two producers; the restored span input reads no table; `StockRouteClear`: cells cleared on bare plots, none past the band,
  **the named risk: a route plot with an improvement and one with a city keep every cell** (cleared 0, kept = the bare
  count), a half-and-half case, no route curve gives nothing, two refusals; the producer publishes the clear mask only
  after its first production and once per tap set; the cut overlay in a `CutTapReader`; the factory). Options 681/681 in
  both trees.
- **Rule A / E2 [L offline, 2026-11-20 05:02-05:18, workstation, this branch's player]:** every bundle of
  `F:/c5-64bit-work/scene-library/bundles/` (16: FOG_0008, HUGE_0350, SCNB_0009 x4, TINY_0009 and 4980's nine C1-C7 bundles) replays
  **0:0** (`--dumped --compare --tolerance 0:0`, no `--option`: FixRoadLayout at its default 0), and no `roadlayout:`
  line (no producer is made). `test_roads_scenes.py` 5/5 (TINY and HUGE off at 1:1 with `FixRoadLayout=0`; on; the refusal).
  Game files read from c5h-e's Goldberg copy (read-only). Script `render/evidence/0454/4934/scenes.sh`.
- **E9, on [L offline, DIAGNOSTIC]:** HUGE_0350 with `--option FixRoadLayout=1`: 784 route plots -> 953 decals (273,684
  vertices), 345 edges left out by the ring rule, 1,376 tap route curves dropped; the frames differ (exit 10). TINY_0009:
  one route plot (no route neighbour: our 16-sample stub; stock draws no curve there), 262 pixels differ. *Finding: a lone
  route plot gets a stub where stock draws nothing; on Tiny at turn 9 that plot is a city's (the feed has no city mark),
  so the stub lies under the city [I: hidden by the city models in the live picture; not checked].*
- **The on/off pair of a forested road** (4933's camera rc01_loop_75_27 on its working copy of the huge bundle, fog lifted,
  bake radius 6; `F:/c5-64bit-work/0454/4934/pics/rc01_roads{0,1}/`, not committed, DIAGNOSTIC): off shows stock's ring
  of road around a stand of trees; on shows our two straight branches and no ring. **The trees do not follow our roads
  in the player**: the offline forest takes its cut from the recorded `forest.bin` (`ForestScene`'s restored `DenseCut`),
  not through `CutTapReader`, so neither the farm's nor the roads' overlay reaches it offline; our roads run under tree
  crowns and stock's old lane stays empty there. The clearance is pictured only on the product path (live check, waiting
  on 0600), or after the player's forest takes `CutOverlaySlot()` too (a change in `server/src/forest/`, not this ticket's).
- **For the next extra that needs the plot table in the player [C, 2026-11-20]:** the fog's table cannot be wrapped
  before `Scene::init`: sceneshot's `RestartFowWithOverlay` (srv_scene_sources) gives the fog a new table after init, so a
  wrapper set before it never sees a copy (4934 lost about 20 min to this). The forest's table slot
  (`ForestPlotTableSlot`) is made once and read at every snapshot; 4934's player takes its copies there (a bundle needs
  `render_forest=1` for `--option FixRoadLayout=1` without a recorded state).
- **Cost, indication [L offline, the player, not threaded, NOT under the quiet lock, PERFORMANCE.md: no claim]:** on
  HUGE_0350: production 29.1 ms, footprint 25.2 ms, the stock clear mask 143.6 ms (81,482 cells on 345 plots cleared,
  146,425 cells on 469 plots kept for an improvement or a city), compose 14.2 ms. The clear mask was first built inside
  `composeDecals` on the render thread; it now runs on the producer's worker like the production (threaded in
  server.exe), so the render thread pays only the compose (once per tap set and road version: cached).
- **Waiting:** server.exe's switch (`srv_render_host.cpp`, held by 0600: the patch is on thread 0600 #7702, with the
  recorder's `setRoads` line) and so E1 (statscmp on and off), the live check on the product path (the trees' clearance
  in a real picture) and E8 per PERFORMANCE.md. *Done by W7 (X3.8), 2026-11-20 08:03-09:01.*

### X3.8 X3b live on the product path (ticket 4934, W7, 2026-11-20)

**Setup [L].** Branch `w7-4934` (team head 7e513c57e + main 729a7c482 with 0600 + the held v2 hunk, commit 11546a72d):
server.exe's `render_roadlayout` now follows `FixRoadLayout`'s effective state from `VP64Options.ini` unless server.cfg
names it (`ParseHostConfig`; a value other than 0 or 1 is a refusal). Release build of server.exe and the proxy (DLL pin
1fd51353e, `src copy`, logs say `repo 11546a72d dirty=0`), this branch's composite. Instance **c5h-b** (C:, Goldberg, build
4390913, EXE stamp 0x546CD0A8 checked before every launch), windowed through the harness (no focus, the D155 watcher per
launch), claimed 08:03-09:01 (MC-B grant). Layout by `server/evidence/0600/lay.py` (prep with SHA-256 backup, set-proxy,
the composite, a TEST pack with `pack=1` as the installer writes it, `vp64helper on`: `ServerRenderer = 1`), CP's
`SQLITE_LOGGING` set to 1 (backed up by hash), `[Extras] FixRoadLayout = 0|1` appended per arm. Driver
`render/evidence/0454/4934/w7_live.py` (prep / arm / bad / restore), pictures `w7_smoke.py`, frame reader `w7_frames.py`.
Save: a copy of `C5H-VP8P-HUGE_0350` (c499d3c6..., huge, turn 350, 784 route plots revealed to the active team).
Workstation busy throughout: `quietlock.py check` listed 3-6 other teams' heavy jobs (builds, a Ghidra pass, another game
instance, sceneshot plays) at each arm.

- **E1 PASS [L, 08:05-08:23].** `perf/profilekit/acceptance.py run --config inc2 --no-ledger`, autoplay T350 -> T360,
  FixRoadLayout 1 (08:05-08:13, 510 s) then 0 (08:14-08:23, 498 s), both windowed on c5h-b (D164: like with like).
  `tools/d10/statscmp.py ON OFF --from-turn 350 --expect-turns 350 360`: **IDENTICAL, 14 tables, 130,227 rows**, every
  per-turn table with rows for 351..360 on both sides. The option does not change the game's state.
- **The producer runs on the product path [L, server's render.log of the on arm].** `terrain scene: render_roadlayout=1:
  the server lays out the roads and railroads (FixRoadLayout, style (e)); the tap's route curves dropped`; at the load:
  784 route plots -> 953 decals (273,708 vertices), 345 edges left out by the ring rule, 19.0 ms on the worker; footprint
  192,858 cells of 2 x 2 samples (13.6 ms); first tap set: 1,376 route curves dropped, 953 road decals added; the EXE's
  route cells to clear: 82,032 cells on 346 plots, 147,678 cells on 468 plots kept (an improvement or a city). Over the 10
  turns: **52 productions** (the observer's revealed set grows under autoplay: 784 -> 1,044 route plots, 953 -> 1,318
  decals), 112 clear masks, 163 composes, **0 LOUD roadlayout lines** *(corrected 2026-11-20 by W7: were 104 / 224 / 326, counted twice because render.log and server.log carry the same lines)*. The off arm made no producer (no `roadlayout`
  line; rule A live). The server's `roadlayout totals:` line is not written on the product path: server.exe ends on the
  EXE's exit (`FAILED: the engine is gone`, as every run) before the render thread's end, where the totals are printed [C,
  srv_render_host.cpp ThreadProc; L].
- **The trees follow our roads live [L, DIAGNOSTIC pictures, 08:38 and 08:42].** The same save, camera on (75,27), (99,32)
  and (100,30) moved in 3 steps, popups closed through the popup queue (D114), on then off: Visual Checks
  vc-20261120-083809-workstation-a183, vc-20261120-083815-workstation-d078, vc-20261120-083822-workstation-286e (on, each with its off picture as reference:
  vc-20261120-084256-workstation-5308, vc-20261120-084304-workstation-c836, vc-20261120-084312-workstation-673f); files in `F:/c5-64bit-work/0454/4934/w7/smoke-{on2,off2}/`
  (not committed). At (75,27) off shows stock's teardrop loop of railroad around a stand of trees with its cleared lane; on
  shows our two straight branches in a V, **each with a cleared lane through the forest, and stock's loop lane filled with
  trees again** (the EXE's route cells cleared on bare plots, MC #7664 option (A)). At (99,32) the railroads run straight
  with short fillets through the jungle with a lane. Pictures are DIAGNOSTIC (D112): a test build and a test pack, not the
  shipped system. A first take (08:29) ran the popup close in the wrong Lua state (`ContextPtr` nil outside `InGame`):
  its three entries are marked SUPERSEDED.
- **A bad value ends the game with the report and popup (D213) [L, 08:45-08:46].** `render_roadlayout=2` through
  vpproxy.ini `server_cfg_extra` (TEST ONLY), a new Tiny game: `render: LOUD: not started: server.cfg render_roadlayout=2:
  0 or 1`, `FAILED: the server's renderer could not start: ...`, server exit code 5 at 0.8 s; D119 report
  `20261120-084620` (stage handshake, code boot-call, the server's reason), the patch's popup (Visual Checks
  vc-20261120-085400-workstation-866b, the watcher's capture), the game ended, no grey map. Closed by the claim's owner
  with Close after the game had ended (COMMON 29).
- **Observed, not this extra's:** 200 `remote: LOUD: stale Unit instance ... Unit.GetTransportUnit / GetPlot` lines in
  server.log, the same count in both arms (the Lua side's, present with the option off).
- **Restore [L, 08:54-09:01].** `lay.py restore` (every backed-up file `equal` by SHA-256; set-proxy --original), the XML by
  hash, the user `VP64` folder that held an earlier ticket's reports put back, `frontend.py restore`, `provision.py verify
  c5h-b --quick` ok (the verify inside lay.py's restore said rc 1 while that folder was moved out; rerun ok); the arms'
  `stats.db` moved out of the instance's cache (it had none). Released 09:01.
- **Offline reruns on the merged head [C, 2026-11-20 09:03-09:57, w7-4934 with main 7cb3e57f8]:** run_all render-host
  163 passed + 1 failed (`test_resize_request`: "fake_consumer did not open the texture" under load; alone three times
  after the suite: 3/3 passed), forest-scene 3/3, scenebundle-py 21/21; `scenes.sh`: test_roads_scenes 5/5 and rule A
  **16 of 16 library bundles 0:0 with no `roadlayout:` line** (FixRoadLayout at its default 0). Before main's FrameEye fix
  (55fe87d38) render-host, forest-scene and scenebundle-py could not build on any branch with 0598's line (main's break,
  not this extra's).
- The E1 arms' 22 per-turn pictures (acceptance.py's own wording called them acceptance pictures) are relabelled
  DIAGNOSTIC in Visual Checks (`--issue 4934`).

### X3.9 E8: what FixRoadLayout costs, and where (ticket 4934, W7, 2026-11-20) [indication only; PERFORMANCE.md: no claim]

**What was measured, with what.** (a) Offline: the player `sceneshot` of this branch (`--option FixRoadLayout=1`,
`--dumped --compare --no-pictures`) on the library's huge bundle, five plays one after another through `quietlock.py run`;
the producer's own `roadlayout:` lines (each step times itself with `steady_clock`). The player runs the producer NOT
threaded (every step on the player's one thread). (b) Live: the E1 arms of X3.8 (one run per arm, 10 autoplay turns,
the huge save at T350, windowed, c5h-b), the server's render.log 1 s lines (`terrain 1s` gpu_ms / cpu_ms, `host 1s`
submit_ms = Scene::draw to the sink's end) and the producer's lines, read by `render/evidence/0454/4934/w7_frames.py`;
turn times = acceptance.py's `turn.autoplay_wall_s` per turn. **Workstation, not a quiet machine, indication only**
(COMMON 23, D106): no quiet lock, 3-6 other teams' heavy jobs registered during each arm (quietlock.py check at the arm
starts), one run per arm (grade C of PERFORMANCE.md 2.6), statistics logging on in both arms (D35 pragmas).

**Each cost, its thread and when it runs [C, `srv_extra_roads.cpp`, `srv_render_scene.cpp` roadsDecalPoll; L numbers]:**

| Step | Thread (server.exe) | Runs | Live huge T350-360, n / median / p95 / max ms |
|---|---|---|---|
| production (`roadsx::produce`: network + layout of the whole map) | producer's worker | once per change of a revealed plot's route band (and once per load) | 52 / 28.7 / 46.1 / 47.7 |
| footprint (`RoadFootprint::build`, our pieces' cut cells) | producer's worker, right after the production | with each production whose footprint differs | 51 / 17.6 / 34.1 / 34.3 |
| clear mask (`StockRouteClear::build`, the EXE's route cells on bare plots) | producer's worker | once per new tap set (decal generation) and load | 112 / 164.3 / 210.6 / 239.2 |
| compose (`composeRoadSet`: the tap's route curves out, ours in) | **render thread**, in the poll between frames (`roadsDecalPoll`, `decalPoll`) | once per (tap set, road version); cached otherwise | 163 / 24.2 / 32.3 / 34.5 |
| per frame | render thread | every frame | a pointer compare (the compose cache); no per-frame road work |

Offline, the same four steps on the library's huge bundle (HUGE_0350 214920: 784 route plots -> 953 decals, 1,376 route
curves, the final tap set 17,743 decals), five plays 09:52-09:56, one of each step per play, all on the player's one
thread, median (min - max, sd): production 23.9 ms (16.1 - 29.1, 5.3), footprint 17.1 (12.4 - 25.3, 6.3), clear mask
123.2 (85.5 - 144.5, 24.7), compose 12.2 (11.4 - 18.2, 3.4). Each play exit 10 (the frames differ from the recording,
E9's expected result), 7 `roadlayout:` lines and its totals line. W6's single play (X3.7: 29.1 / 25.2 / 143.6 / 14.2)
lies inside these ranges. The live compose (median 24.2 ms) is about twice the offline one on similar tap sets: the
live render thread shares the machine with the game and the other teams' jobs [I: not separated].

**Frame and turn, on against off (one run each, live):** frame GPU time median 0.246 / 0.234 ms (p95 2.70 / 1.27,
mean weighted by frames 0.446 / 0.326); scene CPU 0.913 / 0.921 ms; submission median 4.70 / 4.68 ms (max 266.6 /
66.7); frames drawn per second, median of 288 / 282 seconds, 53 / 55; seconds per autoplay turn, sum of 10 turns 261.2 /
258.8 s (median 24.6 / 23.0, mean 26.1 / 25.9); server private bytes after turn 360: 2,047 / 1,990 MB.

**Why the numbers are what they are [I from the code and the counts].** Production and footprint scale with the
revealed route plots (784 -> 1,044 over the run, every whole-map re-lay), and they ran 52 times because autoplay's
observer reveals new route plots every turn; a human player's view changes far less often. The clear mask is the largest
step (rasterising 1,376 route-curve records against the cut grid of 4,096 x 2,688 cells) and runs per tap set, which the
EXE republishes whenever its decals change (112 times in ten AI turns); it is off the render thread. The compose is the
one step on the render thread: about 24 ms each time a tap set or our roads change, which in a turn with many changes is
one or two late frames per change; the submission's max (266.6 against 66.7 ms) and the GPU p95 point the same way but
one busy-machine run cannot separate them from the other teams' load. The turn-time difference (+0.9 %) is far inside
the workstation's 5-15 % spread: no turn cost is shown and none is expected (the producer reads the feed only; E1 shows
the game unchanged). The ~57 MB of private bytes is in line with the producer's copies (the decal set of ~300k vertices
twice: ours and the composed set, the footprint and clear-mask bit grids) [I: not itemised].

**How often the compose runs in play (lead #8097 point 3, MC #8097) [L counts, C triggers, I for a human player].** From
the on arm's render.log alone (every `roadlayout:` line is also in server.log; `w7_frames.py` read both and doubled the
counts first given here, corrected above), per turn window = from the turn's first sighting to the next one
(acceptance.py's per-turn.csv; `C:/c5-64bit-work/0454/4934/w7/compose_turns.py`, scratch):

| Window | tap sets | productions | composes (after a tap set / after a production) | route plots revealed |
|---|---|---|---|---|
| the load (before T350) | 4 | 3 | 6 (3 / 3) | 784 |
| T350 (autoplay armed) | 10 | 8 | 18 (10 / 8) | 845 -> 1,019 |
| T351 - T358, per turn | 8 - 15 (median 11) | 2 - 7 (median 4) | 12 - 19 (median 15) | +1 to +5 a turn |
| T359 (autoplay ends) | 10 | 7 | 17 (10 / 7) | 825 - 1,044 |
| in all | 112 | 52 | 163 (111 / 52) | 784 -> 1,044 |

So in the steady turns about 15 composes a turn (0.6-0.8 a second of a 20-25 s AI turn), each about 24 ms on the render
thread. **Triggers [C, `RoadLayoutProducer::composeDecals` and `Scene::roadsDecalPoll` / `decalPoll`]:** (1) a new tap set,
111 of 163: the EXE republishes its whole decal set whenever any decal in it changes (an improvement, a route, a farm, a
pillage, anywhere in the revealed area), and the composed set depends on the tap set, so every republish is composed
again even when our roads did not change; (2) a production, 52 of 163: a revealed plot's route band changed (a road or
railroad built, removed, pillaged or repaired, a trade route on or off) or the revealed set grew. The two jumps in the
route plots (T350's arming, where `Game.SetAIAutoPlay` makes the observer follow a player's view, and T359's end, when
the view goes back) are autoplay's, not a player's [I: matches 4676's note in clientarm.py on the observer's visibility;
not separated in the log]. **What a normal player sees [I]:** no observer jumps; productions only when a route band
changes in their revealed area (their own workers, the AIs' roads in sight, pillage in a war), a few a turn late in a
game; tap-set republishes on the same events plus every other decal change in sight, so on a huge late game probably
several a turn, mostly during the AI turn while the player watches the map. Each is one 24 ms render-thread stall: at 60
fps one or two frames late, a visible hitch if the camera moves at that moment.

**Can the compose move to the worker? [C for the code read; I for the effort.]** `composeRoadSet`
(`render/src/extras/roads.cpp`) is CPU only: it checks the road decals, copies the tap's decals except its route curves,
appends ours with their vertices and maps texture names into one `decal::DecalSet`; no D3D11 call, no device or context
(it also builds a `std::string` per road decal for its error text before any check fails, about a thousand allocations a
compose: a cheap saving). `Scene::composedDecals` then runs the farm's and the forest's composes (render thread, also CPU)
and `ResidencySource::setDecals` resolves texture names against the install and swaps the set in use (CPU). The D3D11 work a change causes is later and stays where it is: the residency's re-bake of the changed rectangles'
tiles (render targets, draws and copies on the device context [I: the bake pipeline's thread not re-read here]),
driven by `takeDecalChanges`. The
DecalSet is immutable once published (`shared_ptr<const ...>`), so the worker can own a compose. **Sketch:** a third job
kind on the producer's worker, beside the production and the clear mask, which already run once per tap set: a
`ComposeJob {tap shared_ptr, road decals version}` queued in `composeDecals` when (tap, version) differs from the cache;
the worker runs `composeRoadSet` and posts the set; `composeDecals` returns the newest finished set (the previous one until
then: one or two frames with the previous tap set's decals, as with any tap set read late) and `roadsDecalPoll` hands it
to the residency when it arrives; a result whose tap generation or road version is stale is dropped; the unthreaded path
(the player, tests) stays as it is. What stays on the render thread: the poll, the farm and forest composes (unless they
move the same way), `setDecals`, and the re-bakes. **Effort [I]:** about 3-5 h with tests (threaded and unthreaded give the
same set, stale results dropped, the cache kept), rule A unchanged (the player is unthreaded), one live smoke.

**Open (written into ticket 4934):** the quiet-machine E8 is owed before the default can ever be proposed as 1. It needs
a Goldberg testbed job on oldpc that runs the product path with the flag on and off (interleaved, >= 6 runs per arm),
which does not exist yet; nothing was submitted to any testbed. Cheap improvements if E8 asks: compose only the changed
decals or move the compose to the worker (the render thread would then only swap a pointer), and re-lay a cluster
instead of the whole map (X3.4).

### X3.10 All connections, part 1: the generator and the offline previews (ticket 4940, 2026-11-22)

The owner's request D215 (a third road layout where every link between neighbouring road plots is drawn, as in Civilization
III, laid out by variant (e)'s rules); part 1 on the owner's word of 2026-11-21 about 23:50 [reworded for privacy] (limited to generating about 10
previews in the render harness); the option key, the producer and the live checks after the first beta. Design:
`docs/extras/roads.md` 4e. Code: `Connect::All` in `buildNetwork`, `LayoutOptions::junction` (`Through`, `Cross`,
`Ring`, `Rosette`) in `layOut`, `c5r_roadgen --connect --junction` (branch `task-4940-road-grid-civ3`).

**The networks [C, `c5r_roadgen`, 2026-11-22 00:03, render build of this branch, style line fillet 22 merge 22]:**

| Scene | route plots | edges, tidy (e) | edges, all connections | plots with 4-6 links (not a city) | decals (e) / cross / ring / rosette |
|---|---|---|---|---|---|
| huge (`C5H-0301-HUGE_0350_20261119-140930_workstation`, cities from 4930's plots dump) | 784 | 764 of 1,109 (345 cut by the ring rule) | 1,109 | 205 (134 with 4, 57 with 5, 14 with 6) | 1,025 / 1,506 / 2,018 / 1,813 |
| tiny design (4933's hand-placed network, `tinyplots.py`) | 41 | 38 of 57 | 57 | 6 | 51 / 79 / 92 / 86 |
| dense (`denseplots.py`: every land plot of x 36-48, y 7-17 on the Tiny bundle a route, a railroad along row 12, two cities) | 118 | 120 of 290 | 290 | 96 (56 with 6) | 162 / 330 / 658 / 562 |

Every treatment on every scene: 0 crossings between plots, 0 loose ends, 0 abrupt ends, the largest turn between
centre-line steps 12.4 degrees (as (e)). Straight share of the centre lines on huge: (e) 0.818, cross 0.872, ring 0.730,
rosette 0.667; the checker's "curves longer than 40" counts each ring once (205 on huge: a ring is 56.5 iso samples
round), the other treatments 0.

**Variant (e) unchanged [C]:** with the defaults the generator's output did not move: the (e) images this branch writes
for the huge and the tiny scene are byte-identical to 4933's `line_wide_merge.dtap` (`cmp`, 2026-11-22), and so is the
stock image; the previews therefore reuse 4933's stock and (e) pictures of those cases (same bundle, camera, sceneshot
build).

**Cost [L indication only, workstation, not under the quiet lock, sceneshot of another job possibly on the GPU;
PERFORMANCE.md: no claim]:** produce (network + layout) median of 9 on huge: (e) 16.95 ms, cross 23.9 ms, ring 21.8 ms,
rosette 25.8 ms. The decal count rises by 47 % (cross) to 97 % (ring) over (e) on huge; E8 against value 1 on the dense
scene is part 2's.

**Tests [C, 2026-11-22]:** `render/tests/extras/roads_test.cpp` 22 tests (16 before), all pass: every candidate kept
under all connections (triangle, disc, six random maps plain and wrapped: each edge bit equals "both plots are routes";
the tidy network a subset), the centre plot of a radius-3 disc per treatment (decals 5 / 3 / 7 / 6; the cross passes
through the centre, the rosette stays more than 4 iso samples off it, the ring's nearest point is its radius; a city
stays a six-spoke hub), no change below `junctionMin` (and a three-link plot under `junctionMin` 3), random maps clean
(0 crossings, overshoots, abrupt ends) and deterministic for every treatment, the option checks.

**Other suites [C, 2026-11-22]:** the render suite (`render/build.py --config Release --test`) 108/108, 0 skipped;
`server/tests/extras/run_extras_test.py` test_extras 83, test_roads_producer 121 (as 4934's), the injected failure fails;
`build_sceneshot.py` and `server/build_server.py --config release --no-proxy` link (the callers of `roads.h`, runbook
2.10's `server/src/extras/*.cpp` row).

**Pictures [L offline, DIAGNOSTIC, 2026-11-22 00:05-00:34]:** Visual Checks `--issue 4940`, ten sheets:
dense vc-20261122-003404-workstation-1840, dense_wide -003406-workstation-b6e0, tiny_design -003409-workstation-fc48,
huge rc01 -003430-workstation-59ef, rc03 -003441-workstation-8588, rc08 -003442-workstation-fff1, rc11
-003444-workstation-0826, rc13 -003446-workstation-ddcb, rc14 -003447-workstation-356c, rc15 -003458-workstation-0181.
On rc01 the three treatments give the same picture (no plot with four or more links in view; the difference to (e) is
the loop's cut link drawn again). By eye [L]: on rc14 the junction west of the city shows the three treatments clearly
(lines crossing in a point; a roundabout; curved corners round an empty centre); the dense scene reads as Civilization
III's lattice under cross. Tool: `render/evidence/0454/4940/shots.sh` (runs
`previews.py` per scene through `quietlock.py run`; a huge shot about 0.8-2.5 min, a Tiny one about 40 s); sheets in `F:/c5-64bit-work/4940/previews/` (never in git):
per case stock, (e), cross, ring, rosette and a legend. Cases: huge rc01, rc03 (loops), rc08 (kink), rc11, rc13
(crossings), rc14 (the hairpin of round 2), rc15 (node) from 4933's second candidate set; the tiny design; the dense
scene close and wide. A centre-line diagram of the dense scene (`linesdiagram.py`): cross reads as a triangular lattice
of straight lines, ring as a lattice of small roundabouts, rosette as rounded triangles with the plot centres left empty.

### X3.11 All connections with jitter, part 1b (ticket 4940, 2026-11-22)

The owner, 2026-11-22 about 00:40, after X3.10's sheets [reworded for privacy]: wanted slightly more jitter in the lines, closer to the original screenshot. Design:
`docs/extras/roads.md` 4f (`LayoutOptions::lineJitter`, `c5r_roadgen --jitter N`, 0 .. 7 iso samples; branch
`task-4940-road-jitter`).

**The checks per amount [C, `c5r_roadgen`, 2026-11-22 00:44-00:46, style line fillet 22 merge 22, all connections]:**

| Jitter (iso samples) | huge: crossings / loose ends / largest turn per step (cross; rosette) | dense: the same |
|---|---|---|
| 0 | 0 / 0 / 12.4; 12.4 | 0 / 0 / 12.4; 12.4 |
| 3 | 0 / 0 / 6.6; 7.0 | 0 / 0 / 6.6; 7.0 |
| 5 | 0 / 0 / 6.8; 7.6 | 0 / 0 / 6.9; 7.6 |
| 7 | 0 / 0 / 7.2; 8.3 | (tiny: 0 / 0 / 7.4; 7.4) |
| 8 (refused now) | 1 / 0 / 7.4; 8.6 | 0 / 0 / 7.4; 8.5 |

0 abrupt ends everywhere; the ring's largest turn is 7.5 (its own 48-point circle). The largest turn falls with the
jitter on because short curves then get 32 points instead of 16 (16 keeps jitter 0 byte for byte). Before the two fixes
of roads.md 4f (merges ending on the bent line, the cross rounded through its centre) the same sweep on huge gave 55-61
crossings and 4-36 loose ends at jitter 3-12, and the largest turn rose to 13-20 degrees: those are the first build's
numbers, replaced. **Jitter 0 is byte for byte part 1:** the images of cross, ring, rosette and (e) on huge and dense
rebuilt with this branch equal X3.10's (`cmp`).

**Tests [C, 2026-11-22]:** `roads_test` 25/25 (3 new: jitter 0 and an explicit 0 give the same bytes under both networks
and every treatment, a jitter moves them, style (c) ignores it; amounts 2, 3, 5, 6, 7 on four random maps (two wrapped,
one with a filled disc) for every treatment: 0 crossings, loose ends and abrupt ends, the turn per step no larger than
without jitter, every route plot drawn, deterministic; the jittered cross still meets in one point off the plot centre
within 7; lineJitter 8 and -1 refused).

**Pictures [L offline, DIAGNOSTIC]:** per case the cross at jitter 0 (X3.10's picture), 3 and 7, the ring and the
rosette at 5, and a legend (`render/evidence/0454/4940/shots.sh --part 1b`); sheets in
`F:/c5-64bit-work/4940/previews1b/` (never in git). Visual Checks `--issue 4940`, ten sheets [2026-11-22 00:47-01:15]:
dense vc-20261122-005121-workstation-3db0, dense_wide -005123-workstation-543d, tiny_design -005410-workstation-787b,
huge rc01 -011533-workstation-e194, rc03 -011534-workstation-5851, rc08 -011536-workstation-a3b8, rc11
-011537-workstation-25f1, rc13 -011539-workstation-2494, rc14 -011540-workstation-505f, rc15 -011541-workstation-ce59.
By eye [L]: at 3 the runs wobble just visibly, at 7 the dense lattice reads hand-drawn while every link stays joined;
the cross's lines still meet in one point.

**Other suites [C, 2026-11-22 00:53-01:01]:** render suite 108/108, 0 skipped; `run_extras_test.py` test_extras 83,
test_roads_producer 121; `build_sceneshot.py` and `server/build_server.py --config release --no-proxy` link.

### X3.12 Part 2: the options and the product path, offline (ticket 4940, 2026-11-22 20:50-)

Terms (MC-B, MC #10671/#10673/#10677/#10685): headless and offline only; the centre treatment and the jitter become
option values; default stock; ids 30 and 31 confirmed; RoadJunction and RoadJitter default 0 (a non-zero default is the
owner's pick from the sheets); `srv_render_scene.{h,cpp}` held by 0674, not edited. Design: `docs/extras/roads.md` 4g.

**The options [C, `options/tests/test_options.cpp`, 905 checks in both trees, 2026-11-22 21:05 and 21:22]:**
- Row 18 renamed `FixRoadLayout` -> `RoadLayout` (never shipped, MC #8258), range 0..2: 0 stock, 1 the tidy layout
  (variant (e), a bugfix), 2 all connections (a visual addition). MC #8261's "a category per value" needed a new member:
  `Def::valueCategory` (one entry per value, value 0 `kCatNone`; NULL on every other row) and `CategoryOf(d, v)`.
  `ExtraOf` takes the switch of the value's category: `RoadLayout = 2` with `EnableVisualAdditions = 0` is off
  (`kExtraCategoryOff`, decided by `EnableVisualAdditions`, value 0: stock, never 1); `EnableBugfixes = 0` does not touch
  value 2. The start-log line names the value: "RoadLayout: off: EnableVisualAdditions = 0 (default) switches off every
  visual addition (RoadLayout = 2 is one)".
- Rows 30 `RoadJunction` (0..3: 0 through, 1 cross, 2 ring, 3 rosette, the values of `c5r::extras::roads::Junction`) and
  31 `RoadJitter` (0..7, `LayoutOptions::lineJitter`): visual additions, default 0, no `needsKey`. Why no `needsKey`
  [C, `tools/installer/options_schema.py`]: the installer's page puts an extra under the extra it needs only when that one
  is a checkbox ("would sit under RoadLayout, which is no checkbox"); they sit under `EnableVisualAdditions`, and the
  consumer reads them only while `RoadLayout = 2` is in effect.
- The table rules (`ExtrasRuleViolations`): a per-value row is exempt from the Fix prefix but may start with neither Fix,
  Enable nor Experiment, runs 0..N, has value 0 = `kCatNone`, every other value a bugfix or a visual addition, and its row
  category is its first non-zero value's (7 new broken-table cases, one control).
- The installer's page [C, `test_options_schema.py` 22/22 on the real table, 2026-11-22 21:22]: RoadLayout shows as a
  radio 0, 1, 2 under `EnableBugfixes` (captions "0 = as the game lays them out (default for now)", "1 = tidy", "2 = all
  connections"); RoadJunction and RoadJitter as radios under `EnableVisualAdditions`. Help text rule found: the extra's
  help must name exactly one category switch in the form "needs <Switch> = 1" (`NEEDS_RE`), so RoadLayout's value 2 says
  "used only while EnableVisualAdditions is 1" (no "needs"), and the two new rows use the house form "(also needs
  EnableVisualAdditions = 1)" (the schema test's switch swap replaces exactly that text).

**The product path [C]:** the scene (`srv_render_scene.cpp`, held) passes `render_roadlayout` on as "on" only, so the
whole choice goes through a process-wide slot, `srvextras::RoadLayoutChoiceSlot()` (header only, `srv_extras.h`: an
inline function's static, so a program without `server/src/extras/` needs no symbol). `ParseHostConfig`
(`srv_render_host.cpp`) fills it from `RoadLayout`/`RoadJunction`/`RoadJitter`'s effective values, or server.cfg's
`render_roadlayout` 0..2, `render_roadlayout_junction` 0..3, `render_roadlayout_jitter` 0..7; the recorder writes the
two shape keys only with layout 2 (a layout-1 bundle keeps the keys it had); `CheckBundle` refuses another value, or a
shape key without layout 2; sceneshot takes the bundle's keys or `--option RoadLayout/RoadJunction/RoadJitter`. The
producer reads the slot once, when made (`RoadStyleFor`: layout 1 = `ProductRoadStyle()`, layout 2 = the same with
`Connect::All`, the junction and `lineJitter`), and names it in its load line ("roadlayout: field of load ...; layout 2
(all connections), junction cross, jitter 3").

**Tests [C, 2026-11-22 21:15]:** `run_extras_test.py`: test_extras 136 (83 before; the choice's style for every junction
x jitter, the clamps, the ranges with six refusals, the text, the slot read once, a dense 8 x 8 production: tidy leaves
edges out, all connections none and more decals, the same choice twice the same decals), test_roads_producer 121.

**The product's layout offline [L offline, 2026-11-22 21:41-21:56, sceneshot built from this branch (release, F:),
`server/tests/extras/test_roads_scenes.py` 10/10 in 857 s; the C: library copy `C5H-0301-TINY_0009_20261119-123934` and
`C5H-0301-HUGE_0350_20261122-124422`; game copy `pure_vp` (4390913, Vibrant Textures), MODS of the asset root
`mods548-vt`]:** E2 equal to the recording at 1:1 with `RoadLayout=0` and with `RoadLayout=2` while
`EnableVisualAdditions` is 0 (no producer: stock, never 1), on both scenes; E9 with `RoadLayout=1` and with `RoadLayout=2,
EnableVisualAdditions=1, RoadJunction=1, RoadJitter=3`: the producer names its layout, the same route plots, all
connections leaves no edge out and draws more decals than the tidy layout on the huge save, the frames differ from the
recording (exit 10); `--option RoadLayout=3`, `RoadJunction=4`, `RoadJitter=8` and the old `FixRoadLayout=1` refused
(exit 2). The script: `C:/c5-64bit-work/4940-p2/scenes.sh` (scratch; the command is in the hand-back for the runbook).

**The installer's page from the built server.exe [C, 2026-11-22 21:35]:** `server.exe --write-options ...
--write-options-schema ...` of this branch's release build, read by `options_schema.build_page`: `RoadLayout = 0 (radio
0, 1, 2)` under `EnableBugfixes`; `RoadJunction = 0 (radio 0, 1, 2, 3)` and `RoadJitter = 0 (radio 0, 1..7)` under
`EnableVisualAdditions`, no refusal.

**Sheets for the owner's pick [L offline, DIAGNOSTIC, 2026-11-22 21:58-22:22]:** the three treatments (rows: RoadJunction
1 cross, 2 ring, 3 rosette) at RoadJitter 0, 3, 7 (columns), as `RoadStyleFor` makes them under `RoadLayout = 2`
(`sh render/evidence/0454/4940/shots.sh --part 2 --record`; the generator `c5r_roadgen` and sceneshot of part 1b, whose
layout code this part does not change; the jitter-0 column and the cross at 3 and 7 are X3.10's and X3.11's images,
reused; 4 new shots per case). Sheets in `F:/c5-64bit-work/4940/previews2/` (never in git); Visual Checks `--issue 4940`:
dense vc-20261122-220335-workstation-6db6, dense_wide -220338-workstation-6334, tiny_design -220658-workstation-a1fd,
huge rc11 -222201-workstation-3204, rc13 -222206-workstation-c1ed, rc14 -222212-workstation-7dad. By eye [L]: the ring's
roundabouts and the rosette's curved triangles keep their shape at jitter 7; every link stays joined.

**Other suites and links [C, 2026-11-22 21:35-23:10]:** `server/build_server.py --config release --no-proxy` OK (117 s),
`proxy/build_proxy.py --config release` OK, `build_sceneshot.py --smoke` OK; `run_all.py --only render-host --only
scenebundle-programs --only scenebundle-py`: 185, 4 (sceneshot_test 256) and 22 passed; `test_installer.py -k
test_options_page` 2/2 (the test installer built with Inno Setup); `test_safety_static.py` 55 passed. Not linked here:
the other builders of `vp64_options.cpp` and `srv_render_host.cpp` (acceptance file
`render/evidence/0454/4940/acceptance_part2.txt`). Not done: anything live (re-lay on a route change, E1, E8, the video).

## X9 ExperimentSharperTerrain, step 1: the one-hour offline test (ticket 4937, 2026-11-20)

By W4, `server/evidence/0454/4937/sharp.py`, 2026-11-20 00:45-00:58, workstation RTX 4080, the offline player of 4933
(same build, install c5h-d, the same working-copy treatment: camera moved with its overlay window, fog lifted).

**What "512 per tile" already is [C, terrain-bake-passes.md 3.5].** The bake's blend targets are 512 x 512; the cache
every tile is drawn from by default is BC1 256 x 256 (6 mips, 43,680 B; t0 and t8). Stock and the server (0371,
`render_bake_near=1`, the default) then up-res visible tiles to an **RGBA8 near pair at the wanted size, 512 when the
tile's projected size exceeds 256 px**, drawn by the same CacheBake at that size: the full blend detail, uncompressed.
The 512 pool holds 64 textures (32 tiles with t0 and t8); the up-res takes the farthest visible tiles first, two per
settled frame, and an empty 512 pool gives a tile no near pair at all (array mode), so at stock cameras the closest
tiles can stay BC1 256 (3.5.1: 26 at camera 2). So the proposal's experiment (bake near tiles at 512, 4.4) exists in
stock's and the server's design; what is open is whether it reaches the tiles near the camera.

**The test [L offline, DIAGNOSTIC].** Arms `--set render_bake_near=0` (every tile from BC1 256) and `=1`, four
cameras: Tiny `near_city` (the recorded camera's point, distance x 0.45) and `near_coast` (x 0.30), huge `near_farms`
(5035.8, 1698.8; x 0.45) and `near_hills` (6200.5, 2359.5; x 0.30). Pairs with 2x centre crops:
`F:/c5-64bit-work/0454/4937/pair_*.png`, Visual Checks `--issue 4937` vc-20261120-005105-workstation-1fdc,
-005236-...-23f4, -005453-...-f4e4, -005644-...-9d56. Results:
- Three of the four pairs are **byte-identical**; `near_city` differs in 59,640 pixels (1.7 %), all in the top 72
  rows (the far tiles), by at most 24; the 2x crops show no visible difference. Centre-crop sharpness (mean absolute
  Laplacian) equal in both arms at every camera (14.659, 11.687, 8.670, 3.936).
- **Why: the offline player does not up-res.** It replays the near pairs the recording held at its first frame
  (`near.bin`: Tiny 50 pairs, huge 40), placed by the live up-res for the recorded camera, farthest tiles first; a
  moved or zoomed camera gets no new near pair in a one-shot replay. So the offline harness cannot show a 512 bake at a
  chosen near camera today [C from the player's log lines, L].
- **Bake time and memory [L, the player's lines]:** the whole huge map at BC1 256, 4,922 tiles, **410.1 MB of
  caches** (415.9 MB of arrays), 1.18-2.12 s of baker GPU time over 4 runs (0.24-0.43 ms a tile); Tiny 1,352 tiles,
  109.4 MB, 0.33-0.54 s. The near pools hold 179.3 MB when full. **At BC1 512** (7 mips, 174,752 B a cache [C
  arithmetic], t0 and t8) the same 4,922 tiles would hold 1,720,258,688 B = 1,640.6 MiB, 4.0 x the 410.1 (the log's "MB"
  are MiB: 4,922 x 87,360 B = 410.06 MiB): the proposal's [I] "about 1.6 GB against 413 MB" holds. Not baked at 512: the size class of the cache is fixed at 256 (`kCacheSizeDefault`); no
  `render_config` key sets it.

**Recommendation (for the MC and the maintainer): close the experiment.** Do not build a whole-map or radius BC1 512 cache (4x
the video memory of the caches) on this evidence: 512 near the camera is already stock's own design (the RGBA8 near
pairs), and nothing measured here shows a sharper picture to gain. **Proposal: close `ExperimentSharperTerrain`
(ticket 4937) after step 1; option id 21 stays reserved, no code** (MC #7475, 2026-11-20, to be closed at 0454's next
milestone). Should the question come back, the open part is narrower than the proposal's: whether the tiles nearest the
camera get their 512 near pair in play (pool full, farthest first), and if not, a larger 512 near pool or a
nearest-first up-res order; it needs a live A/B on an instance or an up-res step at the shot camera in `sceneshot`.

## X10 the contribution guide's worked example: VisibleAreaOutline (ticket 4938, 2026-11-19)

The contribution guide for extras (public text; the maintainer's yes 2026-11-19 23:14, MC #7372) walks through one real extra end to
end. This section records what was built and measured for it. Marks: `[C]` read in code, `[L]` run here.

### X10.1 What it is

`[Extras] VisibleAreaOutline` (id 22, MC #7345; visual addition, `needsKey` none, range 0-1, **default 0, not approved**):
a thin ribbon on the ground along every hex edge between a plot the active team sees now and a neighbour it has revealed
but does not see now (`PlotState::Seen`). An unexplored neighbour gets no edge: it lies under the fog's clouds, and the
approved guide text says "revealed but out of sight". Display only: its input is the fog scene's per-plot states
(`FowScene::states()`, kept from the feed's kind 3 `kFowChanged` and the fog's own table reads, caller 3) and the scene's
height field; no new DLL send, no fork change, no new `copyPlots` caller and no bundle format change [C].

### X10.2 How it is built [C]

- `server/src/extras/srv_extras.h/.cpp`: the scene's extras module (`SceneExtras`), made through a factory registered with
  `srv_render_scene.cpp` (the model module's pattern): a program that does not link `server/src/extras/` gets null.
  `Scene::init` takes the options once per Scene (`configureExtras(SrvOptions(), o.fow != 0)`): no extra **of this module**
  in effect (`srvextras::SceneExtrasWanted`, the ids `Extras::configure` reads) = no module, so no extras code runs and the
  picture is the build before. (Corrected 2026-11-22, mc-red: the gate was "any `[Extras]` row on", which the unit extra
  FixAirBases' default 1 (D235) tripped in every Scene and made the scene tests LOUD; fog-of-war.md 7.7.) `Scene::fowFeed` hands each batch to it after the
  fog; `Scene::extrasDraw` draws after the ranged-attack arc and before the trees, cities and units; `totalsLine` at the
  end. server.exe links the folder (`build_server.py` render units), so does the player (`build_sceneshot.py`); the
  scene tests do not.
- `server/src/extras/srv_extra_visible_outline.h/.cpp`: `BuildOutline` (CPU: axial neighbours in cyclic order, an edge's
  ends = the centroid of three plot centres, so no hex size is assumed; 4 segments per edge with the ground's height at
  each point + 0.75 lift; ribbon half width 1.2 world units); `OutlineRenderer` (own HLSL by D3DCompile, blend
  SRC_ALPHA / INV_SRC_ALPHA with the destination's alpha kept, depth LESS_EQUAL without writes, three copies one world
  width apart on a wrapping map, the context's state put back); `VisibleOutline` (rebuild on a new fog generation or a
  new height field; E6: no fog / no field / a table that does not fit = nothing drawn with one line at each change; the
  fog off in this server, a shader or draw failure = off for the session with one line).
- `server/tools/sceneshot`: `--option KEY=VALUE` (repeatable, `[Extras]` keys only, any loader warning = exit 2) sets the
  player's `SrvOptions()`; every extra's E9 pictures go through it.

### X10.3 Results [L, 2026-11-19 23:10-23:56 EST, workstation, RTX 4080, busy machine]

Player `build_sceneshot.py` of w5-4938 (with `server/src/extras/`); game files read-only from the Goldberg copy c5h-e
(EXE `04cfdc71...`, its MODS database with Community Patch, Vox Populi and the EUI files active). c5h-a, the library's
recording instance, read "0 mods activated" at 23:30 and the unit pre-check failed there (exit 8): not the extra's fault.

- **E2 (off is unchanged):** `--dumped --compare --tolerance 1:1 --option VisibleAreaOutline=0` on the four library
  bundles TINY_0009, HUGE_0350, SCNB_0009 (12:52) and FOG_0008: 8 of 8 frames byte-equal each, no extras line (no
  module is made with the extra off).
- **E9 (on):** HUGE_0350 (128x80 plots: 774 visible, 9,034 seen, 432 unexplored at fog generation 1): 380 edges, 9,120
  vertices, heights 0.75-31.61; 14,600-14,655 of 3,603,694 pixels changed per frame, all in x >= 1251 (the side of the
  map where the sight edge lies in this camera); `--compare` exits 10 (Differs) as it must. Diagnostic pair:
  visualcheck `vc-20261119-234430-workstation-7d14` (pictures in `C:/c5-64bit-work/0454/4938/shots/pairs/`, not committed).
  The Tiny-based bundles (TINY, SCNB, FOG; turn 8-9) have 28 visible, 0 seen and 1,988 unexplored plots: nothing to
  outline, on equals off, logged "first draw: 0 edges". So early-game scenes show nothing; the line appears once plots
  have been seen and left.
- **E8:** the player's settle+draw+readback per frame on HUGE, 5 runs per arm alternating, 35 later frames each: off
  median 52.4 ms (39.7-67.7), on 50.9 ms (40.3-696.0, one outlier). No difference beyond the noise; `quietlock check`
  was NOT QUIET (five other heavy jobs), so this is an indication, not a PERFORMANCE.md figure. The edge rebuild on a fog
  change: 0.205-0.384 ms (n 5, 380 edges, CPU, QueryPerformanceCounter). GPU memory: one dynamic vertex buffer of 16,384
  x 16 B = 256 KiB at this size. Seconds per turn: none added on the DLL's thread by construction [I: not measured].
- **Unit tests:** `run_extras_test.py` 83 passed (clang-cl /W4 /WX, WARP device) and 83 under MSVC ASan (canary caught,
  no report); the injected failure fails. Options: `test_options` 673 passed x86 and x64.
- **Not run:** E1 (statscmp on vs off from one save) needs a game instance with the product path; by construction the
  extra reads only the fog's states on the render thread and the render units compile without the DLL's headers [C].
