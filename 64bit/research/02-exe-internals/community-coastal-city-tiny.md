# Community report: coastal cities drawn about 3 times smaller than a land city of the same population (ticket 0555)

Community report, unconfirmed (the owner, 2026-11-18, decision D187 [reworded for privacy]: to be investigated with caution, as unconfirmed). The report:
cities on some coastal tiles are drawn about three times smaller than a land city of the same population.

Timebox: about 1 h of reproduction on Goldberg instances, stock route first, then the product route of the current
pins (batch 4). Work 2026-11-18 from 15:32 EST (first launch 15:36), on the workstation, pool instance `c5h-b`
(profile mods548, mods cp, vp, eui exclusive, so Squads was off). Implementer impl-0555 (account B).

## 1. What the knowledge base already says (before any run)

How the EXE builds a city's picture is in `research/05-graphics-composition/cities-and-landmarks.md` 1.5 and 1.12
(ticket 0337, [C] from the decompile, [L] from 3907's stock captures):

- The size bucket comes from the DLL's `GetVariableCitySizeFromPopulation`: Fibonacci thresholds 1, 2, 3, 5, 8, 13,
  21, 34, 55. Two cities whose populations are in the same bucket (for example 13-20, or 21-33) get the same density
  map and the same stamp.
- The blocks stand on a fixed lattice. Each block takes its class from the **City Density Field**, which has seven
  shapes: `flat` and six coast shapes `0`, `1`, `05`, `15`, `045`, `135`, each with four symmetries (25 layouts). A city
  uses the layout whose 6-bit key matches a mask that 0x53FA50 builds from its plot record and three neighbours. When
  no layout matches, it uses `flat`.
- An anchor is then dropped when the 16-bit terrain value at its plot cell is outside 13,001-35,000, or when the
  terrain normal there has z < 0.85 (about 32 degrees of slope).

So by design a coastal city keeps only the blocks that fall on land, and a city on steep ground loses blocks on the
slopes. If the report is right, the likely place is a coast mask that matches no layout: such a city falls back to
`flat` and keeps only its land part. Before the runs this was [I]. Sections 2 and 3 test the look live (3.1 and 3.2
find it); the mechanism itself is still [I] (section 4).

## 2. Stock route (2026-11-18 15:36-16:02 EST) [L]

Set-up: the instance's Steam DX11 EXE, the Goldberg wrapper route, the frame cap on, the dialog watcher on (`hold`),
and no proxy. The DLL in the CP folder was the instance's base `i0ship-26234371-ia` (batch 2, instance-aware,
SHA-256 `3c2f47da...`). This is a comparison arm by COMMON 18: the EXE draws the cities, and the batch makes no
difference to the save's state as loaded.

Save: `C5H-VP8P-HUGE_0350` (huge, turn 350, 124 cities). The save was not changed: no turn was played and nothing was
saved. All units of the active player were put to sleep (`tools/scenario/lua/quiet.lua`). The camera was put on each
city with `UI.LookAt(plot, 0)`, zoomed in to the limit, then moved out a fixed number of wheel ticks. That zoom was the
same for every shot of a round. The mouse was moved off the city. The interface was shown, and no popup was open
(`UI.IsPopupUp()` false).

Cities were chosen by the Lua script `C:/c5-64bit-work/0555/cities.lua`, which lists owner, art style, era,
population, `IsCoastal(10)`, lake neighbours, hills and harbour. A second script, `water.lua`, lists each city's
water-neighbour directions (0-5).

| Coastal city (style, pop, water sides) | Land city of the same owner (pop) | Size bucket | Result |
|---|---|---|---|
| Izapa (South America, 13, 5 sides: 01345, harbour) | Coba (14) | 13-20 | Izapa is a compact walled core; Coba's blocks spread somewhat wider. Comparable, not 3x |
| Liverpool (European, 15, 4 sides: 0135) | Dresden (16, German, same style) | 13-20 | comparable |
| Toledo (European, 19, hills, 3 sides: 012) | Cordoba (18) | 13-20 | comparable |
| Uxmal (South America, 19, ocean and lake) | Copan (17) | 13-20 | comparable |
| Harappa (South America, 17) | Copan (17) | 13-20 | comparable (round 1) |
| Hastings (European, 20, 2 sides: 45) | Coventry (20) | 13-20 | comparable (round 1) |
| Kiel (European, 16) | Dresden (16) | 13-20 | comparable (round 1) |
| Giza (Middle East, 22, harbour) | Xochicalco (22, captured, Egyptian art) | 21-33 | comparable |
| Pi-Ramesses (Middle East, 28, ocean and lake) | Heliopolis (26) | 21-33 | comparable |
| Asyut (Middle East, 16, hills) | Tzintzuntzen (16, hills) | 13-20 | comparable (round 1) |

**Stock result: not reproduced in 10 pairs.** No coastal city was drawn anywhere near three times smaller than its
land pair. The coastal cities lose the blocks that would stand on water, as section 1 expects, and their cores
(walls, centre towers, palace and wonder slots) are the same size.

Shots: Visual Checks feed, issue 0555, result `info`, every one labelled DIAGNOSTIC (D112). Files are in
`F:/c5-64bit-work/0555/stock/` (round 1: zoom limit then 4 ticks out) and `F:/c5-64bit-work/0555/stock2/`
(round 2: limit then 1 tick out). Contact sheets: `C:/c5-64bit-work/0555/view/sheetA.jpg` and `sheetB.jpg`.

**Capture trap [L].** Three of the twelve round-1 PrintWindow frames on the stock route (Giza, Dresden,
Pi-Ramesses) came back without the interface and from a farther camera, and the cities in them showed no building
models. They are marked "NOT USED" in the feed and were retaken in round 2. The driver now retakes a frame whose top
30 rows have no top panel. This is the same kind of glitch as proxy-design.md 20 records for the product path, so it
happens on the stock route too.

## 3. Product route (batch 4) (2026-11-18 16:04-16:14 EST) [L]

Set-up: the batch-4 32-bit pin `i0ship-e96457fd-ia` (SHA-256 `3EAA3290...`) was put in with
`provision.py set-dll`. The proxy layout `x64-c29442e8-p58724e0` was then put on c5h-b with `provision.py
set-proxy`. Boot lines: proxy `3dfd7521...` and server.exe `fc73e60e...`, both `repo 58724e072 dirty=0 dll_pin
c29442e8c`; `mode=on`; server attached after 1.03 s; `[Renderer] ServerRenderer = 0 (default)`, so the EXE draws the
world and the cities. The log has no LOUD or FALLBACK line. Save, zoom and driver were as in round 2 of section 2.

- **City list:** the 124 cities of the save (owner, name, style, era, population, plot, coast, lake, hills, harbour)
  are identical on both routes (`cities_stock.txt` vs `cities_product.txt`, diff empty).
- **The 12 shots of round 2, retaken on the product route:** every pair looks as it does on stock. Footprint, core and
  harbour models are the same. Only the random block meshes and quarter turns differ, as cities-and-landmarks.md 1.12
  says they do between processes. Sheets: `C:/c5-64bit-work/0555/view/psheetA.jpg` and `psheetB.jpg`.
- PrintWindow returned one more frame without the interface and without models (Copan; the top-panel test missed it
  because of a dark map at the top). It was retaken (`product_r`) and the glitch frame is marked in the feed.

So in these 10 pairs, **the product route shows nothing that stock does not.**

### 3.1 The case that does show it: a city on a one-tile island (product route 16:12, stock route below)

The save has 30 land plots with five or six water neighbours (`islands.lua`). Two cities of player 0 (Egypt,
Middle East art, Industrial era) were founded the same way and at the same moment with `Players[0]:InitCity`, then
`SetPopulation(22, true)`. This was in memory only: the game was never saved. **Avaris** is on the one-tile island
90,16 (six water sides). **Lisht** is inland at 101,21 (no water, hills or mountain within one plot, at least 4
plots from any city). Both have population 22, the same size bucket (21-33), and no buildings. Giza (22, coastal, 2
water sides) was shot again beside them (`found.lua`; shots `F:/c5-64bit-work/0555/product_new/`, sheet
`view/new.jpg`).

- **Avaris is drawn as a tight cluster on its single hex.** At the round-2 zoom, at half scale, its blocks cover
  about 150 x 110 px. Lisht's cover about 260 x 200 px, and Giza's at least as much as Lisht's. **By area that is
  about 3 times smaller** [L, the extents measured by eye on the half-scale sheet; blocks not counted]. Both cities
  have tall centre towers of the same kind. The difference is only how far the blocks spread.
- This fits the report's "some coastal tiles ... about 3 times smaller". It also fits the mechanism of section 1, but
  the mechanism is not proven: neither the mask nor the anchors were read. Every lattice point whose anchor falls on
  water fails the terrain-value test (outside 13,001-35,000) and gets no block, and a hex with six water sides keeps
  only the anchors on its own land. A plot with water on five or six sides very likely also matches none of the 25
  layouts (the coast shapes cover one to three water edges), so the city falls back to `flat`. Both the fallback and
  the water drop are [I] here.

### 3.2 The same stimulus on the stock route (2026-11-18 16:14-16:22 EST) [L]

The proxy layout was removed (`set-proxy --original`). The game ran on the batch-4 32-bit pin `i0ship-e96457fd-ia`
alone, with the same save and a fresh load. The stimulus was the same, plus a third city **Buto** on 93,14, a plot
with five water sides (`found3.lua`). Avaris, Lisht and Buto were each set to population 22. Shots are in
`F:/c5-64bit-work/0555/stock_new/`; the sheet is `view/snew.jpg`.

- **Avaris** looks as it does on the product route: a tight cluster on its one hex, about a third of Lisht's area.
- **Buto** (five water sides) is the same: its blocks stay on its own hex and the land tongue next to it. It is about
  the size of Avaris.
- **Giza** (two water sides) spreads inland as far as Lisht.

**So the reported look reproduces on the stock route** for cities on a plot with five or six water sides. It is the
EXE's own behaviour, not something the patch does.

## 4. Conclusion (2026-11-18 16:25 EST)

- **Reproduced, and the stock game does it too.** A city on a plot with water on five or six sides (a one-tile
  island, or a tip with one land neighbour) is drawn on its own hex only. By area that is about a third of a land city
  of the same population, era and art style. Seen on both routes [L]. Cities with one to four water sides (10 pairs
  from the turn-350 save, three art styles, with harbours, on hills and flat, by ocean and lake) are drawn at a size
  comparable to their land pairs on both routes [L].
- **Trigger and frequency:** how much land is around the plot, not the population. The huge turn-350 save has 30 free
  land plots with five or six water sides. Of its 124 cities only Izapa stands on such a plot (five sides); the next
  is Liverpool with four. Izapa's island runs on for several plots past the city hex, and Izapa looks only somewhat
  smaller than Coba. So the look needs very little land around the plot, not just a count of water sides [I, from
  Buto vs Izapa]. **Stock vs product:** the same in every case (section 3).
- **Root cause, as far as it is known.** By the EXE's block placement (cities-and-landmarks.md 1.12, [C] for the
  tests), every anchor of the city's lattice whose plot cell has a terrain value outside 13,001-35,000 or a slope
  steeper than about 32 degrees gets no block. That every water plot fails the terrain-value test (the value as a
  height, water below 13,001) is [I]. It is backed by the shots: no block was seen on water in any frame, and the
  block area follows the land. A plot with five or six water sides very likely also matches no coast layout (the 25
  layouts cover one to three water edges), so the city uses the `flat` field, whose larger buckets reach far past
  its hex. That is [I]: the mask of 0x53FA50 was not read for these plots. Either way, only the anchors on land
  survive. No address was read anew in this ticket.
- **What would confirm the [I]s:** the 16-bit terrain value at a water cell and a land cell (the field 0x5BAC00
  reads, or the city-field manager 0xB9DDA0 in a dump), and the layout index each test city takes (the 6-bit mask of
  0x53FA50 for 90,16 and 93,14). A cheaper live check is to count the block instances per city in one frame capture
  (`cityinst.py` of 0337) for Avaris and Lisht.
- **Fix proposal for the MC:** none for the patch. This is stock behaviour (D66: the goal is the stock picture), and
  the server's future city renderer must keep the terrain test so that island cities stay this size
  (cities-and-landmarks.md D4c). If the community wants bigger island cities, that would be an increment 4 Extras
  visual addition, off by default (D169/D170): for example, letting the density field's anchors on water next to the
  city plot keep low-class blocks on stilts or a quay. That is a new visual, not a bug fix, so it is not proposed
  here. Suggested close: "reproduced as stock behaviour", as ticket 0554 was.
- **No crash and no box** in any of the three launches (pids 25064, 63776, 35156; dialog watcher clean at every
  step, `leftovers` clean after each stop).
- **Not verified:** the exact block counts (judged by eye from half-scale sheets); the layout each test city took;
  the full shipped system (the product route here had `ServerRenderer = 0` and no compositor); cities with walls on
  islands; the Asian and Polynesian art styles (none in the save).

## 5. Instance and restore

c5h-b ended as it started:
- CP-folder DLL back to `i0ship-26234371-ia` (`set-dll --previous`, SHA-256 `3c2f47da...`).
- Proxy layout removed.
- Front-end Lua restored by hash (`frontend.py restore`, all `original`).
- `provision.py verify c5h-b` ok, `changed_mutable` empty. Its one warning is a stale `stats.db-journal` from
  2026-11-17 18:40, older than this ticket.
- No save written: the newest files in `ModdedSaves/single` and `auto` are older than the first launch.

The claim was released at 16:23. The stimulus cities existed in memory only.
