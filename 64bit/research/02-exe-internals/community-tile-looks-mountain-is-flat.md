# Community report: a tile drawn like a mountain while the plot is flat (ticket 0554, 2026-11-18)

The report (community, unconfirmed; the owner 2026-11-18, D187 [reworded for privacy]: the owner has not seen it, so treat it with caution): a tile's
look does not match what it is, usually drawn like a mountain while the plot is flat, and it stays so after reloads.
Investigated by impl-0554 (account B) on 2026-11-18 14:43-15:45 EST on the Goldberg instance c5h-c, stock route
(the stock DX11 EXE with the 32-bit DLL pin `i0ship-e96457fd-ia`, batch 4), mods CP + VP + EUI (VP 5.4.8 instance
profile `mods548`), Vibrant Textures asset set (D94).

## 1. Result in one paragraph

**Reproduced on the stock route, as a property of the stock EXE and DLL, not of the patch [L].** No plot of two
unmodified saves (CAT_0009, VP8P-HUGE_0350; 12,256 plots) is drawn against its plot type. The symptom appears in three
ways, and only the first two survive a reload. (1) **A natural wonder is drawn by its own shape, whatever the plot type**:
the EXE gives a wonder plot the wonder's flag in place of the terrain flags (asset-pipeline.md 4.2 point 1), so a
Kilimanjaro or Mt Sinai plot made flat grass by Lua is still drawn as the mountain after a reload (z 30.7 and 37.8,
flat land is 12). VP places Mesa and Uluru on the core tile's own plot type (flat or hills, passable), and they are
drawn as tall rocks (Mesa on flat: z 36.1, mountain height). A mod's natural wonder that the EXE does not know gets
Mt Fuji's flag (0x40000), so it is drawn as Fuji on any plot [C, from 4.2; not run]. (2) **A mod that re-applies a
plot-type change at every load** (case 3 re-applied) keeps the live mismatch for good [I]. (3) **A mod that changes a
mountain's plot type alone** (`SetPlotType`, the terrain stays `TERRAIN_MOUNTAIN`): during play the tile stays drawn
as a mountain (the field is static until a load, epic 0465); at the reload the DLL's `CvPlot::Validate` turns it back
into a mountain and removes its feature and resource, so drawing and rules agree again and the mod's change is lost
[L]. A mod that changes plot type and terrain together is drawn correctly after a reload [L]. What is wrong, by the
ticket's question: **the drawing, not the saved map data**: the saved plot data was always what the mod wrote (except
where `Validate` rewrote it), and the EXE builds its height field from it at every load; the field is not in the save.
Product route not run (section 5).

## 2. How the drawn height was compared with the plot data (tool)

- `tools/harness/evidence/0554/plotscan.lua` (InGame state, read only) returns, for every plot, the plot type,
  terrain, feature, river and lake flags, a natural-wonder bit and the drawn height at the plot's centre:
  `HexToWorld(ToHexFromGrid(Vector2(x, y))).z`. That z is the EXE's CPU height field (`z = u16 / 255 * 0.2`, equal
  within 2e-6 at 10 plots, `05-graphics-composition/asset-pipeline.md` 4.5) [C, by that earlier check]. The field is
  what the terrain is drawn from, stock route and product route alike (the product taps the same field).
- `tools/harness/evidence/0554/analyse.py SCAN.txt [--json OUT] [--list TYPE]` prints the z distribution per plot
  type and flags: flat or hills plots with z >= 26 (drawn as high as a mountain), mountains with z <= 24 (drawn low),
  flat plots with z >= 17 (drawn at hills height). Natural-wonder plots are listed apart.
- Height bands on CAT_0009 (56x36, 2026-11-18) [L]: water 0-8.4, flat 11.8-15.7 (median 12.02), hills 14.6-23.0,
  mountains 29.0-44.4. The bands do not overlap between flat and mountain, so one centre sample per plot separates
  them.

## 3. What the DLL does with a plot whose type and terrain disagree [C, source at the pin e96457fd5]

- `CvPlot::setPlotType` (CvPlot.cpp 7231) changes the terrain only when the plot changes between land and water. A
  mountain set to `PLOT_LAND` or `PLOT_HILLS` keeps `TERRAIN_MOUNTAIN` (7); a flat plot set to `PLOT_MOUNTAIN` keeps
  its grass or plains terrain [C, and L: section 4].
- `CvPlot::isMountain()` (CvPlot.h 447) is `terrain == TERRAIN_MOUNTAIN || plotType == PLOT_MOUNTAIN` ("the two
  should be synonymous"); the EXE reads it through `ICvPlot1::IsMountain` (+0x6C) when it builds the terrain at load
  (asset-pipeline.md 4.2 point 1, flag 0x400). `isHills()` is the plot type only.
- `CvPlot::Validate` (CvPlot.cpp 14510), run on every plot by `CvMap::Validate` (CvMap.cpp 2894; the engine calls it
  through `CvDllMap::Validate` right after a load, "called immediately after serialization") and by
  `CvGame::DoGameStarted` (CvGame.cpp 1048): terrain MOUNTAIN with another plot type -> plot type set to MOUNTAIN;
  plot type MOUNTAIN with another terrain -> terrain set to MOUNTAIN; in both cases the plot's feature (unless a
  natural wonder) and its resource are removed.

## 4. Scans and trigger tests (stock route)

Every attempt, 2026-11-18, instance c5h-c, stock route as in the header; scans with section 2's tool; raw answers and
JSON under `C:/c5-64bit-work/0554/` (not kept), the summaries in `tools/harness/evidence/0554/RESULTS.txt`.

| # | save (copy) | what was done | result |
|---|---|---|---|
| 1 | C5H-0301-CAT_0009 (56x36, turn 9) | scan after load | 2016 plots, **0 mismatches** [L]; 4 wonders (Reef x2 on water, Mt Kailash on a mountain at z 40.3, Solomon's Mines on flat at z 12.0) |
| 2 | same session | Lua `SetPlotType` only (`edit_cat.lua`): A mountain (9,16) -> flat, B mountain (47,15) -> hills, C flat (34,9) -> mountain, D Mt Kailash plot (38,12) -> flat, E river mountain (37,16) -> flat; rescan live | all five keep their old drawn height: A z 33.4, B 29.0, E 32.4 drawn as mountains with plot type flat/hills; C drawn flat (12.0) with plot type mountain; D still drawn as Kailash [L]. The terrain stays TERRAIN_MOUNTAIN on A, B, D, E and grass on C (section 3) [L] |
| 3 | C5H-0554-CAT-EDIT (saved from 2 by `UI.SaveGame` on the main thread), reloaded in a new process | scan + `plots.lua` | **all five plots are mountains again** in the rules (plot type 0, terrain 7, isMountain true), drawn as before: the scan has 1's bands and 0 mismatches (14 plain mountains: the 13 of 1 plus C) [L]. `CvPlot::Validate` at load turned A, B, D, E back into mountains (terrain was MOUNTAIN) and gave C mountain terrain (section 3). The mismatch of 2 does not survive a reload; the mod's change is undone instead |
| 4 | C5H-VP8P-HUGE_0350 (128x80, turn 350, 1930 AD) | scan after load | 10,240 plots, **0 mismatches** [L]; bands as on CAT (flat max 15.9, hills 11.9-23.1, mountains 29.6-44.6); 7 wonders: Kilimanjaro, Mt Sinai, Potosi, Gibraltar on mountains (z 28-40), Volcano on a mountain at z 20.4 (its crater), Lake Victoria on flat at z 6.3, Barringer Crater on flat at z 12.5 |
| 5 | same session | Lua `SetPlotType` + `SetTerrainType` together (`edit_huge.lua`): W1 Kilimanjaro plot (82,22) -> flat grass, W2 Mt Sinai plot (44,28) -> flat plains, M1 mountain (44,20) -> flat grass, M2 mountain (104,21) -> plains hills, F1 flat (96,23) -> mountain + mountain terrain | live: isMountain false on W1, W2, M1, M2 and true on F1; nothing redrawn (picture: Kilimanjaro still drawn at (82,22), feed entry of `huge_edit_live_kilimanjaro.png`, DIAGNOSTIC) |
| 6 | C5H-0554-HUGE-EDIT (saved from 5), reloaded in a new process | `plots_huge.lua` + scan | the rules keep every edit (no plot touched by `Validate`: plot type and terrain agree). M1 drawn flat (z 12.06), M2 drawn as hills (18.29), F1 drawn as a mountain (33.69): **the reload redraws ordinary plots to match** [L]. **W1 (Kilimanjaro, plot type flat, grass) is still drawn as the mountain at z 30.69 and W2 (Mt Sinai, flat plains) at z 37.77** [L], both impassable (the wonder feature's `Impassable`), isMountain false, isHills false. Picture `huge_edit_reload_kilimanjaro2.png` (DIAGNOSTIC; the first take `huge_edit_reload_kilimanjaro.png` showed the terrain gray, a frame taken before the tiles streamed in after the camera jump). **This is the report's symptom, and it survives the reload** |
| 7 | C5H-0554-HUGE-NW (from 6: Lua `SetFeatureType` put Uluru on flat plains (48,22) and Mesa on flat desert (51,29), `edit_huge_nw.lua`), reloaded | `plots_nw.lua` | Uluru: plot type flat, passable, **z 24.19** with neighbours at 11.97-12.04; Mesa: plot type flat, passable, **z 36.11** (mountain height) with neighbours at 12.0-19.0 [L]. Picture `huge_nw_reload_mesa.png` does not show the Mesa clearly (fog, improvements); the height reading is the evidence |

Database facts behind row 7 (`nwplacement.lua`, `nwfeatures.lua`, VP 5.4.8 mods548) [L]: the wonders without a
`ChangeCoreTileTo*` rule keep the core tile's plot type (`CoreTileCanBeAnyPlotType`): Crater, Mesa (desert core
tile), Reef, Gibraltar, Geyser (desert), Uluru (desert or plains). Of these, Crater, Mesa, Reef, Geyser and Uluru are
passable (`Impassable` false). So on ordinary VP maps a Mesa or an Uluru can stand on a flat or hills plot that units
walk onto while it is drawn as a tall rock; Kilimanjaro, Sinai, Kailash, Sri Pada, Fuji, Potosi and Volcano get
`ChangeCoreTileToMountain` and Lake Victoria, Solomon's Mines, El Dorado and the Fountain `ChangeCoreTileToFlatland`.
Neither the CAT nor the Huge save holds a Mesa or an Uluru, so this was not seen on a generated map [I: map
generation was not run].

No crash, assert or dialog box in any of the five launches (dialog watcher marker clear at every check) [L].

## 5. What was not done

- **The product route was not run.** The server draws the terrain from a tap of the same EXE height field
  (`05-graphics-composition/renderer-in-server-design.md`, "the server's field comes from the tap's height samples"),
  which the scans above read, so the product shows the same heights [I: would be confirmed by one reload of
  C5H-0554-HUGE-NW through the proxy and a picture at (82,22)]. Skipped within D187's timebox: the stock behaviour
  answers the report, and the product's terrain inherits it.
- No mod that adds natural wonders unknown to the EXE was installed, so the Fuji fallback (flag 0x40000) was not
  seen live [C from asset-pipeline.md 4.2 only].
- The order of `CvMap::Validate` and the EXE's `CreateTerrain` during a load was not established; it does not change
  the result, because `isMountain()` already reads the terrain OR the plot type, so the EXE draws a mountain for either
  half of a mismatched pair [C].
- The texture side (the material byte layer) was not compared; the height alone separates the classes.
- The first picture after a `UI.LookAt` jump showed the land flat gray until the tiles streamed in (stock route,
  `huge_edit_reload_kilimanjaro.png`); a retake a few seconds later was normal. Not investigated (gray tiles on pan,
  D86, are already tracked).

## 6. Fix proposal for the MC (no product code in this ticket)

1. **Natural wonders drawn by their shape on any plot type**: stock design (the wonder's art), not a defect of the
   patch; nothing to fix for VP's own wonders. Mods that make a mountain wonder flat land, or add wonders the EXE does
   not know (drawn as Fuji), are covered by epic 0465 (live terrain: "Any <---> Special (mountains, wonders, etc.)")
   once the server draws terrain from the plot data itself: the server can choose the wonder shape by the feature and
   the base by the plot type, and give unknown wonders a neutral base instead of Fuji. Recommend: add this report and
   the Fuji fallback as acceptance cases to 0465; close 0554 as explained (stock behaviour).
   *2026-11-24: the unknown-wonder part is built by ticket 0613 as `[Extras] FixUnknownWonderBase` (default 1): live and
   at every load (`research/05-graphics-composition/live-terrain.md` 8.13.5).*
2. **`SetPlotType` leaving `TERRAIN_MOUNTAIN` behind (and `Validate` undoing the change at the next load, with the
   plot's feature and resource removed)**: a DLL behaviour that loses a mod's change and data silently. A small DLL
   change would make `CvPlot::setPlotType` keep the pair consistent (leaving `PLOT_MOUNTAIN`: terrain to
   `LAND_TERRAIN` or the mod's choice; entering it: `TERRAIN_MOUNTAIN`), as it already does for land/water. That
   changes game results of mods that rely on today's behaviour, so it is an upstream question, not ours to take
   alone; recommend at most an upstream report after the owner's word (D95 scrubbing applies).
3. Live redraw during play (case 3 before the reload) is epic 0465's scope already.
