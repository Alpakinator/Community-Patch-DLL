# Observer mode and a player switch: the minimap and the city banners (ticket 0683); un-revealed plots on the minimap (ticket 0552)

The owner's report of 2026-11-22 21:1x (0.1b3-UNTESTED test build, main 0d3114c03): after observer autoplay for a few
turns and coming back as another player, [reworded for privacy] the minimap behaved oddly near the wrap and showed what looked like
capitals of nations seen during observer mode; the screenshot (MC chat only) also had a city banner over unexplored
cloud far from the player's land. This file holds the stock comparison and the patch arm, both run on 2026-11-22.

## 1. Result

- **The minimap part is stock** [L, 2026-11-22, both arms]: with the patch off (the EXE draws everything) the EXE's own
  minimap keeps observer-revealed texels at the map's edges after the switch: a column of water down the panel's left
  edge and a strip of water and ice along its bottom, plots the new player has never seen. Because these count as known
  ground, the panel shows the whole map at full-map scale, so it is mostly the panel's background art with the new
  player's area at one side and the stale marks at the edges: the owner's report [reworded for privacy] of a nearly empty panel with marks at its edge
  near the wrap. The patch arm (the owner's build, laid by its installer) shows the same pattern, plus a top-edge row
  of ice texels and one stray water texel, with 40 more AI turns (section 3). The patch does not draw the minimap: it is the EXE's
  `MiniMap_System`, kept on since 0192 (`../02-exe-internals/disable-systems-and-masks.md`, "Minimap on in the product").
- **Capitals from observer mode on the minimap: not seen** in either arm [L]: every city mark on the panel after the
  switch was a city the new player's team has revealed in the DLL (`state.lua`'s city list). The owner's marks are
  most likely the stale edge texels above, or cities the AI had revealed for that civilisation while it played it [I].
- **A city banner over unexplored cloud: not reproduced** in either arm [L]: one switch in the stock arm, two in the
  patch arm (T60 and T71), cameras on the revealed plot nearest an unrevealed capital (Vatican City at 63,14) and on
  revealed foreign cities (Carthage, Saguntum, Ghulja, Antananarivo), and one zoomed-out view: banners only over
  revealed ground, clouds only over unrevealed ground. EUI's banner manager (`(3a) VP - EUI Compatibility
  Files/LUA/CityBannerManager.lua`, `Events.GameplaySetActivePlayer` handler) destroys the banner of every city whose
  plot the new active team has not revealed [C, read 2026-11-22]. The case is open: it needs the owner's save
  (section 5).

## 2. How the DLL switches (CvGame.cpp at the x64 source e9d0c4837) [C, read 2026-11-22]

- `CONTROL_TOGGLE_OBSERVER_MODE` (VP's observer key and `WorldView.lua`'s observer exit button, `CvGame::doControl`
  ~line 3873): not an observer -> `setAIAutoPlay(MAX_TURNS_OBSERVER_MODE or INT_MAX, active player)`; an observer ->
  `setAIAutoPlay(0, the first alive major whose turn is active)`. If no major is turn-active at that moment the toggle
  does nothing (seen once in the patch arm at T51: the toggle missed, the second try at T60 took).
- `setAIAutoPlay` on: an observer slot in a major slot, `SetAllPlotsVisible(observer team)` (every plot revealed and
  visible), `setActivePlayer(observer, ..., bAutoplaySwitch = true)`. Off: the slot goes back, `setActivePlayer(return
  player, ..., true)`.
- `setActivePlayer` (~6285), with graphics initialised: `PublishActivePlayer`, then `CvMap::updateFog` = for every
  plot `GameplayFOWChanged(x, y, new active team's mode, false)`, `updateVisibility`, and the dirty bits
  (`MinimapSection_DIRTY_BIT` among them). So the EXE is told the new fog of every plot, edge plots included; the
  stale edge texels are the EXE minimap's own (how it keeps them, e.g. wrap padding texels it paints on reveal and does
  not clear on re-fog, is not read) [I].
- The server follows the same events (`fog-of-war.md` 3.9); its clouds matched the DLL's reveal state everywhere
  looked at (section 1).

## 3. The runs (2026-11-22, c5h-new-b, EXE 26577625, VP 5.4.8 + EUI, Small Continents, quick speed)

| arm | set-up | switch | after the switch (DLL, Venice = player 2) | minimap | banners |
|---|---|---|---|---|---|
| stock | patch off: 32-bit instance-aware DLL ff0f6e86, no proxy, stock config.ini | observer at T0 (player 5, team 21, 2772/2772 plots), off at T20 | 392 plots revealed, box x 36..60 y 7..35, none in column 0/65 or row 0/41 | full-map scale; stale left column (water) and bottom strip (water, ice) | none over cloud |
| patch | installer `VP64-Patch-0.1b3-UNTESTED-main-0d3114c03-d99b7f1-for-VP5.4.8-TEST.exe` (b2d603b0...), installer defaults (ServerRenderer, Units, Cities, Landmarks, FarmFields, Combat = 1; Arc, Path = 0), player DLL VP 5.4.8's own 262aee88... | same save; observer at T0, off at T60 (first try at T51 missed); again T61 -> T71 | 710 plots revealed at T60 | the same stale edges, also a row of ice at the top and one stray water texel; still there after one more turn (T61) | none over cloud |

The save `C5H-0683-T0` (a new game made in the stock arm, sha256 d6cf96cc...) is kept in
`C:/c5-64bit-work/0683/inputs/` (not committed). Pictures: Visual Checks, issue 0683 (all DIAGNOSTIC; the camera claims of four entries corrected by `amend`);
minimap crops by `server/evidence/0683/crop_minimap.py`.

## 4. Commands (scripts in `server/evidence/0683/`)

- New game / load: `python -I -X utf8 server/evidence/0683/prep_0683.py <inst> new|load <SAVE>`, then `cli.py launch`,
  `frontend.py ingame` (exit 1 "load_screen_ready" until the click), `frontend.py dismiss`.
- Observer on and off, the owner's path, on the main thread: `python -I tools/harness/lua.py --instance <inst>
  --state InGame -f server/evidence/0683/mt_observer.lua`; read the outcome with `state.lua` (turn, autoplay, active
  player, revealed count, every city with its revealed flag). Retry the "off" toggle until autoplay reads 0: it misses
  when no major is turn-active.
- Camera: `UI.LookAt` moves only to a revealed plot (an unrevealed target, from the channel thread or the main thread,
  left the camera where it was; a click on that spot of the minimap did not move it either) [L]: `look_near.lua`
  (placeholders @X@, @Y@) puts the camera on the revealed plot nearest the target, from a one-shot
  `Events.LocalMachineAppUpdate` handler [L]. Whether `UI.LookAt` to a revealed plot works from the channel's own
  thread was not tried.
- A named save on the main thread: `mt_save.lua` (placeholder @NAME@).
- Reading EUI's banner table (`g_cityBanners`, a local) through `debug.getupvalue(RefreshCityBanner, ...)` did not
  work: `RefreshCityBanner` reads nil from the channel's `CityBannerManager` state, also through `G` [L]; not pursued
  (the script was dropped).
- The patch arm's layout: `bash server/evidence/0683/drv_0683.sh <inst> install|remove` (lead-0673's `drv.sh` with
  this ticket's inputs; remove ends with `set-dll --previous` and `verify --quick`).

## 5. Open

- The banner over cloud: needs the owner's save (or the turn-28 Karakorum game) to replay the switch; without it,
  closed as not reproduced. What would confirm a patch cause: a banner on a plot the DLL reports unrevealed for the
  active team while EUI's handler ran (a stale banner, EXE/UI side), or the server drawing cloud on a plot the DLL
  reports revealed (server fog stale); neither was seen.
- A fix for the stock minimap edge (a full minimap rebuild on an active-player change) would be an EXE-behaviour
  change; not attempted (stock, ticket scope). *(2026-11-24, ticket 0552: the EXE's minimap never marks a plot
  unexplored again after it was shown, section 6. Whether the observer's stale edges come from that is not checked: a
  player slot's block is made all unexplored when first used (6.2), so they would need a block the observer's events
  reached [I].)*

## 6. Un-revealed plots keep their minimap picture (ticket 0552, 2026-11-24)

The owner's engine-bug report (2026-11-18): when a mod or script sets plots back to unrevealed during play, the world view
follows but the EXE's minimap keeps their old picture until a reload. Branch `task-0552-minimap-refog`.

### 6.1 Result

- **Cause [C, decompile of build 4390913, Ghidra, 2026-11-24]:** the Minimap system's handler of the engine event
  `HexFOWStateChanged` acts only when the event's fog byte is not 0 (0 = unexplored in the EXE's encoding, 1 = seen,
  2 and 3 = visible; `fog-of-war.md` 3.2): it then clears the plot cell's "unexplored" bit, updates the edge bits to its
  six neighbours and widens the box of explored plots. For byte 0 it changes no cell and no box; it only sets the
  system's redraw flag. Nothing else in the system ever sets a cell's "unexplored" bit back, except building a block
  (all cells unexplored) at game start-up and when a block for a new player slot is made. So a plot that goes from
  explored back to unexplored keeps its minimap texel, its edges and its share of the minimap's scale until the next
  load. The DLL does send the event: `CvPlot::setRevealed(team, false)` for the active team calls `updateFog(true)`,
  and `CvMap::updateDeferredFog` sends `GameplayFOWChanged(x, y, FOGOFWARMODE_UNEXPLORED, false)` [C, x64 DLL source
  1b87dd185, CvPlot.cpp 12103-12356 and 502-519, CvMap.cpp 991-997].
- **Seen against visible (a re-fog of a visible plot):** the handler treats bytes 1, 2 and 3 alike, so the minimap
  never shows the difference between a visible and a seen plot; a plot that goes from visible to seen has nothing to
  update on the minimap. The bug is the unexplored case only [C].
- **Live [L, 2026-11-24, c5h-a, build 4390913]:** reproduced on the stock route and on the product route (the beta 3
  release installer) alike: after `SetRevealed(false)` on 48 plots the DLL reports none revealed, the EXE's minimap
  keeps all 48 cells explored, its box and scale unchanged; a reload clears it (6.4). The patch neither causes nor
  changes it: the EXE's Minimap system draws the panel in the product (0192).

### 6.2 What the Minimap system keeps (behaviour; addresses of build 4390913) [C]

Build 26577625's addresses of the same items come from `tools/exebuild/addr_match.py` (all eleven matched, high
confidence, 2026-11-24) and stay local (D202). *(2026-11-25, ticket 0727: a match is not a byte-equal body; the fog
handler of 26577625 is compiled differently, its behaviour re-read by a decompile, 6.6.)*

| what | 4390913 |
|---|---|
| MiniMap_System static object | 0x1A0C670 |
| app Startup: registers its ten event handlers | 0x692D80 |
| StartupGame: builds the blocks | 0x549DF0 |
| `HexFOWStateChanged` handler | 0x6A5400 |
| active-player handler (slot = the event's player, redraw all) | 0x7162E0 |
| block of a slot (made on first use) | 0x4A0AA0 |
| cell of (column, row) | 0x6E0A10 |
| active slot (dword) | 0x1A0C338 |
| list of blocks (pointer to the sentinel node) | 0x1A0C340 |
| render (from the system's render entry 0x72C310) | 0x46E550 |

- **One block per player slot** (up to 64), in a list of nodes (next, prev, slot, then the block); the active one is
  the slot of the last active-player event. In a block: width +0x94, height +0x98, a wrap byte +0x9C, the cells from
  +0xA0 (4 bytes each, 256 per row, index = row x 256 + column; out-of-range reads go to one spare cell at +0x400A0),
  the box of explored plots +0x400B0..+0x400C4 (six ints: first plot, then the low and high corner) and a byte
  +0x400C8 "no explored plot yet". A cell's byte 2: bit 0 = unexplored, bits 2-7 = "my neighbour in direction n
  differs" (the edge drawing).
- **The fog handler** (event record: hex x +4, hex y +6 as shorts, fog byte +8, "every plot" byte +9, as FluidFOW's,
  `fog-of-war.md` 3.2): fog byte & 3 = 0 -> nothing but the redraw flag. Otherwise, one plot: the hex is turned into
  (column, row), bit 0 is cleared, the box widened (with the wrap rule when the map wraps), the six edge bits of the
  cell and of its neighbours recomputed; "every plot": every cell's bit 0 and edge bits cleared and the box set to the
  whole map. The box is never made smaller by anything but a new block.
- **A new block** (first event for a slot) copies the first block's cells and sets every cell's byte 2 to 1
  (unexplored, no edges).
- The scale the minimap draws at follows the box: the render's view step (4390913 0x48B3F0, called from the render
  when its redraw bits ask for it) turns the box's corners into world positions, pads them and fits the panel's
  aspect, and that rectangle is what the panel shows [C]. So un-revealing the plots that made the box wide leaves the
  minimap zoomed out: "minimap scale not resetting on programmatic fog changes" (`research/sources/REVIEW.md`) is this
  bug [C for the box's use, I that REVIEW.md meant this].

### 6.3 The set-up (scripts in `server/evidence/0552/`)

- `mm_block.lua` (main thread, one-shot `Events.LocalMachineAppUpdate` handler; `@MODE@` = `reveal` or `unreveal`):
  an 8 x 6 block half a map away from the active player's start plot; `reveal` remembers the plots the team has not
  revealed and reveals them (`Plot:SetRevealed(team, true)`), `unreveal` sets exactly those back
  (`Plot:SetRevealed(team, false)`); both end with `Map.UpdateDeferredFog()`. `mm_state.lua` reads the counts.
- `mm_cells.py --pid P --build 4390913 [--block X0 Y0 W H]` (another build: `--addrs SLOT_VA LIST_VA` from the local
  address map): reads the active block's cells from the running EXE (read only; counts and fields, no code bytes) and
  counts the cells marked unexplored.
- `drv_0552.sh <inst> inputs|snap|new|load|reveal|unreveal|state|cells|shot|save|stop|install|remove` (0716's driver
  with these steps; install/remove = the beta 3 release installer).

### 6.4 Live (c5h-a, build 4390913, 2026-11-24 20:50-21:05 EST) [L]

Both arms on c5h-a (EXE build 4390913, VP 5.4.8 + EUI), one new Small Continents game (66 x 42, wrapping), saved at T0
as `C5H-0552-T0` (sha256 79196993...) and loaded again for the product arm. The block: 8 x 6 plots at (42..49, 20..25),
48 plots, none revealed before. "EXE cells" = `mm_cells.py` on the running EXE (loaded at 0x50000, not at its ImageBase:
the reader moves the addresses to the loaded base).

| arm | step | DLL: plots revealed (block) | EXE cells unexplored (block) | box (first; low; high, column/row) | minimap |
|---|---|---|---|---|---|
| stock: patch off, the instance's own 32-bit instance-aware DLL `3c2f47da` (batch 2 pin), no proxy | T0 | 23 | 2749 of 2772 | 11,23; 11,21; 17,25 | the start area, zoomed in |
| | reveal | 71 (48 of 48) | 2701 (0 of 48) | 11,23; 11,20; 49,25 | the block drawn far right, zoomed out |
| | un-reveal | 23 (0 of 48) | **2701 (0 of 48)** | **11,23; 11,20; 49,25** | **unchanged: the block still drawn, still zoomed out** |
| | save `C5H-0552-UNREV`, stop, reload | 23 | 2749 (48 of 48) | 11,23; 11,21; 17,25 | equal to T0 |
| product: the beta 3 release installer `6da3170e...` with its defaults (ServerRenderer, Units, Cities, Landmarks, FarmFields, Combat = 1), the tag DLL 262aee88 as the player's | T0 (the same save) | 23 | 2749 of 2772 | 11,23; 11,21; 17,25 | as stock |
| | reveal | 71 (48 of 48) | 2701 (0 of 48) | 11,23; 11,20; 49,25 | as stock |
| | un-reveal | 23 (0 of 48) | **2701 (0 of 48)** | **11,23; 11,20; 49,25** | **as stock: the block still drawn, still zoomed out** |

- The bug is the EXE's own, the same with the patch: after the un-reveal the DLL reports none of the 48 plots revealed,
  while the EXE's minimap keeps all 48 cells explored and its box (so its scale) as after the reveal, exactly as 6.1
  reads it. A reload rebuilds the block from the save's state (only the 23 start plots).
- The cell index = row x 256 + column with column = the plot's x and row = its y (the block's 48 cells are the ones
  the reveal cleared) [L].
- Not checked: the world view at the block (the camera cannot be sent to an unrevealed plot, 0683's trap), a plot that
  goes from visible to unexplored (one a unit sees), build 26577625 live, Lua `SetRevealed` from the channel's own
  thread (both runs used the main thread). *(Corrected 2026-11-25 by ticket 0727: this line said "the handler's bytes
  match 4390913's [C by addr_match only]"; addr_match carried the handler by its first 16 bytes, but the body differs
  from +0x3D on (registers and order); its behaviour and offsets are the same by a decompile of build 26577625, 6.6.)*
- Pictures: Visual Checks issue 0552 (all DIAGNOSTIC): whole-window shots and 3x minimap crops for T0, reveal, un-reveal
  (result fail = bug shown) and the reload; files under `F:/c5-64bit-work/0552/shots/`. Saves and logs under
  `C:/c5-64bit-work/0552/` (not committed). The instance was restored: installer removal exit 0 with 0 differences
  outside the run outputs, DLL back to `3c2f47da`, saves by hash, `provision.py verify --quick` ok.

### 6.5 Fix routes (to the MC before any code, as the ticket asks)

The EXE itself offers no call that marks a cell unexplored (6.2), so a fix either writes the minimap's own heap data
(D165: plain data on the heap and in .data) or replaces the picture (0377).

| route | what | means | where it applies | size |
|---|---|---|---|---|
| R1 | The proxy marks the cells: right after the EXE served a carried `GameplayFOWChanged(x, y, FOGOFWARMODE_UNEXPLORED)` (the seam `proxy_rt.cpp` `OnCall` already uses for `ProxyCombatHold::AfterServed`) it notes the plot; at the next frame on the main thread (the Lua bridge's frame hook; that the minimap's own handlers run there too, after the system's deferral queue, is [I] and the first thing R1 must check) it sets the cell's bit 0, recomputes the edge bits of the cell and its six neighbours, recomputes the box from all cells (the handler's rule, with wrap) and sets the system's redraw bits (+0x1C bits 0 and 2). Behind `[Extras] FixMinimapUnexplored` (a bugfix extra, default 1 under `EnableBugfixes`, D169). Fail loud (D118): layout checked per build (4390913, 26577625; the two .data names from the build table), else off with one LOUD line. | heap and .data writes only; no EXE function called, no code byte | every product route (the proxy is loaded); not the stock route | 6-10 h with offline tests and a live pair |
| R2 | Close into 0377: the server draws the minimap from its own plot records, so the current fog is drawn by construction; 0377's acceptance already names "0552's re-fog case shows the current fog". | the server's own drawing | only with `[Renderer] ServerMinimap = 1` (0377's default is 0); 0377 is blocked by D71 | in 0377's 20-35 h |
| R3 | Have the DLL re-send every plot after an un-reveal | none | does not work: the handler ignores byte 0 whatever sends it | - |

Files R1 would touch (named for the MC; nothing written): `proxy/src/proxy_minimap_fog.h` and `.cpp` (new: core on a
target of plain addresses, binding to the build table, glue), `proxy/src/proxy_rt.cpp` (one `AfterServed` call for
`GameplayFOWChanged`), `proxy/src/proxy_ui_bridge_rt.cpp` (the frame-hook apply), `proxy/build_proxy.py` (source list),
`exebuild/symbols/proxy.tsv` (two names: the active slot and the block list, anchors 0x1A0C338 and 0x1A0C340),
`options/include/vp64_options.h`, `options/src/vp64_options.cpp`, `options/tests/`, `docs/OPTIONS.md`
(`[Extras] FixMinimapUnexplored`), `proxy/tests/minimap_fog/` (offline core test, also under ASan), this file.

**Recommendation (impl-0552, 2026-11-24):** R1 as a bugfix extra, because R2 fixes it only once 0377 lands and its
`ServerMinimap` is switched on (default 0, blocked by D71), and the cost of R1 is small and bounded. Who un-reveals
plots during play [C, read 2026-11-24 in the x64 source 1b87dd185, for calls with a literal false; calls passing a
variable were not traced]: the DLL itself only before graphics exist (`CvGame` map regeneration,
`setRevealedPlots(team, false)`); in Lua, a mod or scenario script, and the Community
Patch's own `InGame.lua` (Overrides, line 117): Ctrl+Z with the debug flag toggles debug mode and sets every plot
unrevealed, then revealed again only in debug mode, so leaving debug mode leaves the whole map on the minimap. If the
MC prefers to wait for 0377, this ticket closes into it: 0377's acceptance already names the case, and `mm_block.lua`
plus `mm_cells.py` are its test.

*(2026-11-25: the MC chose R1 on 2026-11-24 (ticket 0727). **R1 landed** on branch `task-0727-minimap-unexplored`
(ticket 0727): `proxy/src/proxy_minimap_fog.*`, `[Extras] FixMinimapUnexplored` (id 33, a bugfix, default 1). The
handler's rules it follows are in 6.6, the fix as built in 6.7, its live pair in 6.8. Four .data names, not two: the
system object (its defer and redraw words) and the handler's wrap byte as well.)*

### 6.6 The fog handler's rules, both builds (ticket 0727, 2026-11-25) [C]

Read for R1 from the decompile of build 4390913 that 0552 made (handler 0x6A5400, cell lookup 0x6E0A10, block of a
slot 0x4A0AA0, active-player handler 0x7162E0; scratch `C:/c5-64bit-work/0552/d*.txt`, not committed, D83), the
direction table's 18 values read from the golden copy's `.data`, and for build 26577625 a decompile of its fog handler
and cell lookup in a scratch Ghidra project imported without analysis (`C:/c5-64bit-work/0727/g26/`, read only, not
committed; the commands are in the runbook, section 3, "headless Ghidra read of build 26577625").

- **The system object** (MiniMap_System, 4390913 0x1A0C670): every handler first tests byte +0x18 bit 0; when set it
  queues the event (an engine call that keeps it for later) and returns. +0x1C is the redraw word: the fog handler ORs
  bit 0 (cells) after every event and bit 2 (the box changed) when it changes the box; the active-player handler writes
  0xFFFFFFFF; the render entry (0x72C310) draws when the word is not 0, counts a frame and clears it.
- **The block list's owner** starts at the active slot's dword (0x1A0C338): +0 active slot, +8 the pointer to the
  sentinel node (0x1A0C340), +0xC the node count. The block of a slot walks the list and returns node + 0xC; a slot
  without a block gets a new one appended (cells copied from the first block, every cell's byte 2 set to 1).
- **The block's transform**: the block's first 16 ints are a 4 x 4 matrix the handler applies to the event's cube
  coordinates (x, y, -x-y) to get (q, r); then column = q + (r >> 1) (arithmetic shift), row = r. The matrix is not
  the identity: on build 26577625 a game's block holds q = -y - z and r = -x - z (rows (0, -1, -1, 0) and (-1, 0, -1, 0),
  the third (-1, -1, 0, 0), translation 0) [L, mm_check.py, c5h-new-a, 2026-11-25], which for z = -x-y is (q, r) =
  (x, y). The fix requires exactly that (checked at every apply: m0 - m8 = 1, m4 - m8 = 0, m1 - m9 = 0, m5 - m9 = 1,
  no translation); 6.4's live reading, column = plot x and row = plot y, is what it gives. *(2026-11-25: the first
  build of the fix required the identity matrix and refused live with `minimap-transform`, 6.8.)*
- **The cell lookup** (block, column, row): when the block's wrap byte (+0x9C) is set or the column is in 0..width-1,
  and the row is in 0..height-1, the cell at +0xA0 + (row x 256 + column) x 4, with the column taken modulo the width
  when the block wraps; otherwise the spare cell at +0x400A0.
- **One plot, an explored state** (fog byte & 3 not 0, "every plot" byte 0): the cell's flag byte (byte 2) loses bit 0;
  when the block's "no plot yet" byte (+0x400C8) is set, the first plot (+0x400B0, +0x400B4) and both corners are set to
  this cell, redraw bit 2 set, the byte cleared. Rows: the high row (+0x400C4) is raised to the row when the row is not
  below it, the low row (+0x400BC) lowered when the row is not above it (redraw bit 2 each time, also when equal).
  Columns, map not wrapping (the byte at 0x1A0C568, set from the map's wrap flag at the game's start): the low column
  (+0x400B8) lowered and the high column (+0x400C0) raised as needed. Map wrapping: with column c and the first plot's
  column f: when c < f, the alternative is c + width; when c is not right of the low column and the high column is not
  right of the alternative (neither form inside the box), the box grows to the nearer side: the low column to c when
  (low - c) < (alternative - high), else the high column to the alternative. When c >= f, the alternative is c - width;
  when the high column is not right of c and the alternative is not right of the low column, the high column goes to c
  when (c - high) < (low - alternative), else the low column to the alternative. So a wrapping box may run below 0 or
  past width - 1, and it always holds the first plot's column.
- **The edges**: for each of the six directions d = 0..5, in cube coordinates (0, 1, -1), (-1, 0, 1), (1, -1, 0),
  (0, -1, 1), (1, 0, -1), (-1, 1, 0), the neighbour's cube coordinates go through the same transform and lookup; when
  the cell's and the neighbour's bit 0 are equal, the cell's bit d + 2 and the neighbour's bit ((d + 3) mod 6) + 2 are
  cleared, else both set. After the six, the spare cell's byte 2 keeps only its bit 0.
- **Every plot** ("every plot" byte set, an explored state): every cell's byte 2 cleared (explored, no edges), the box set
  to (0, 0) .. (width - 1, height - 1); the first plot and the "no plot yet" byte are not touched.
- **The unexplored state** (fog byte & 3 = 0): only the spare's edge bits cleared and redraw bit 0 set; no cell, no box.
- **Build 26577625** [C, decompile and byte compare, 2026-11-25]: the fog handler is compiled differently from +0x3D on
  (other registers and order; 838 bytes, not 839), with the same offsets, constants, bit tests and rules as above, and
  without 4390913's one extra test near its start (a call when the system's first word is 0x23 and the next word's low
  nibble is 2; the callee was not read, and the fix does not depend on it). It names the same three items (the active
  slot, the wrap byte, the direction table). The cell lookup, the block of a slot, the active-player handler, the view
  step 0x48B3F0 and the render entry are byte-equal to 4390913's outside relocations and branch targets; the direction
  table's 18 values are equal. Check: `python -I proxy/tests/minimap_fog/layout_check.py --old <4390913 EXE> --new
  <26577625 EXE>` -> `layout_check: PASS (0 failed)` (prints counts and verdicts only, D83/D202).
- Not read: when the system sets its defer bit (+0x18) and when the queued events are run again; where the main thread
  dispatches the fog events relative to the Lua `LocalMachineAppUpdate` listeners. The fix does not depend on either
  (6.7).

### 6.7 The fix as built (ticket 0727, `proxy/src/proxy_minimap_fog.*`, 2026-11-25)

- **The note** (`ProxyMinimapFog::AfterServed`, from `proxy_rt.cpp` `OnCall` right after the combat hold's, on the
  thread that served the call): for a served `ICvEngineUtility{1..4}::GameplayFOWChanged` the arguments are read from
  the call's private copy (handle, x, y, mode, setAll). DLL mode 1 (`FOGOFWARMODE_UNEXPLORED`; 0 is OFF = visible, 2 is
  NOVIS = seen, `CvEnums.h`) notes the plot for `kApplyFrames` = 8 frames; any other mode for the same plot forgets
  the note (the served order is the EXE's queue order, so a later reveal wins). setAll does the same for every plot.
- **The apply** (`OnFrame`, the last link of the Lua bridge's frame-hook chain, `proxy_ui_bridge_rt.cpp` Init: on the
  main thread, once per in-game frame): nothing while no plot is noted. Else, when the defer bit is clear: the active
  block (slot, list walk of at most 80 nodes, the block readable and writable, size 1..256 x 1..256, both wrap bytes 0
  or 1, the transform taking (x, y, -x-y) to (x, y)); each noted plot loses one frame; a plot whose cell is explored gets bit 0 set and the
  handler's edge rule with its six neighbours (6.6). When a cell changed: the box is recomputed from all cells, the
  spare's edge bits cleared, redraw bits 0 and 2 set (compare-exchange). Applying each plot at 8 frames in a row makes
  the order of the EXE's queue and the frame hook irrelevant: if the EXE's handler runs an earlier reveal of the same
  plot after the first apply (the reveal served before the un-reveal, its event still queued), the next frame marks the
  cell again (offline case `CaseLateReveal`).
- **The box after a re-fog** (the handler can only grow it): rows from the lowest to the highest explored row;
  columns from the lowest to the highest explored column, or, on a wrapping map, the shortest run of columns round the
  map that holds every explored column (the width minus the longest run of empty columns, the first such run from the
  first plot's column on), written so that it holds the first plot's column (so it may run below 0 or past the width, as
  the handler's). The first plot stays while explored, else the first explored cell by rows. No explored cell left: the
  "no plot yet" byte is set and the box left as it is (the next reveal starts a new box, as in a new block). This is
  the tightest box the handler's rule allows, not necessarily the one a reload builds (a reload replays every plot in
  the DLL's order).
- **Checks, fail loud** (D118; one LOUD line in vpproxy.log `minimap fog: OFF, check <name> failed: ...`, then off for
  the proxy load; no report, no popup, the minimap then as in stock): `minimap-build` (unknown build), `minimap-layout`
  (a build not in `kLayoutVerified`), `minimap-symbol` (one of the four names not carried), `minimap-data` (the .data
  unreadable or the redraw word not writable), at an apply `minimap-slot`, `minimap-list`, `minimap-block`,
  `minimap-wrap`, `minimap-transform`. Reads go through VirtualQuery before the read: clang-cl's `__try` does not catch
  a fault raised in its own body (the offline test's unreadable page crashed the clang build until then, 2026-11-25).
- **Option**: `[Extras] FixMinimapUnexplored` (id 33, kCatBugfix, default 1, needs `EnableBugfixes = 1`), read once at
  the bridge's Init (`Vp64Opt::Extra`); off = `minimap fog: off (FixMinimapUnexplored not in effect: ...)` and nothing
  noted. On = `minimap fog: on (FixMinimapUnexplored; <build>, layout: ...)`.
- **Log**: one line per apply that changed cells (`minimap fog: slot S: N cell(s) marked unexplored ...; box first ..
  low .. high ..`, the first 40), and the counters line with the bridge's stats every 3,600 Updates once a plot was
  noted.
- **Tests** (2026-11-25, workstation): `python -I proxy/tests/minimap_fog/test_minimap_fog.py --scratch <dir>` (through
  `quietlock.py run`, about 1 min): `test_minimap_fog: 15/15 passed` (core 128/128 and binding 152/152 checks, each with
  the proxy's toolchain and MSVC x86 ASan, the runner's negative `--expect-fail` and the ASan canary). Cases: off; every
  arm refusal; 0552's 8 x 6 block revealed then un-revealed (cells, edges, box, redraw bits, log line); a hole and the
  first plot moved; nothing explored left; a later mode for the same plot; setAll; out of range; the late reveal; the
  defer bit; a wrapping map (edges across the seam, boxes round it both ways); the active slot switched and a slot
  without a block; every block check refused with one LOUD line; Disarm; the chain; two noting threads racing the apply.
  The edge check uses the test's own neighbour table per row parity, the fake handler the rules of 6.6. Options:
  `test_options` 1020/1020 in both trees against `docs/OPTIONS.md` with the entry
  (`proxy/tests/minimap_fog/OPTIONS-md-entry.txt`); 1019/1020 against the document without it (the docs check: the MC
  writes the entry at merge). Installer page schema: `test_options_schema.py` 22/22 (the row comes from the table).
  `proxy/tools/test_proxy_rt.py --build <this branch's build_server out>`: 235/235 (156 s).

  The fake blocks carry build 26577625's live transform (and, in a second run of the reveal case, the plain identity).
  `layout_check.py` also checks `kDirections` of `proxy_minimap_fog.h` against the golden 4390913 table; its `--break`
  run (the handler compared byte for byte, one direction value changed) fails exactly those three checks (exit 1).

### 6.8 The live pair (ticket 0727, c5h-new-a, build 26577625, 2026-11-25)

Instance c5h-new-a (Goldberg, EXE build 26577625, VP 5.4.8 + EUI), 0552's save `C5H-0552-T0` (sha256 79196993...,
put in by hash and taken out again), the product layout of this branch laid by `lay.py prep` (build_server output on
the 0.1b pins, the composite of this branch's shell build, `helper on`) with VP64Options.ini = the beta 3 installer's
renderer defaults (ServerRenderer, ServerUnits, ServerCities, ServerLandmarks, ServerFarmFields, ServerCombat = 1) and
`[Extras] FixMinimapUnexplored` as named. The block as in 6.4: 8 x 6 plots at (42..49, 20..25), 48 plots. Driver:
`proxy/tests/minimap_fog/live/drv_0727.sh` (phases in its header); cells by `proxy/tests/minimap_fog/mm_check.py`.

- **First build (identity check), FixMinimapUnexplored = 1, 06:14-06:16 EST [L]:** `minimap fog: on (...; Steam build
  26577625 ...)` at the boot; the reveal: 71 plots revealed, EXE cells 2701 unexplored (0 of 48 in the block), box low
  11,20 high 49,25; the un-reveal: the proxy noted all 48 served `GameplayFOWChanged(unexplored)` and refused at the first
  apply, `LOUD: minimap fog: OFF, check minimap-transform failed: ... (q row 0 -1 -1 0, r row -1 0 -1 0)`, writing
  nothing: the cells and box stayed as after the reveal (the stock bug), the game went on. This was the fail-loud path
  live; the check was too strict (6.6, "the block's transform") and was corrected. The edge bits of every cell matched
  the reader's neighbour table at T0, after the reveal and after the un-reveal (0 of 16,632 inconsistent), on a
  wrapping map: the directions and the wrap of 6.6 hold live [L].
- **Corrected build, FixMinimapUnexplored = 1, 06:39-06:44 EST [L]** (`server-build2`, the code of 28d58523b): at T0
  2749 of 2772 cells unexplored, box low 11,21 high 17,25; after the reveal 2701 (the block 0 of 48), box low 11,20 high
  49,25, the block drawn far right and the minimap zoomed out (as stock); after the un-reveal, within the 2 s before the
  read: **2749 unexplored (the block 48 of 48), box first 11,23 low 11,21 high 17,25: equal to T0, and to what a reload
  gives (6.4)**; the minimap picture equal to the T0 picture (block gone, view back on the start area). The proxy's log:
  `minimap fog: slot 0: 48 cell(s) marked unexplored (of 48 this frame); box first 11,23 low 11,21 high 17,25`, one
  line, at the first frame after the un-reveal. Edge bits: 0 of 16,632 inconsistent at every read.
- **FixMinimapUnexplored = 0, the same layout, 06:47-06:48 EST [L]:** `minimap fog: off (FixMinimapUnexplored not in
  effect: FixMinimapUnexplored = 0)`; after the un-reveal 2701 unexplored (the block 0 of 48), box low 11,20 high
  49,25: the stock behaviour of 6.4, unchanged.
- Pictures (all DIAGNOSTIC, D112: a product layout laid by `lay.py`, not the installer; the minimap crop is the EXE's own
  panel): Visual Checks issue 0727, `F:/c5-64bit-work/0727/shots/c5h-new-a-{t0-fix1,reveal-fix1,unreveal-fix1,
  unreveal-key0}.png` and their `-minimap` crops. Outputs (state files, checks) in `C:/c5-64bit-work/0727/live2/`
  (not committed). The instance was restored: `lay.py restore` ok (verify ok), the saves by hash (48 files, the T0 copy
  moved out), no box.
- **Build 4390913, FixMinimapUnexplored = 1, c5h-c, 06:53-06:58 EST [L]** (the same layout build; c5h-b was not used:
  its `VP64/` folder held another run's `reports/`, so `lay.py prep` refused, and the T0 copy was taken out by hash): the
  block's transform is the same matrix as on 26577625; T0 box low 11,21 high 17,25; after the reveal 2701 unexplored,
  box 11,20..49,25; after the un-reveal 2749 (the block 48 of 48), box first 11,23 low 11,21 high 17,25, edges 0 of
  16,632 inconsistent; the proxy's log `minimap fog: on (...; Steam build 4390913 ...)` and one `slot 0: 48 cell(s)
  marked unexplored` line. Restored: `lay.py restore` ok, saves by hash (153 files), no box. Pictures as above,
  `c5h-c-{reveal-fix1,unreveal-fix1}`.
- **Not checked live**: a plot that goes from visible to unexplored (one a unit sees; the note and the apply do not
  depend on the earlier state), the setAll form (the DLL never sends it, `CvPlot::updateFog` only), the defer bit set
  (never seen set: it read 0 at every check), an active-player switch (observer mode, sections 1-5) with the fix on, the
  installer-laid layout (the fix is in the proxy, which the installer lays unchanged).
