# Human-input (HI) testing: driving the game by mouse and keyboard only

Epic 0392 (decision D156, the owner through the Architect, 2026-11-16). The design is the Architect's proposal
`research/07-architecture-decisions/architect-2026-11-16-human-input-test-harness-epic.md`; the interface is
`tools/hi/CONTRACT.md`. This file holds the facts the harness rests on and what each work package measured.
Marks: `[C]` read in code or records, `[L]` verified live, `[I]` inferred.

Section owners (one writer per section): 0-3 and 11 the lead (4500); 12 W7 (4510); 13 W8 (4511); 14 W9 (4600, epic 0398); 15 W10 (4601 and the phase run, epic 0398; lead-0398); 4 the live probes (4501); 5 W1 (posted
actions); 6 W2 (judge and UI map); 7 W3 (trace, replay, stock control); 8 W4 (release journeys); 9 W5 (runners and
explorers); 10 W6 (real-input lane).

## 0. Status

- 2026-11-16 01:33 EST: file opened by the lead of 0392; the contract is being drafted (C1 not yet reviewed).
- 2026-11-16 01:38 EST: contract v1 drafted (section 3); probes 4501 started (section 4).
- 2026-11-16 02:28 EST: C1 accepted by the MC (queue #2212, contract v1.1); probes 4501 done (section 4); W1 (4502) and W2 (4503) started.

## 1. What the harness builds on

See the proposal's section 3 for the table of existing pieces. Additions found by the lead while drafting the contract
are in section 2.

## 2. Facts the contract rests on (lead, 2026-11-16) `[C]` unless marked

- **The Lua channel is silent in the front end and while loading** (run-and-drive-the-game.md section 3; it is
  polled in the in-game `Update`). The judge therefore has no reads before a game is loaded; the front-end part of a
  journey is judged by pictures and files only (probe 4.6).
- **The channel runs on the game-core thread holding the Lua lock**: no unvalidated ids and no window-touching engine
  calls in a judge chunk, or the game freezes on an assert box or deadlocks (run-and-drive-the-game.md section 3).
- **PrintWindow on the joined product path is not the player's frame for a large share of shots**: 87 of 150 product
  samples were glitch frames (flat grey 84,84,84, or the server's world without the EXE's UI) against 22 of 151 on
  stock (`research/02-exe-internals/disable-systems-and-masks.md`, ticket 0172 entry; overlay-system.md 12.2: 24 of 33
  rejected in one run) `[L]`. The composite's back-buffer dump (`shell_composite.shot` trigger file beside the DLL,
  BMP into the user folder's `Logs`) is the frame as presented. A picture check (all black, single colour, frozen)
  on raw PrintWindow frames of the product route would report the capture's glitch as a fault, so the picture
  source is part of the contract (probe 4.7).
- **The safety scan forbids synthetic input anywhere** (`SendInput`, `keybd_event`, `mouse_event`) except named
  real-input sites fenced off the workstation by host name (`tools/harness/tests/test_safety_static.py`,
  `REAL_INPUT_OFF_WORKSTATION`), and it forbids **posting a key or mouse message in a function that also looks dialogs
  up** (`scan_dialog_presses`). The HI step's box check and its posting therefore live in separate functions; the box
  check reads the dialog watcher's marker (`dialogwatch.marker_dialog`) before each action.
- **Hex picking follows posted mouse moves** (`UI.GetMouseOverHex`, 18 of 18 against the camera unprojected on the
  plane z = +12, camera-and-picking.md) `[L]`; picking uses a flat plane, not the terrain mesh.
- **A left click on a unit recentres the camera**, and the game reselects the next unit that needs orders
  (run-and-drive-the-game.md 4.1) `[L]`: a recorded plot target can move on screen between record and replay, which
  is why the trace stores plots, not pixels, where it can.
- **Key panning** (posted arrow keys held) runs about 1,900 px/s at zoom 17 (camera-and-picking.md, W7) `[L]`.

## 3. The contract (summary)

`tools/hi/CONTRACT.md` v1 (draft for C1, 2026-11-16 01:38 EST). In short `[I]` until each package proves it:
one CLI (`tools/hi/hi.py`) that any agent on either account runs; a session per claimed instance and ticket; a step
is a batch of posted actions (move, click, double-click, drag, wheel, key, hold, type, hover, wait) then one picture
of a fixed width (1280 by default) whose pixels are the next step's coordinates; the box check before every action
(exit 8) and the deny list before every click and key on the workstation (exit 2); the judge is a fixed set of
read-only chunks, statically scanned for writing calls, with no runner-supplied Lua, answering "not available" in the
front end, where a picture judge takes over; pure mode removes the judge from the runner only; the trace stores each
hit as a control path, a plot or a pixel fraction, so a replay re-resolves it; journeys are committed traces without
pictures (D83); findings become reports only after 2 of 2 reproductions and a stock replay; exit codes 0-6, 8, 9.
v1.1 (C1 accepted, MC #2212): the product route uses the composite dump once the mods are active, PrintWindow before and on stock; control rectangles by hover search (no `GetScreenOffset` in this EXE); the front end judged by pictures only (section 4).

## 4. Live probes for the contract (4501)

**Set-up of every probe (2026-11-16 01:47-02:23 EST, impl-4501, account B) [L].** Instance c5h-a (C:, Goldberg,
claimed `--ticket 0392 --address impl-4501`), launched only by `cli.py launch` (dialog watcher on; no box at any
point), frame cap 30 fps (0157). Three launches:
1. **Stock route**, 01:47-02:09: the instance's own DLL `i0ship-26234371-ia` (3C2F47DA...), client 2554x1411
   (`WindowResX/Y` 2560x1440), front end prepared by `frontend.py prepare --mods cp,vp,eui` (activates the mods,
   loads nothing), then the whole way into the game by posted clicks: main menu, SINGLE PLAYER, SET UP GAME and back,
   LOAD GAME, the save `HI4501-TINY_0008` (copy of c5h-a's `C5H-0167-TINY_0008`, SHA-256 b2ceb663..., turn 9,
   Polynesia), Load Game, Continue Your Journey. Units put to sleep and the auto unit cycle turned off through the
   channel before the in-game probes (run-and-drive 4.1).
2. **Product route**, 02:10-02:18: `shell/evidence/3308/run_3308.py`'s prep imported unchanged (scratch wrapper) on the
   same Tiny save as `HI4501-PP-TINY_0008`: pins `x64-04b1211b-pfdee656` (DLL 29F58E17..., server 5B254430..., proxy
   75C40CEC..., hashes checked after the prep), `shell_composite.dll` F6BA4926... built by `shell\build.cmd` from
   `git archive` of fdee65668 (the repository commit of the pinned layout), `[Renderer] ServerRenderer = 1`, D33 with
   Overlay kept on, 3308's composite keys (ring + fence, lockstep, depth). The same posted walk through the front end.
   Restored by 3308's restore (backup by SHA-256, layout removed, save copy removed).
3. **Stock route at client 1914x1051** (`WindowResX/Y` 1920x1080 written before the launch, file backed up by SHA-256
   and restored by hash after), 02:18-02:22, auto-load of `HI4501-TINY_0008`, then posted input.
Every action was a posted window message (`tools/harness/winapi.post` in a scratch script; the focus guard told the
virtual cursor and held buttons as `input.py` does); the Lua channel only read, apart from the scene set-up. Scratch
scripts in `C:/c5-64bit-work/0392/4501/` (not committed); pictures `F:/c5-64bit-work/0392/4501/pics/`, the described
ones in the Visual Checks feed under issue 4501.

**Trap met on the way [L].** run_3308's prep backs up the standard files (front-end Lua included) while the front end
is patched, and its restore calls `frontend.py restore` before the backup restore, so the patched `MainMenu.lua` came
back (`provision.py verify`: `install: size Assets\UI\FrontEnd\MainMenu.lua`). A second `frontend.py restore` after it
put the original back. Run `frontend.py restore` last, or prepare the front end only after the product prep.

### 4.1 Double-click

**Answer: there is nothing to double-click in the shipped mod set with touch off. A `double_click` action is not
needed for any journey. If one is kept, post down, up, `WM_LBUTTONDBLCLK`, up.**
- The game window's class `WinClass_FXS` has style 0x2b, **`CS_DBLCLKS` set** (`GetClassLongW(GCL_STYLE)`), and the
  system double-click time is 500 ms [L]. A real double-click therefore arrives as down, up, `WM_LBUTTONDBLCLK`, up.
- Every double-click handler in the install's and the active mods' UI Lua is inside `if UI.IsTouchScreenEnabled()`
  [C, read at run time from c5h-a's install and MODS, 2026-11-16]: `NotificationPanel.lua` (stock, both expansions,
  EUI's copy in `(3a) VP - EUI Compatibility Files/LUA`), `Mouse.eLDblClick` -> right-click handler, and `WorldView.lua`
  (stock, CP and VP overrides), `MouseEvents.LButtonDoubleClick` in selection mode -> move-to mode. No XML declares a
  double-click callback.
- A posted down, up, `WM_LBUTTONDBLCLK`, up on a Load Game list item (`HI4501-TINY_0008`) selected it and loaded
  nothing (picture) [L]: the list has no double-click handler, and the double-click message did not undo the selection.

### 4.2 Drag

**Answer: works. Left down, moves with `MK_LBUTTON`, up pans the map about 1:1 horizontally. The focus guard's
held-button report is not needed. A drag on a scroll bar moves it. A drag that starts on a unit selects the unit
instead of panning.**
- Map, stock, 2554x1411: 20 posted moves of 20 px at 16 ms (0.56 s). A drag of 400 px to the right moved the picture
  by +392 px (search on an 8 px grid); the hex under the client centre went from (51,14) to (50,14). The same drag back
  with the guard's `hold` disabled moved it by -384 px and back to (51,14): **the button state is read from the
  messages, not from `GetAsyncKeyState`** [L]. A vertical drag of 300 px upward moved the picture by about -216 px
  (the tilted camera, so not uniform; the fit error was high) [L].
- A drag that started on the Warrior's plot (1200,600) did not pan and left unit 1002 selected [L]. Start map drags on
  an empty plot.
- The Save Game list's scroll-bar thumb was dragged by 315 px with the same message sequence: `ScrollPanel:GetScrollValue()`
  went 0 -> 1 and the list showed its end (picture) [L]. Sliders were not tried (the options' volume sliders apply
  live; not worth the risk).

### 4.3 Typed text

**Answer: works with `WM_CHAR` only. Type each character as one posted `WM_CHAR`; erase with posted `VK_BACK`
key-downs; move the caret with key-downs (End, Home, Left, Delete). Never post a key-down together with its `WM_CHAR`.
There is no select-all.** Box: `/InGame/GameMenu/SaveMenu/NameBox`, prefilled `Kamehameha_0009 BC-3640`, caret at
the start, focus true. Each step was read back through the channel with `GetText()` [L]:
- `WM_CHAR 'X'` alone inserts X. A key-down 'Y' plus `WM_CHAR 'Y'` plus key-up gave one 'Y' and one 'y'. A bare posted
  key-down of a letter types it: 'A' gave 'a', SHIFT+B gave 'b', CTRL+A and CTRL+C gave 'a' and 'c'. The game's own
  `TranslateMessage` makes a `WM_CHAR` from a posted key-down using the thread's real keyboard state, so a posted
  Shift or Ctrl has no effect [L for the behaviour; I for the mechanism].
- Backspace: a posted `VK_BACK` key-down deletes one character (translated to `WM_CHAR 8`); a posted `WM_CHAR 8` alone
  does the same; both together delete two. End, Left and Delete work as key-downs. Shift+End selects nothing
  (the next character was appended).
- Cleared with End plus 40 `VK_BACK` key-downs, typed `HI4501-probe` as `WM_CHAR`s, read back exactly. A posted click on
  Save wrote `ModdedSaves/single/HI4501-probe.Civ5Save` (221,280 B, 01:57:56) in the instance's own folder.
- The `SaveMenu` Lua state is not the visible save menu: in it `ContextPtr:IsHidden()` is true and `NameBox` is empty.
  The visible one is the `SaveMenu` LuaContext inside `GameMenu.xml`. Read it by its absolute path.

### 4.4 Hover

**Answer: works. A posted move shows the tooltip in 0.12-0.4 s, and a move off takes it away within the next frame or
two (30 fps cap).** Burst of PrintWindow frames as fast as the capture allowed (about every 70 ms), difference to a
reference in a box around the point, stock route [L]:
- City banner (Honolulu): change from 0.12-0.18 s, full at 0.33 s (banner tooltip plus the city's worked-tile
  overlay); gone on the frame after the move off.
- Unit flag (Pathfinder): from 0.14 s, full at 0.35 s ("Click to select this unit").
- Top panel gold (`/InGame/TopPanel/GoldPerTurn`): from 0.19 s, full at 0.39 s (the gold breakdown).
- Front end: the cursor left on PLAY NOW after a click showed its tooltip in the next picture (2.5 s later).
- Side finding: one frame of 47 in the banner burst (just after the move off) was a **half-drawn PrintWindow frame on
  the stock route** (terrain tiles over grey, no UI). Stock PrintWindow is not perfect either when frames are taken
  back to back (see 4.7).

### 4.5 Control reads

**Answer: works with a hover search, not with offsets. `GetScreenOffset` does not exist in this EXE. Name a control by
its absolute path; read size, hidden flag and text directly; find its client rectangle with posted moves and
`Control:HasMouseOver()`. UI pixels are client pixels.** [L unless marked]
- **Calls that work** on a looked-up control, the same at both client sizes: `GetSizeVal`, `GetSizeX`, `GetOffsetVal`
  and `GetOffsetX` (relative to the parent's anchor, so not a screen position: TechTree `CloseButton` offset 7,-13
  sits at client y 1045 at 1411 high), `IsHidden`, `IsDisabled`, `GetText` (an error on a non-text control),
  `HasFocus`, `GetID`, `GetNumChildren`, **`HasMouseOver`**. Also `UIManager:GetMousePos()` (returned the posted point
  exactly) and `UIManager:GetScreenSizeVal()` (= the client size: 2554x1411 and 1914x1051).
- **Absent:** `GetScreenOffset` (absent in the InGame, OptionsMenu, GameMenu and TechTree states, and not among the EXE's
  method-name strings [C]: the job's chunk in `minimap_step_a.py` could never have worked), `GetParent`,
  `GetChildren`, `GetChild`, `GetRect`, `GetAnchor`. The method table is a C function (`__index` is a function), so
  the methods cannot be listed. The EXE's strings name `GetNamedContextCount`, `GetNamedContextByIndex` and
  `GetVisibleNamedContext`, but `UIManager:` returned nil for each.
- **Hover search, the exact procedure** (scratch `locate.py`):
  1. A guess point (x, y) inside the control (from the picture, or the last known rectangle).
  2. `over(x, y)`: post one `WM_MOUSEMOVE` to (x, y) (wParam 0; the guard's virtual cursor set to the same point),
     sleep 0.12 s (about 3 frames at the 30 fps cap), then run in `--state InGame`
     `local c = ContextPtr:LookUpControl(PATH); return c and c:HasMouseOver() or false`.
  3. If `over(guess)` is false, stop: wrong guess, hidden control, or a control covered by another (an empty
     result, not an error).
  4. For each of the four directions (dx, dy) in (-1,0), (1,0), (0,-1), (0,1): `lo = 0, d = 4`; while d <= 700: if
     `over(x + dx*d, y + dy*d)` is false, bisect the integer interval (lo, d) with `over` until it is 1 px wide and
     return the last inside coordinate, `x + dx*lo` (or `y + dy*lo`); else `lo = d, d = 2*d`. 700 without leaving the
     control gives no edge.
  5. Rectangle = left..right x top..bottom, inclusive; size = right - left + 1 by bottom - top + 1.
  6. Afterwards move the cursor to a quiet point (60, 1300 here, map corner under clouds) so no tooltip stays open.
  Cost: 9-12 `over` calls per edge, 4-10 s per control on the workstation (each call is one posted move and one
  channel round trip). Found rectangles against `GetSizeVal`:

| Control | 2554x1411 | 1914x1051 | GetSizeVal |
|---|---|---|---|
| `/InGame/TopPanel/GoldPerTurn` | 244..303 x 6..17 | 244..303 x 6..17 | 60,12 |
| `/InGame/TopPanel/SciencePerTurn` | 4..49 x 6..17 | 4..49 x 6..17 | 46,12 |
| `/InGame/WorldView/MiniMapPanel/Minimap` | 2225..2544 x 1217..1406 | 1585..1904 x 857..1046 | 320,190 |
| `/InGame/TechTree/CloseButton` | 7..106 x 1045..1076 | 7..106 x 865..896 | 100,32 |
| `/InGame/CityView/ReturnToMapButton` | 1137..1416 x 1291..1326 | 857..1136 x 931..966 | 280,36 |
| `/InGame/CityView/FoodFocusButton` | 2269..2400 x 106..117 | - | 32,32 (hit area differs) |

  All five checked against the pictures by eye. The found size equals `GetSizeVal` except where a control's hit area
  is larger than its box (the EUI focus row). The search gives the hit area, which is what a click needs.
- **Listing children and "the control under a point": no direct call.** The stand-in is `HasMouseOver` on candidate
  paths after a posted move. A parent does not report mouse-over when a child takes it (`TechTreeScrollPanel` false
  over a tech button).
- **Instanced controls** (tech buttons, list items, notifications) have no path. Their instance tables are file-local
  (`local g_techButtons` in EUI's `TechTree.lua`) and not reachable as globals. The `debug` library exists in the
  TechTree state (`debug.getupvalue` is a function), so upvalues of a global handler might reach them [I, not tried;
  out of the contract's scope (lead, 2026-11-16)]. For these the trace keeps a plot or raw pixels (proposal 4.3).
- **States:** absolute paths resolve from any UI state tried (InGame, GameMenu, SaveMenu, TechTree, OptionsMenu). The
  default state `Main` has no `ContextPtr`; use `--state InGame` for control reads and `--state WorldView` for
  `UI.GetMouseOverHex`. StateNames can be ambiguous (4.3's `SaveMenu`).
- **Client fractions do not carry over between sizes:** Continue Your Journey sat at (2216,972) at 2554x1411 and
  (1656,792) at 1914x1051, i.e. 0.868/0.689 against 0.865/0.754. Panels are anchored (the minimap keeps its 10/5 px
  margin to the bottom-right corner).
- **Pixels go stale when the camera moves:** after the drags the banner of Honolulu had moved, and a click at its old
  pixel hit a plot (plot tooltip) instead of opening the city screen. The trace's plot form (proposal 4.3) is needed.

### 4.6 The front end

**Answer: in the front end the picture is the only witness that tells the screens apart. Files and the process show
two moments: mod activation and the load. From the end of the load on, the channel answers and `frontend.py ingame`
names the load screen.** Witnesses read without the channel (scratch `fe.py`: window title, client size, top-level
windows, process tree and memory, files changed in `Logs/` and `cache/`, `turns.py frontend`, `frontend.py status`)
[L, stock and product routes]:
- **Window title:** `Sid Meier's Civilization V (DX11)` on every screen, in game too. It tells nothing.
- **Boot and mod activation:** `turns.py frontend` answers `BUSY mod activation in progress` and then
  `IDLE front end idle` (activation done, cache quiet 12 s): this is the main menu's witness. It took 108 s (stock)
  and 71 s (product) from the launch. On the product route the proxy, the composite and `server.exe` (with its
  `conhost.exe`) start at mod activation (`shell_composite.log` loaded 02:11:48; `vpproxy.log` `boot: server pid` 97 ms
  after `DllGetGameContext`), so the main menu shows **`server.exe` as the EXE's child**. Before activation there is
  none.
- **Main menu, SINGLE PLAYER, SET UP GAME, LOAD GAME:** no file under `Logs/` or `cache/` changed between them, and
  the title, top-level windows and process tree did not change either. **Only the picture tells them apart.**
- **The load:** on the posted Load click the EXE writes `Logs/init_host.log` and `Logs/net_message_debug.log` at once,
  then `Logs/CustomMods.log`, `cache/Civ5SavedGameDatabase.db`, `Logs/Lua.log` (`InGame: ...`) and
  `Logs/system_init.log`. The Tiny load took about 8 s. Private memory went 1,504 -> 1,705 MB (stock).
- **The load screen after the load:** the channel answers (`cache/luaexec_result.txt` written).
  `frontend.py -i NAME ingame` says `NOT IN GAME [load_screen_ready] load screen up, its Continue button is shown`; after
  a posted click on Continue it says `IN GAME [in_game]`. While loading it says `[silent]`.
- **Posted input in the front end works on both routes:** every screen above was reached by posted clicks, and the
  front-end tooltips appear on hover.

### 4.7 The picture source on the product route

**Answer: on the product route use the composite's back-buffer dump. It is correct in every state tried, the main
menu and the load screen included, ready in 0.3-0.6 s, and 14.4 MB per frame. PrintWindow there gives flat grey or
UI-less frames for a quarter to a half of the in-game shots and about half of the load-screen shots. On the stock route
PrintWindow is good, with a rare half-drawn frame in fast bursts. A picture check needs a flat test (std 0, 84,84,84)
and a top-band test.** PrintWindow series of 20 at 0.5 s spacing (`shot.capture_hwnd`, 0.05-0.19 s per call), 2554x1411 [L]:

| State | Stock good / 20 | Product good / 20 | Product failures |
|---|---|---|---|
| Main menu (mods active) | 20 | 20 | - |
| In game at rest (Tiny, units asleep) | 20 | 11 | 7 flat grey (84,84,84, std 0), 2 UI-less (the server's world without the EXE's UI; top-band mean 198-204 against 151) |
| Game menu popup open (posted ESC) | 20 | 15 | 4 flat, 1 UI-less (no dimming, no menu) |
| Load screen after the load (frames 3-5 s apart, not 20) | 13 of 13 | 6 of 13 | 6 flat 84, 1 without the load screen's art (mean 164) |

- **Back-buffer dump** (`shell_composite.shot` written beside the composite in `MODS/(1) Community Patch`; BMP
  `backbuffer_<hhmmss>_<n>.bmp` in `<user>/Logs`, scratch `bb.py` polling every 20 ms): trigger taken after
  0.06-0.46 s, file first seen after 0.17-0.50 s, complete (size stable 100 ms) after 0.32-0.61 s, over 6 dumps (main
  menu, load screen, three at rest, popup). 14,414,830 B = 2554 x 1411 x 4 + 54, the client size. Every dump was the
  player's frame by eye (load screen, map with UI, menu over the dimmed map). The main-menu dump equals PrintWindow's
  statistics exactly.
- **The dump in the front end:** it works from mod activation on, because the composite is loaded with the proxy. Before
  activation (the intro and the base main menu) no composite runs and no server exists, so PrintWindow is the only
  source there; on both routes it was correct (20 of 20 at the main menu).
- **Stock in fast bursts:** 1 of 47 frames was half drawn (4.4). Over the 60 frames at 0.5 s spacing, none.
- **The tests used on these series, with their margins** (statistics on the full client frame, RGB, PIL `ImageStat`):
  - *Flat:* the largest per-channel standard deviation below 3. Every flat frame had mean exactly 84.0/84.0/84.0 and
    deviation 0.0. The lowest good frame (the main menu) had 22.2.
  - *UI-less:* the mean of the top band (rows 0-39, full width) more than 25 levels from the reference band, in any
    channel. Reference: a dump taken in the same state, or the series' good frames. At rest the dump's band was
    150.8/151.9/146.1, the good frames 150.6-152.4 (0-1.6 off) and the UI-less ones 197.5-198.7/203.1-203.9 (about 47-58
    off). With the game menu open: dump 17.4/17.2/15.7, good 17.4-18.3 (under 1 off), UI-less 193.9 (176 off). A
    threshold of 25 leaves more than 20 levels of margin on both sides here. run_3706.py (overlay-system.md 12.2)
    used 35 against a stock picture, which was 11-18 off for good frames and 53-63 for UI-less ones.
  - *Half-drawn* (the one stock burst frame): whole-frame mean 94 against about 169. Neither test catches it in
    general. A whole-frame difference to the previous frame above about 30 levels, with the judge saying nothing
    changed, flags it.
- **For the contract:** use the dump as the picture source on the product route; keep PrintWindow for stock and for
  the time before mod activation. Every PrintWindow frame needs the flat test (all pixels 84,84,84) and a top-band test
  against a known-good frame or the dump (UI-less frames have a bright top band, no top panel). A frozen-picture check
  on PrintWindow frames of the product route would also report the capture's own glitches.
- **The band as first built (rows 0-39, full width) was replaced on 2026-11-22 (ticket 0671): see 4.7a.**

### 4.7a The top band measures the top bar only (ticket 0671, 2026-11-22)

**Answer: the band is client rows 0-23 and 480 client columns from each side (`capture.band_boxes`, `BAND_ROWS`,
`BAND_SIDE`), scaled to the picture when it is a scaled copy (`stats(img, client_width=...)`). Rows 0-39 at full width
held the world, and a fight or a camera move under the bar failed good frames.** `[C]` code and tests, `[L]` on
recorded pictures; no live run.

- **The failure** `[L]`: release-large's checkpoint `attacked` (step 11, right-click attack, 4 s wait; the camera moves
  with AutoUnitCycle and the fight animates): "top band 38 levels from the reference (printwindow, 2 retakes)" on the
  first beta's usual plan (cand1/run, product route without the composite, 2026-11-22 11:37) and 29 levels on the
  second candidate's gate (cand2/tier3, STOCK route, 13:4x), exit 3, both on c5h-new-a (EXE 26577625, client
  1914x1051). The pictures show the whole top bar in place; only the world under and between its pieces changed.
  Earlier the same: 4609's "top band 37 levels" product flake at the same checkpoint (15.16).
- **The bar's geometry** `[L]`, per-row and per-column means of in-game frames at 1914x1051 (EUI, VP 5.4.8): the bar is
  rows 0-26 (dark, mean 3-62 per row), the world from row 27; in two pieces, left about x 0-620 and right about
  x 1320-1914, the world showing between them (row means about 220 on a snowy view). Probe 4501's frames at 2554x1411
  show the same dark bar in rows 0-23 at both sides (band 41-43 per channel), so the UI is laid out in client pixels,
  not scaled with the client `[I]` from two sizes.
- **On the runs' pictures** (`C:/c5-64bit-work/0671/survey.txt`, `series4501.txt`, scripts beside them): consecutive
  pictures of cand1's four routes and cand2's two release-large routes, band difference old / new: the attacked
  checkpoint 38.3 / 0.0 (cand1 product), 29.4 / 0.0 (cand2 stock); the camera moves of steps 9 and 12 27.8-38.1 / 0.0.
  Every pair still above 25 with the new band is a real UI change (load screen to game, a city screen, the game menu's
  dimming: 28-135), which the retake rule accepts as before. Probe 4501 at 2554x1411 against the series median: good
  frames 0.1-1.7 (old 0.1-1.4), UI-less frames 159.6 and 164.4 (old 55.8 and 55.5), the popup series' UI-less frame
  188.7 (old 183.4): the margin to a frame without the UI grew.
- **Regression test** `tools/hi/tests/test_band_interface_only.py` (6 tests, 0.5 s): the top 40 client rows of the two
  failing checkpoints, their reference pictures and probe 4501's good and UI-less frames, kept as 64 x 10 block means
  in `tools/hi/tests/data/band_0671.json` (no picture committed); it fails with the old geometry put back (negative
  control, `C:/c5-64bit-work/0671/scripts/negctl.py`).
- **Not changed:** `campaign.band_glitch` calls `stats()` on scaled step pictures without `client_width`, so its band
  there is the band's client size read at the picture's scale (rows 0-23 and 480 columns of a 1280-wide picture: about
  36 client rows and 718 columns of a 1914 client). Its test still passes; giving it the trace header's client would
  make it exact (tools/hi/charters/ was outside 0671's touches).
- **Live so far** `[L]` (2026-11-22 14:32-14:41, c5h-new-a, installer-laid product route, the new band): release-basic
  passed 6 of 6 checkpoints, 67 `composite` pictures and one `printwindow` (the leader screen of an in-game load, when
  no `server.exe` child exists, so `composite_active` is false by design). That run ended before release-large.
- **Live, whole run** `[L]` (2026-11-22 15:55-16:44, c5h-new-a, the validated plan's stage the tier's way): the new
  band met release-large's `attacked` checkpoint on both routes and both passed: product (composite pictures) and
  stock (PrintWindow, the case that failed the second candidate's gate at 29 levels). All checkpoints passed: 6 + 5 per
  route; details and picture counts in [validation-tiers.md](validation-tiers.md) 19.

### 4.8 The options screen, read only (the deny list's input)

**Answer: press none of these. In game the display controls are blocked by the game itself, so the risk is in the front
end.** Read by path and picture; nothing listed here was pressed [L, C for the XML ids read at run time]:
- **In game** (`/InGame/GameMenu/OptionsMenu_InGame/`, opened by posted clicks ESC -> OPTIONS -> VIDEO OPTIONS):
  `FullscreenCheck` (32x32), `WResolutionPull` (210x32; `FSResolutionPull` is hidden while windowed), `ApplyResButton`
  (200x32), `MSAAPull`, `VSyncCheck`, the quality pull-downs (`LeaderPull` ... `TextureQualityPull`), `DefaultButton`,
  `AcceptButton` (260x45, "Accept"; it writes the settings files), and `GraphicsChangedPopup` with `CountYes`/`CountNo`.
  In game the tab shows "Video options may not be changed while in game", and `VideoPanelBlock` (928x460, shown)
  covers the resolution, anti-aliasing and Fullscreen block: a posted move onto `FullscreenCheck` or `ApplyResButton`
  gives `HasMouseOver` false. Also deny: the MULTIPLAYER OPTIONS tab's `TurnNotifyEmail*` and `TurnNotifySmtp*`
  edits (they store credentials), `LanguagePull` (disabled in game).
- **Front end** (no channel there, so ids from the install's XML): main menu `/FrontEnd/MainMenu/MultiplayerButton`
  (then `MultiplayerSelect`'s `InternetButton`, `LANButton`, `ReconnectButton`, `PitbossButton`), `ModsButton` ->
  `ModsBrowser`'s `SmallButton2` (Workshop: `Steam.ActivateGameOverlayToWebPage`) and `SmallButton1` (delete a mod),
  `OptionsButton` -> the same options screen with the display controls **not** blocked in the front end [I, not
  opened], `ExitButton`. Picture: the main menu in the feed (4.6).
- **Elsewhere:** the Load Game and Save Game screens' `Delete` button (deletes a save in the instance's own folder;
  harmless to the owner's saves but it destroys the tester's inputs), and the game menu's EXIT TO WINDOWS and RETIRE.

## 5. Posted actions (W1)

Ticket 4502, impl-4502 (account B), 2026-11-16. Code: `tools/hi/actions.py`, `capture.py`, `session.py`, `hi.py`;
tests `tools/harness/tests/test_hi_actions.py` (hermetic) and `test_hi_actions_live.py` (stand-in, opt-in).

### 5.1 What was built `[C]`

- **Actions** (`actions.py`), each a function on an instance or a session, every one logged as `hi_action` in the
  instance's `harness.log`: `move`, `click` (left, right, middle; Shift/Ctrl/Alt held through the guard and the `MK_`
  flags), `double_click` (down, up, `WM_xBUTTONDBLCLK` 60 ms later, up), `drag` (down, `steps` moves with the button's
  `MK_` flag and the guard's held button, up), `wheel` (120 per tick, 50 ms apart, lParam in screen coordinates),
  `key` (input.key's sequence on the same primitives), `hold` (key down, repeats with lParam bit 30 at the system's
  keyboard delay and speed, key up; at most 5000 ms), `type_text` (`WM_CHAR` only, 15 ms apart; printable ASCII and
  Latin-1, at most 200), `hover` (move, then a sleep). `input.py` and `shot.py` are unchanged.
- **Checks, in the contract's order for a step:** box check (`actions.box_check`, the dialog watcher's marker through
  `dialogwatch.marker_dialog`, a function that posts nothing; exit 8), clipping to the client area (exit 2), the deny
  check (`from deny import check`; until W2 lands a stub that refuses only `ALT+ENTER`; kinds `click`,
  `double_click`, `drag`, `key:<SPEC>`; exit 2), the hit (W2's `judge.resolve_hit`, else kind `pixel`), the input.
  The action functions run the box check and clipping again just before their input. `move(inst_or_session, x, y,
  frame="client"|"shot")` is the signature W2's hover search uses.
- **Pictures** (`capture.py`): PrintWindow with the flat test (largest channel standard deviation < 3) and the top-band
  test (the top bar only, more than 25 levels from the band of the session's last accepted picture; see 4.7a), up to
  2 retakes; a band change that the retake repeats is accepted as a UI change. (Corrected 2026-11-22, ticket 0671;
  was: "rows 0-39".) On the product route, when `shell_composite.dll` is
  in `MODS/(1) Community Patch` and the EXE has `server.exe` as a child, the composite's dump (trigger
  `shell_composite.shot`, BMP read from `<user>/Logs` and deleted), with PrintWindow as a recorded fallback after 5 s.
  The full frame of the current picture is kept as `current_full.png` for `zoom`. A glitch picture is saved, does not
  become the current picture, and the command exits 3.
- **Session** (`session.py`): `<run>/hi/session.json`; `session start` refuses an active session (2), a claim of
  another ticket (2; the claim's ticket may be the ticket's tracker `parent`, since the pool knows only ids on main),
  a box (8), no picture (3). Pictures and `trace.jsonl` (stub writer until W3's `trace.append`) in `--out`, default
  `F:/c5-64bit-work/<ticket>/hi/<session id>/`. W2/W3 modules are loaded by file path (`tools/hi/trace.py` would lose
  to the standard library's `trace` on a plain import).

### 5.2 Live proofs, stock route `[L]`

2026-11-16 02:43-02:50 EST, c5h-a (C:, Goldberg, claimed `--ticket 0392 --address impl-4502`), the instance's own
DLL, client 2554x1411, frame cap 30 fps, `HI4502-TINY_0008` (copy of `C5H-0167-TINY_0008`, b2ceb663..., turn 9),
auto-loaded by `frontend.py prepare`, units asleep and the auto unit cycle off before the session
(`setup_scene.lua` of 4501). Session width 1280 (shot px x 1.995 = client px). Independent reads through the Lua
channel (read only) after each action: `UI.GetMouseOverHex()` in `WorldView` after a posted move, `GetText()` of the
name box. Pictures in the Visual Checks feed under 4502 (DIAGNOSTIC), full sessions in `F:/c5-64bit-work/0392/4502/`.

| Action | Command | Result |
|---|---|---|
| drag | `step drag 330,560 530,560` (client 658,1117 -> 1058,1117, 12 moves, 0.4 s), start plot (49,12) empty | map panned right: picture shift +182 shot px (363 client px for 400, 4501 4.2 had 392); hex under the shot centre (51,14) -> (50,14) |
| hold | `step hold RIGHT 1000` | 15 repeats posted; camera panned east, hex under the centre (50,14) -> (54,13) |
| hover | `step hover 137,6 800` (client 273,12 = `/InGame/TopPanel/GoldPerTurn`) | `HasMouseOver` true; the gold tooltip in the step's picture (region difference 54 levels against the picture before) |
| type | `step key ESC`, `click` SAVE GAME, then `key END` + 30 x `key BACKSPACE` + `type "HI4502-typed ok"` (one step, 32 actions) | `/InGame/GameMenu/SaveMenu/NameBox` `GetText()` = `HI4502-typed ok` exactly (it read `Kamehameha_0009 BC-3640` before); closed with Back and ESC, nothing saved |
| double-click | `step click 380,300` and `step dclick 380,300`, each from the same scene (units asleep, nothing selected, a new session after the set-up) | the same: the hex under the point moved by (-2,+1) in both (the click pans the camera), interface mode 1 (selection) and no unit selected after both; `UI.IsTouchScreenEnabled()` false. With touch off the double-click is delivered and nothing reacts beyond the first click (4501 4.1) |
| outside the client area | `step click 1280,10`, `step move 10,707` | exit 2, `point (2554,20) is outside the client area 2554x1411`, `(20,1411)`; nothing posted |
| another owner | `--by intruder step click 10,10` | exit 2, `owned by impl-4502` |
| deny stub | `step key ALT+ENTER` | exit 2, nothing posted |

Side observation: the first double-click of the run (before the controlled comparison) left the Pathfinder (1003)
selected and the camera on it; the controlled pair (set-up between them) showed no selection after either, so that
selection came from the scene (the unit was due for orders), not from the double-click `[I]`.

### 5.3 The box refusal, live on a stand-in `[L]`

`test_hi_actions_live.py` (2026-11-16 02:46 EST, `C5_HARNESS_LIVE=1`, hidden desktop `C5T4502`): a stand-in with
window class `WinClass_FXS` opens a NULL-owner `Render Error` box after 10 s and ends by `--exit-after 40` (nothing
presses it); the real watcher (period 1 s, `--no-notify`) wrote the marker 12.1 s after the launch. Before it, `step
click 100,80` reached the stand-in (its log: one `WM_LBUTTONDOWN`) and `step click <client width>,10` exited 2;
after it, `step click 200,120` and `step key ENTER` exited 8 with the box's text, and the stand-in logged no further
button message; `session start` on the instance exited 8. The trace's exits were 0, 2, 8, 8. 1 passed in 40.8 s.

### 5.4 Live proofs, product route `[L]`

2026-11-16 02:54-03:04 EST, c5h-a, run_3308's prep (scratch `pp.py`, as 4501) on `HI4502-PP-TINY_0008`: pins
`x64-04b1211b-pfdee656` (DLL 29F58E17..., server 5B254430..., proxy 75C40CEC..., checked after the prep),
`shell_composite.dll` built by `shell\build.cmd` from `git archive` of fdee65668 (SHA-256 500E06E7...; 4501's build
of the same commit was F6BA4926..., so the build is not byte-reproducible), client 2554x1411, `frontend.py prepare`
after the prep (the order that avoids 4501's trap: `frontend.py restore` before the prep, so the backup holds the
original front end). Restored by run_3308's restore: backup verified equal by hash, layout removed, the save copy
removed, `provision.py verify` rc 0, the three front-end files "original".

- **Picture source switches by itself:** `session start --route product` about 25 s after the launch (before mod
  activation, no `server.exe`) took PrintWindow (one start 5 s earlier exited 3: the intro frames were flat, std 0).
  During mod activation `shot` exited 3 twice: PrintWindow timed out after 10 s (the window thread did not pump), and
  once the dump also timed out (5 s) just after the server appeared. From then on every `shot` and step picture of the run
  was a dump (11 shots at 13-16 s spacing through the load screen with Continue, then about 30 step pictures in game),
  retakes 0, none flat. A dump requested during activation arrived 17 s
  late in `Logs` (14,414,830 B); `capture._dump` now marks a timed-out request and the next dump deletes such a file.
- **The same actions as on stock, the same results:** drag 330,560 -> 530,560: shift +182 shot px, hex under the
  centre (51,14) -> (50,14); hold RIGHT 1000: (50,14) -> (54,13); click and double-click on 380,300 from the same set-up:
  interface mode 1 and no unit after both, touch off (the camera moved by (-1,+1) after the click and (-3,+1) after the
  double-click from different starting cameras, so the pan amounts are not a comparison).
- **Hover: 11 of 12 hovers showed the tooltip; one did not.** In the one, `UIManager:GetMousePos()` still read the
  previous point (1277,1297) after the hover's posted move to (273,12), and stayed there through a later `shot` and a
  burst: the move was not registered. The next move to the same point registered, and 7 repeats of move 640,650 +
  hover 137,6 (with and without step pictures) all registered (`HasMouseOver` true, tooltip in 4 of 4 pictures,
  region difference 49-53 levels). Cause not known `[I]`: the lost move came right after a step whose picture was a
  dump. A runner sees it in the picture; the judge's `under` read (W2) would see it too. Not seen on stock (1 of 1
  hover, plus 4501's).
- **Bursts:** `burst 10 2000 --printwindow`: 10 frames in 2.05 s, 6 marked glitch (flat 84,84,84, std 0), 4 good.
  `burst 4 2000` (dumps): 4 frames at 0.02, 1.89, 2.23, 2.81 s (the dumps' own rate), none glitched. Burst frames are
  now tested and saved after the series, so saving (about 0.4 s per frame) does not stretch the spacing.

### 5.5 Timings `[L]`

One `hi.py` call costs about 1 s of Python start and imports. `shot` on stock: PrintWindow 0.05-0.2 s plus saving the
1280 picture and the 2554x1411 `current_full.png` (about 1-2 s together). On the product route a dump is 0.3-0.6 s
(4501 4.7) plus reading the 14 MB BMP. A `step` with one action, a 500 ms wait and a shot took 3-4 s wall time.

### 5.6 Not verified by W1

- The `stable` wait on a live game (hermetic tests only).
- The deny list beyond the stub's `ALT+ENTER` (W2's `deny.py`), the judge's hit forms and state reads (W2): W1 calls
  them by the interface the lead relayed (`judge.resolve_hit`, `read`, `wait`, `check_condition`), tested with a fake.
- A box during a game session (the stand-in proved the refusal; no game box was made, by rule).
- A middle click and the wheel on the game (posted as `input.py` does; hermetic tests only).

## 6. The judge and the UI map (W2)

Ticket 4503 (impl-4503, account B, 2026-11-16). Code: `tools/hi/judge_lua.py`, `judge.py`, `uimap.py`, `deny.py`,
`frontend_screen.py`; tests `tools/harness/tests/test_hi_judge.py` and `test_hi_judge_static.py`. Live runs on
c5h-d (F:, Goldberg, stock route, DLL `i0ship-26234371-ia` 3C2F47DA..., mods CP+VP+EUI activated by
`frontend.py prepare --mods cp,vp,eui`, save `HI4503-TINY_0009` = copy of `C5H-0301-CAT2_0009`, SHA-256
b2ceb663..., the same Tiny save as 4501), launched by `cli.py launch` (watcher on, frame cap 30 fps), every action a
posted message through W1's `tools/hi/actions.py`. Scratch drivers in `C:/c5-64bit-work/0392/4503/scripts/` (not
committed); run records `F:/c5-64bit-work/0392/4503/runs/`, pictures `F:/c5-64bit-work/0392/4503/pics/`.

### 6.1 What the judge is made of [C]

- **Chunks** (`judge_lua.py`): `STATE` (the short state, InGame state), `SNAPSHOT` (humanplay's text copied
  unchanged, Main), `CONTROL`, `CONTEXTS`, `UNDER`, `OVER` (InGame), `HEX` (WorldView), `PLOT` (Main). Placeholders
  take ints or tuples of ints only (`fmt`); a control path goes in as its character codes and is rebuilt with
  `string.char`, after a check against `^(/[A-Za-z_][A-Za-z0-9_]*){1,12}$`. Map bounds are checked in Lua before
  `Map.GetPlot`; `LookUpControl` answers nil for an unknown path.
- **The read-only scan** (`judge_lua.scan`, run by the static test over every chunk and again by the judge before each
  send): a Lua 5.1 tokenizer with scope tracking. Refused: any call or reference whose name starts with the contract's
  prefixes (the test of "starts with" is literal; `LookUpControl` is the one named exception to `Look`, which the
  contract meant for the camera's `LookAt`; `Register`, `Dequeue`, `Enqueue` added), anything on `Events`,
  `LuaEvents`, `Network`, a `UI.`/`UIManager:`/`ContextPtr:` member outside the listed getters, code loading and
  environment changes (`loadstring`, `load`, `dofile`, `require`, `setfenv`, `rawset`, `setmetatable`, `debug`, `io`,
  `os` but `os.clock`/`os.time`), a string index with a writing name, and any assignment to a global or to a field of
  a table the chunk did not make (a local initialised by a table constructor). 45 positive controls (one per
  forbidden form) and 15 negative ones; every chunk also compiles in Lua 5.1 (lupa).
- **Availability:** a timeout whose request was never picked up (the signal still pending) means the front end or
  loading (`why` from the picture judge: `front-end`, `loading`, or `channel silent`); a request picked up but not
  answered means a loaded game that is busy (`channel busy`, still exit 4; the deny list refuses then); a Lua error
  comes back as its text with exit 1. Pure mode refuses every read with exit 2 before anything is sent.
- **`under`:** the cursor is moved to the point (W1's `actions.move`, a harness move), then one `CONTEXTS` read says
  which contexts are shown, then one `UNDER` read asks `HasMouseOver` of every control id of the shown contexts,
  deepest first (nested context before its parent, later sibling first, deeper element first). The **candidate
  table** is built at run time from the install's and the active mods' UI XML (file name resolution: MODS folders
  last-loaded first, then DLC folders other than the expansions and Tablet (EUI's `UI_bc1`, `VPUI`, ...), then
  Expansion2, Expansion, Assets): every element with an `ID` outside `<Instance>` templates, LuaContexts followed
  down to four levels. On c5h-d (CP+VP+EUI) [L, 2026-11-16]: **122 contexts, 4,475 control ids**; it is cached in
  `<run>/hi/candidates.json` per build and never committed (D83). `uimap.candidates(inst, screen)` lists one screen's
  paths (W7, W8, D159). Contexts whose box follows the cursor (`/InGame/PlotHelpManager`, `PlotHelpText`) are left
  out of `under`: `/InGame/PlotHelpManager/TheBox` reported `HasMouseOver` over the map and over unnamed UI [L].
- **Rectangles:** 4501's hover search exactly (posted move, 0.12 s, `HasMouseOver`; step 4, 8, 16, ... then bisect;
  700 px limit), cached in `<run>/hi/rects.json` under `<build>|<w>x<h>` and the screen (first two path segments);
  a cached entry is checked at its centre (one move and one read) and searched again when the check fails, never
  returned stale. The cursor parks at the bottom centre afterwards (4501's 60,-111 is the unit panel when a unit is
  selected; its tooltip stayed open there [L]).
- **`plot-pixel`:** by computation from the EXE's own camera, read from its memory with `shell/tools/gt_reader.py`
  (read only: `OpenProcess` with `PROCESS_VM_READ`, `ReadProcessMemory`; refuses an EXE whose PE timestamp is not
  0x546CD0A8; issues 3004 and 3951) and projected on the picking plane z = +12 (camera-and-picking.md 5), then
  **confirmed** by one move read with `hex`; when the camera cannot be read or `hex` names another plot, a search by
  posted moves (an affine fit from hex units to client px over a 5 x 4 grid of `hex` reads, refined by the misses,
  then the centre by moves). Works on both routes (the EXE's camera is the same) `[I for the product route, not run]`.
- **Hex centre by moves** (the check's ground truth and the search's last step): by vertex rows, see 6.3 (corrected
  2026-11-16 05:15; the screen-column methods first written here were wrong away from the screen's middle).
- **The front-end screen** (`frontend_screen.py`): normalized cross-correlation of a grey signature region with a
  reference cut at run time, best over +-8 px; named when the best score is at least 0.90 and 0.10 above the next.
  Signature regions in UI px from the client centre: main menu (-150,-200,150,-112), single player (-90,-196,90,-162),
  set up game (-90,-292,90,-265), load game (-90,-279,90,-251), options (the tab row, -430,-245,430,-222). The front
  end's panels sit at the **same UI-px offsets from the centre at 1914x1051 and 2554x1411** [L] (main menu buttons at
  -130/-85/-40/+5, load game's Delete at +296). References in `F:/c5-64bit-work/0392/hi/frontend-refs/<w>x<h>/`.
  **The load screen has no picture signature:** its art is the civilization's (per save) and once loaded it fades
  over the map, so a whole-frame reference cut while loading scored 0.22 on the ready screen [L]. Its witnesses: the
  channel after the load (`frontend.probe_in_game`: `load_screen_ready`/`_loading`) and, while loading,
  `Logs/init_host.log` written within 90 s with the channel silent (and not answering `in_game`).
- **The deny list** (`deny.py`): keys `ALT+ENTER` (any spelling `parse_key` reads as Alt+VK_RETURN). In game (the
  channel answers within 1 s): a move to the point, then `HasMouseOver` of 24 listed paths (the in-game options'
  display controls, language pull-downs, the turn-notification e-mail and SMTP fields, the Delete buttons of the load,
  save and save-map menus); a busy channel or a Lua error refuses (fail closed); a box raised by the move passes
  through as W1's exit 8. Since 2026-11-18 (epic 0398, MC #6282) a read the channel has picked up but not answered
  within the 1 s gets one more read, so the whole wait is at most `DENY_LUA_BUSY_WAIT_S` = 5.0 s before it refuses
  (still fail closed; a request never picked up gets no second read): two i2-rc1 phase runs on saturated F:
  instances were refused at the 1 s bound (setup:6-h1 on c5h-d, #5917 product-1 on c5h-f) [L]; why the channel
  answers late there is not determined [I] (ticket 4610 item 9). In the front end: the screen by picture and centre-anchored regions: main menu MULTIPLAYER
  (-160,-108,160,-63) and MODS (-160,-63,160,-18) (the mod browser's Workshop and delete buttons are behind it),
  load game Delete (-80,278,80,314), and the front-end options' whole content panel (-470,-200,470,230), whichever tab
  is shown (the judge names the options screen, not its tab). Cost: 1.4-2.0 s per check in the front end, one move
  and one read in game.

### 6.2 Live checks at 1914x1051 [L] (2026-11-16 03:00-03:45 EST)

- **Front end, five screens:** 40 of 40 readings right over 5 rounds of posted navigation (main menu 10, single
  player 15, set up game 5, load game 5, options 5; `runs/fe-cycle-1914.json`); scores 1.00 for the right screen,
  at most 0.67 for the next (main menu against single player). The load screen: 5 of 5 by the channel witness
  (`load_screen_ready`, `runs/fe-loadscreen-1914.json`).
- **The front-end options, read and pressed nothing** (opened by a posted click on OPTIONS, then the VIDEO OPTIONS
  tab): Screen Resolution pull-down (client 693..900 x 349..378), Anti-Aliasing (773..900 x 386..413), Fullscreen
  check with its label (787..896 x 423..441), Apply Resolution (617..812 x 477..505), VSync, High Detail Strategic
  View, GPU Texture Decode, Minimize Gray Tiles, Allow Shadow Fadeout, Automatically Size Interface, Use Small Scale
  Interface, and the nine quality pull-downs (1179..1405 x 330..693); none blocked in the front end (4501 found them
  blocked in game). Cancel returns without applying.
- **Deny dry runs, nothing pressed:** 23 of 23 as expected (`runs/deny-fe-*-1914.json`): every video control above
  refused, the tab row, Cancel and Accept allowed; load game Delete refused (centre and edge), Back, Load Game and a
  save item allowed; MULTIPLAYER (centre and edge) and MODS refused, SINGLE PLAYER, OPTIONS, OTHER allowed. A W1
  `actions.click` on MULTIPLAYER was refused with `actions.Refused` exit 2 before anything was posted.
- **`under` on three screens** (the expectation is the control the picture shows at the point; "none" where the
  picture shows an instanced control, which has no path; `runs/under-{A,B,C}1914.json`, pictures with each named
  control's searched rectangle drawn in `pics/under-*.png`):
  - In game with the top panel (A): **26 of 27** - all 12 top-panel items, the info corner's PDButton, the minimap
    (two points), the end-turn button, the production notification, the map options button, the unit portrait, three
    map points (plot), four instanced controls (diplomacy corner buttons, city list, an action button) named none.
    Wrong: a unit action button inside the portrait button's hit area (0..119 x 931..1050) is named
    `UnitPortraitButton` (both report `HasMouseOver`; the instanced button is not asked).
  - The city screen (B): **32 of 32** (24 named controls: return, name, previous/next city, edit, buy-plot, avoid
    growth, five yield boxes, five focus buttons (hit areas 1629..1760 wide, as 4501 found), units and buildings
    headers, production box, queue box, production portrait, automate, citizen-management header; 2 plots; 6
    instanced controls none).
  - The tech tree (C): **25 of 25**, but only **4 named controls exist in EUI's tech tree context** (TechTreeScrollPanel,
    EraStack, CloseButton, TechTreeScrollBar): 4 points on Close and the scroll bar named right, 21 points on tech
    buttons, the legend, era headers and the background named none. `ReturnToMapButton` 857..1136 x 931..966 and
    `CloseButton` 7..106 x 865..896 equal 4501's rectangles at this size.
  - Cost: `under` 0.35-0.9 s (277 candidates in game); the first hit of a control adds its hover search, 6-16 s;
    cached afterwards (one check move).
- **Completeness of the candidate table (D159):** every control the pictures show that the XML names was found; what
  `under` cannot name are instanced controls (tech buttons, list items, diplomacy-corner and action buttons, the city
  list, notifications other than the named ones). They are most of the tech tree and the lists. `label` for them
  (contract 8.1) is **not built**.
- **`hex` beneath UI, and the world witness (CONTRACT v1.6 hits; corrected 2026-11-16 04:30, after 4504's live
  cases on ec4061a8: the load screen's Continue button and a city-screen production item were recorded as plots):**
  `UI.GetMouseOverHex` names the plot beneath any UI, instanced UI included, and the tech tree [L]. The game has no
  direct "mouse over UI" read (`UI.IsMouseOverUI`, `UI.IsMouseOverHUD`, `UI.IsCursorOverUI`, `UIManager:GetMouseOver`,
  `UIManager:IsMouseOverUI`, `UIManager:GetControlUnderMouse` are all nil) and LuaContexts and containers (stacks,
  boxes) never report `HasMouseOver`, so the nearest named container of an instanced item cannot be named [L]. The
  witness used instead: **`/InGame/PlotHelpManager/TheBox` (the plot tooltip's box) reports `HasMouseOver` exactly
  when the cursor is on the map** (land, water, clouds, the gap in EUI's top bar) and never over UI (unit action
  buttons, the city list, diplomacy-corner buttons, unit flags) [L, 14 points]. `resolve_hit` order: the load screen
  shown (its own `LoadScreen` Lua state; its context is not reachable by an absolute path from InGame) -> `pixel`
  with the Continue button's text as `label` when it is under the cursor; a named control -> `control`; any screen
  of `judge_lua.SCREENS` shown or a popup up -> `pixel`; the witness not under the cursor -> `pixel`; else `plot`.
  Live on c5h-d: the Fortify row, the city list and a diplomacy button -> pixel, two map points -> plot, GoldPerTurn
  and UnitPortraitButton -> control; 1.0-1.5 s per hit (five reads). Pixel hits carry a `why`.
- **Edge scrolling:** InGame.xml's `ScrollTop/Bottom/Left/Right` are 2 px strips at the client's edges; a cursor there
  pans the camera. Plot centres measured by moves drifted between runs while probes reached the edges; no probe of
  the judge goes within 2 px of an edge any more, and an extent cut by the edge is not taken for a boundary [L].
- **Hex reads lag:** a `hex` read right after a posted move can name the previous point's plot; the read is repeated
  until `UIManager:GetMousePos()` equals the point moved to (an `[I]` remedy: the mouse point may update before the
  hex does).
- **`plot-pixel`:** see 6.3.
- **In-game deny dry runs** (posted ESC, then the game menu's buttons; nothing on the list pressed): load menu Delete
  (centre and edge) and save menu Delete refused, Back and Load Game allowed (`runs/deny-ig-load-1914.json`,
  `deny-ig-save-1914.json`); MULTIPLAYER OPTIONS tab 13 of 13 (`deny-ig-mp-1914.json`): the e-mail check box and
  Use TLS refused by `HasMouseOver`, **the e-mail and SMTP edit boxes never report `HasMouseOver`** [L] and are
  refused by region while `MultiplayerPanel` is shown; Steam Invite, Nick Name, Cancel and Accept allowed. VIDEO
  OPTIONS tab 7 of 7 (`deny-ig-video-1914.json`): nothing under `VideoPanelBlock` reports `HasMouseOver` in game (the
  game's own block), and the panel's content is refused by region while `VideoPanel` is shown anyway.
- **A nested context is shown while its parent context is hidden** [L]: with the in-game load menu or options open,
  `/InGame/GameMenu` is hidden and `/InGame/GameMenu/LoadMenu` (or `OptionsMenu_InGame`) shown. `CONTEXTS` reads each
  context's own flag; an ancestor check there had left the options' controls out of `under`.

### 6.3 `plot-pixel` against the hex centre [L] (1914x1051, 2026-11-16 04:05-05:40 EST)

- **The camera route** (`gt_reader` read of the EXE's camera, projection on z = +12, one move read with `hex`): the
  plot was confirmed by `hex` on every plot tried at both zooms (zoom levels 17 and 11 of the camera block), 0.3-0.7 s
  per plot.
- **The check's ground truth, by `hex` reads only** (`uimap.centre_by_moves`): with a camera that only pitches, a
  screen row is a line of constant world y, so a plot's top and bottom vertices are its extreme rows and lie on its
  axis (the midpoint of its extent along any row); the vertex rows of the plot and of the plots two rows up and down
  (world y +-r, +-2r, +-4r) fix the projective map from world y to the screen row (least squares), which gives the
  centre's row; the extent's midpoint along that row gives its x. 2-4 minutes per plot at the closer zoom.
- **Corrected 2026-11-16 05:15:** two earlier ground truths were wrong and are kept here so nobody repeats them:
  (a) the midpoint of the plot's extent along the screen column: 2-5 px low at zoom 17 (the camera's tilt); (b) a
  projective fit along the screen column: up to 52 px off at zoom 11 away from the screen's middle, because a screen
  column is not a line of constant world x there (perspective), so the column misses the vertices. The camera's
  point was right throughout: along its own row the plot's extent was centred on it within 0.5 px on 10 of 10 plots
  at zoom 11 (`runs/plot-Z2x-1914.json`), and the vertex-row centre agreed within 0-1 px on the three worst plots of
  (b) (`runs/plot-T3z2-1914.json`).
- **Results (1914x1051, 10 plots each, chosen from a grid of `hex` reads):** zoom 17: **10 of 10 within 4 px**
  (0-1.4 px, `runs/plot-Z1-1914.json`); zoom 11 (three posted wheel ticks in): **10 of 10 within 4 px** (0-1.0 px,
  `runs/plot-Z2-1914.json`). Every camera pixel was confirmed by `hex` (method `camera`, 0.3-0.7 s); the ground truth
  took 60-115 s per plot.
- **Edge scrolling** made earlier series drift (see 6.2); none of the series above probes the edge strips.

### 6.4 Live checks at 2554x1411 [L] (2026-11-16 05:45-06:40 EST)

Same instance, DLL, mods and save; `WindowResX/Y` 2560x1440 written before the launch (backed up by SHA-256, restored
at the end); the game stopped with `cli.py stop --human-turn-hook tools.harness.turns:stop_readiness` between the sizes.
Every position of the drivers is the 1914x1051 one moved by its anchor (centre-anchored front end and menus: from
the client centre; right and bottom panels: by the size difference), so the layout itself was checked too.
- **Front end** (`runs/fe-all-S2.json`): with only 1914x1051 references (the first reading, before calibration) the
  main menu was named at score 1.00 (references carry over between sizes since 20253a7e); after cutting native ones:
  **40 of 40** readings over 5 rounds; deny dry runs **23 of 23** (video options, load Delete, MULTIPLAYER, MODS
  refused; tabs, Cancel, Accept, Back, Load Game, a save item, SINGLE PLAYER, OPTIONS, OTHER allowed). At this size the
  menu background is a smaller centred picture with black margins (full-bleed at 1914x1051); the panels sit at the
  same UI-px offsets from the centre, which is why one set of regions serves both.
- **The load screen:** while loading, 5 of 5 by `init_host.log` (1.1-7.4 s after the Load click); ready, 10 of 10
  (channel witness, and the file witness within 90 s). `resolve_hit` on the Continue button gave `pixel` with `label`
  null: the button did not report `HasMouseOver` at its centre (2216,972) after the judge's move `[open]`.
- **`under`:** in game **26 of 27** (the same unit-action-button miss; EUI scales the unit panel with the client: the
  portrait button's hit area is 2..199 x 1211..1408 here against 0..119 x 931..1050 at 1914x1051, while the top
  panel, the minimap, the city screen's panels and the menus keep their px size); the city screen **31 of 32**: at
  this size EUI's left city panel puts the border-growth meter inside `CultureBox`'s hit area (2..240 x 392..491),
  so `under` names `CultureBox` where the 1914 expectation said "none" (by the picture the box encloses the meter);
  the tech tree **25 of 25**. `runs/under-{A,B,C}2554.json`, pictures `pics/under-*2554.png`.
- **In-game deny dry runs: 26 of 26** (`runs/deny-ig-2554.json`): load and save Delete, the video panel, the e-mail
  and SMTP fields refused; Back, Load Game, the tabs, Steam Invite, Nick Name, Cancel, Accept allowed.
- **`plot-pixel`** at zoom 17: **9 of 10 within 4 px** (0-1.4 px; one at 6.0 px in y, the ground truth's fit on
  plot 50,14; `runs/plot-Z1-2554.json`), every camera pixel confirmed by `hex`. The ground truth takes 80-190 s per
  plot at this size; the zoom-11 series was stopped after its first plots for time (the 10-plot check at two zooms
  is the 1914x1051 one, 6.3).
- **Restored** at 06:41: `frontend.py restore` (all three front-end files "original"), the SHA-256 backup restored
  and verified equal (DLLs, config.ini, GraphicsSettingsDX11.ini back to 2560x1440, luaexec.enabled), the save copy
  removed (the game's own two autosaves of turn 8 from the loads stay in the instance's `auto` folder), claim released.

### 6.5 What W2 leaves open

- **Instanced controls** (tech buttons, list items, diplomacy-corner and unit action buttons, notifications other
  than the named ones, the city list) have no path and no `label`: neither they nor their containers report
  `HasMouseOver`, and their instance tables are file-local. A hit on one is `pixel`. A unit action button inside the
  portrait button's hit area is named `UnitPortraitButton` (both 1914x1051 and 2554x1411).
- **The candidate table's XML resolution** approximates the VFS (by file name, mods last-loaded first); a mod that
  replaces a context under another file name would be missed `[I]`.
- **`under` speed:** 0.35-2.5 s; the first hit of a control adds a hover search (6-16 s at 1914, longer at 2554).
- **The front-end deny list** holds by picture: a front-end screen the judge does not name (a popup of the set-up
  screen, the mod browser itself) has no regions; MODS is refused so the mod browser is never reached.
- **The product route** was not run by W2: the judge's reads are the same chunks, the picture source is W1's; 4504
  ran the judge there (its findings are fixed above).
- **UI px against client px** is general in the code (`ui_to_client`) but was only seen equal (workstation).
- **The Continue button** of the load screen did not report `HasMouseOver` at its centre (2554x1411): its hit is a
  `pixel` without the label.

## 7. Trace, replay and the stock control (W3)

Ticket 4504 (W3a), impl-4504 (account B), 2026-11-16: sections 7.1-7.3. Code: `tools/hi/trace.py`, `tools/hi/replay.py`,
and the `checkpoint`, `replay` and `check-trace` sub-commands of `tools/hi/hi.py`. Hermetic tests:
`tools/harness/tests/test_hi_trace.py`. Sections 7.4-7.6 are W3b's (4505).

### 7.1 The trace `[C]`

- **Writer:** `trace.append(sess, record)` writes one JSON line per record, then flushes and `fsync`s. W1's
  `session.py` finds it through `session._sibling("trace")`, loaded by file path, because `import trace` gets the
  standard library's module. Record kinds: `header`, `step`, `checkpoint`, `finding` and `end`. Any other kind is
  refused (`TraceError`).
- **Added at write time:** the header's `build` always carries all seven fields (`exe_sha256`, `dll_sha256`,
  `server_sha256`, `proxy_sha256`, `composite_sha256`, `layout`, `pins`). The five hashes come from the instance's
  files (W1's `build_ids`). `layout` and `pins` come from the environment variables `C5_HI_LAYOUT` and `C5_HI_PINS`
  when the caller sets them; the instance's files do not say which layout they came from. Each step gets its `client`
  size and a `wait_spec`: the spec its wait was run from (`ms`, `stable` or `judge` with `cond` and `timeout_ms`),
  written by `session.py` itself since contract v1.8 (2026-11-16, under the lead's permission to change W1's file).
  Before v1.8, W1 recorded only a wait's result, and `trace.wait_spec` rebuilds the spec from it for such traces: a
  judge wait then gets the 30 s default timeout, because the result does not carry its own (4505's 240 s
  `turn_advanced` wait met that). Every `pixel` hit gets `centre_px`: its
  client point measured from the client centre (contract v1.4).
- **`checkpoint NAME [--expect JSON] [--expect-state F1,F2] [--checks a,b]`** takes a picture (`checkpoint_<step>_<name>.png`;
  it becomes the current picture), reads the judge's `state` (not in pure mode), and builds `expect` from `--expect`
  plus the named fields of the state now. The default checks are `not_flat,not_black,not_frozen`. The checks run
  at once, each on its own pictures (contract v1.4): `not_flat` and `not_black` on the picture now; `not_frozen` on
  the previous checkpoint's picture (or `start.png`) and the picture now, with both states; `no_flicker` on a
  `burst 4 1500`. The picture goes into the Visual Checks feed under the session's ticket, `result` pass or fail,
  labelled DIAGNOSTIC (W1's `capture.feed`). One `checkpoint` record follows, with `after_step`, `results` and the
  state. Refusals: an expectation that does not hold at record time is not recorded (exit 2), so a trace never
  carries a checkpoint that failed when it was made. In pure mode, any judge field is refused (exit 2). A bad name or
  check is exit 1, no picture is exit 3, another `--by` is exit 2. A check whose module is not built answers
  `ok: null`, never true.
- **`check-trace TRACE [--journey]`** (offline) checks the schema: the header first with the contract's fields and
  `hi` "1"; `ms` never negative and never going back; step `n` increasing; every action valid for W1's
  `actions.validate`; every delivered point action with a hit of kind `control` (absolute path), `plot` (`[x, y]`) or
  `pixel` (`client_frac`), plus `client_px`; checkpoint names and checks; nothing after `end`. It also checks the
  deny list. Every delivered click, double-click and drag whose control hit is in W2's `deny.DENY_PATHS` fails, and
  so does every delivered key or hold that `deny.key_reason` refuses. Until `deny.py` is merged, a stub refuses only
  ALT+ENTER and the output says so. Actions refused in the recording are not violations. In a journey (a stripped
  header, or a file under a `journeys/` folder) no picture may keep a `file`, `path` or `sha256`. Exit 0 when clean,
  1 on a schema problem or an unreadable trace, 2 on a deny or picture problem only.
- **Journey stripping:** `python -I tools/hi/trace.py strip SRC DST` sets every picture's `file`/`path`/`sha256` to
  null, removes picture paths from check results, keeps the expectations, marks the header `"stripped": true`, and
  checks the result as a journey (D83).

### 7.2 The replay `[C]`

`hi.py -i NAME --by ADDR replay TRACE --route product|stock [--from N] [--until N] [--out DIR] [--ticket ID]`. In
Python the call is `replay.run(trace, inst, route, out, until=None, ...)`, which never raises for a replay that stops
(contract v1.5; W3b's `stockctl` calls it). The replay is itself a session (`session.start` with the trace's width and
pure flag, the trace's ticket unless `--ticket` names another), so the claim, the active-session and the box refusals
are W1's. It writes its own `trace.jsonl` (step records carry `replay_of`, the re-resolved `hit` and the
`recorded_hit`), `replay.json` (the summary), and `replay-stop.json` when it stops. It never launches or stops a game.

- **Order per action:** the box check, the re-resolution, clipping, the deny check, then the input with frame
  `client`. Actions that were not delivered in the recording (`ok` false or null) are listed as skipped, not replayed.
  `key`, `hold` and `type` are replayed as recorded, behind the box and deny checks.
- **Re-resolution (contract 8.2 with v1.4):**
  - **`control`:** `uimap.rect` (the rectangle cache, else the hover search) seeded with the recorded rectangle's
    centre (or the recorded point) placed eight ways: as recorded, scaled to the new client, anchored right, bottom,
    right-bottom and centre, and centred horizontally at the top and at the bottom. The first seed that finds the control wins.
    Up to 6 tries 0.75 s apart (a screen still opening), then exit 6. The click lands at `at` inside the
    rectangle: x = l + at_x * (r - l + 1), the inverse of W2's `resolve_hit`, or at the centre when `at` is null.
  - **`plot`:** `plot-pixel` by the camera without the search, then with it. At each: the centre plus `at` scaled by
    the client-height ratio, then the centre itself, each confirmed by `hex`. Nothing confirmed: exit 6.
  - **`pixel`:** if the channel was silent before the action (the previous recorded step's `judge` reads
    `available: false` with `why` front-end, loading or channel silent; for the first step, the step's own), the hit is
    centre-anchored at the new client centre plus `centre_px`. The front-end menus sit at fixed UI px from the centre,
    and UI px equal client px on the workstation (4501 4.5). Otherwise (contract v1.6, 2026-11-16) each axis is
    anchored by where the recorded point sat: in the first third of the recorded client it keeps its distance from
    the left/top edge, in the last third its distance from the right/bottom edge, in the middle third its distance
    from the centre. This is the way Civ5's UI is laid out: the production list is bottom-left, the end-turn button
    bottom-right. `client_frac` is used only when a hit has no `client_px`. Pure traces have no judge fields, so their
    pixel hits always use these edge anchors. (Was `client_frac` for every in-game pixel hit until v1.6. Exploration
    on 2026-11-16 showed the HUD and city-screen lists are edge-anchored, so a fraction misses them at another size.)
  - **A drag's end** keeps the recorded offset from its start, converted to client px and scaled by the client-height
    ratio.
- **Waits** are replayed from `wait_spec`: an `ms` wait for at least as long, `stable` as recorded, a judge condition as
  a condition (`judge.wait`). A wait that was `ok` in the recording and reaches its deadline in the replay stops it
  with exit 5. A wait whose recording itself ended in `deadline` (4505's 10-turn trace: a first-contact leader
  screen kept the channel silent until the next step's ESC) is replayed as a plain `ms` wait of at least its
  recorded `took_ms`. It never stops the replay and is listed in the summary's `recorded_deadlines` (contract v1.8).
  This holds for step waits and for wait actions.
- **Checkpoints** run where they were recorded (`after_step`), within `--from - 1 .. --until`. The replay takes a
  picture, reads the judge's state (also for a pure trace: the replayer is not the runner) and compares `expect`
  field by field; lists compare as sets. The checks run as at record time. `ok: false` fails the checkpoint and
  `ok: null` is listed only. Every checkpoint picture goes into the feed with pass or fail.
- **`replay-stop.json`** holds: `exit`, `why`, `step` and `action` (recorded numbers), `recorded_action`,
  `recorded_hit` and the `attempt` (seeds, or the plot points tried with what `hex` named), `recorded_picture` (the
  recorded file, or the same name beside the trace) and `now_picture` (a picture taken at the stop; none on a box),
  the judge's reading now (`judge`, or `got` and `diff` for a checkpoint), `checks`, `client` and `recorded_client`,
  `route`, `trace`, `replay_trace` and `time`. Exit codes: 6 for a checkpoint or a re-resolution, 5 for a wait
  deadline, 2 for a refusal (deny list, clipping, claim), 3 for no picture, 8 for a box (never pressed), 1 for
  everything else (a bad trace, the judge not built, no step in the `--from`/`--until` range).

### 7.3 Live proofs `[L]`

**Set-up (2026-11-16 03:45-05:35 EST, impl-4504, account B).** Instance c5h-a (C:, Goldberg, claimed `--ticket 0392
--address impl-4504`, short), product route by run_3308's prep unchanged (scratch wrapper): pins `x64-04b1211b-pfdee656`
(proxy 75C40CEC..., server 5B254430..., x64 DLL 29F58E17..., checked after the prep), `shell_composite.dll` 897E235B...
built by `shell\build.cmd` from `git archive` of fdee65668. The build is not byte-reproducible: 4501 got F6BA4926,
4502 got 500E06E7. Then `frontend.py prepare --mods cp,vp` (activates the mods, loads nothing) and
`GraphicsSettingsDX11.ini` WindowRes 1920x1080 (client 1914x1051, D158), backed up by hash in the prep's backup.
Frame cap 30 fps. Start save `HI4504-T0.Civ5Save`: a copy of c5h-a's own `auto/prev/0ldAutoSave_Initial_0000
BC-4000` (SHA-256 68ed61e6..., Tiny, CP+VP, Russia, Continental Drift, turn 0, settler unmoved); a copy is at
`F:/c5-64bit-work/0392/4504/`. W2 at task-4503-hi-judge@ce886023 (8f7b8945's hit classification and the waiter fix).

**The trace** `F:/c5-64bit-work/0392/4504/rec3/trace.jsonl` (recorded 04:41-04:49): 26 steps, 5 checkpoints, every exit
0; hits control 13, plot 1, pixel 6; `check-trace` clean with W2's deny list. In a player's words: main menu, SINGLE
PLAYER, LOAD GAME, the top save, Load Game (wait `front_end load_screen`), a 20 s wait, ENTER (Continue; wait
`in_game`), click the settler's plot (the plot hit [45,29]), B (Moscow founded, the city screen opens), Warrior in the
production list, Return (wait `screen_closed CityView`), checkpoint `founded` (turn 0, city screen closed), the end-turn
button (CHOOSE RESEARCH: the tech tree), The Wheel, ESC. Then three times: the end-turn button and SPACE for each of
the two units (warrior, pathfinder), the end-turn button with the wait `state.turn>=N`, checkpoint `turnN`. Then ESC,
EXIT TO MAIN MENU, Yes with a 25 s wait, the wait `front_end main_menu`, and checkpoint `menu`. **The trace starts and
ends at the main menu**, so replays run back to back without a relaunch.

Facts met on the way `[L]`:
- **The save is chosen as the top entry of LOAD GAME** under "Sort by Last Modified": a pixel hit on an instanced
  list item. A journey (W4) must make its start save the newest file in `ModdedSaves/single` as a set-up step, or
  select it by its label.
- **ENTER closes the load screen** (Continue Your Journey) like the button, so no hit is needed there. Found City is
  `B`, and SPACE skips the selected unit. The state's `unit` keeps the head unit after a skip, so the end-turn button's
  next selection is the witness. `F` (Fortify) and a click on the Fortify button showed no effect at turn 0 in the
  exploration; not examined further.
- **Exit to the main menu works on the product route** (15.6 s and 1.3 s to `front_end main_menu` at 1914x1051). For
  some seconds after Yes the window does not pump, and PrintWindow times out. W2's waiter treated that as an error
  until ce886023.
- **Exploration rec1 (27 steps)** found that W2's first `resolve_hit` recorded UI clicks as plots whenever `hex` named
  a plot under the cursor: the load screen's button, the city screen's list, and the unit panel's action buttons. Fixed
  in W2 8f7b8945: a plot only when `/InGame/PlotHelpManager/TheBox` has the mouse.
- **Timing:** a front-end step costs 30-40 s while recording, because every judge read waits for the silent
  channel's 8 s timeout. An in-game step costs 3-8 s. A replay of rec3 took 263-422 s.

**5 of 5 at 1914x1051** (04:50-05:24): `F:/c5-64bit-work/0392/4504/rep-1914-{1..5}`. Each one: exit 0, 26 of 26 steps,
checkpoints `founded`, `turn1`, `turn2`, `turn3`, `menu` all ok (expected fields equal; the picture checks `ok: null`,
because checks.py was not merged on this branch). 380, 422, 311, 278 and 263 s.

**The planted change** (05:26): a copy of rec3 with step 11's `/InGame/WorldView/ActionInfoPanel/EndTurnButton`
renamed `EndTurnButtonRenamed` (`F:/c5-64bit-work/0392/4504/planted/trace.jsonl`). It stopped with exit 6 after 10
steps and 6 tries (120 s). `replay-stop.json` holds the step (11), the action (0), the recorded hit and action, the
seeds tried, the recorded picture (`rec3/step_0011.png`), the picture now (`rep-planted/stop_0011.png`), the judge's
state now (turn 0, `ENDTURN_BLOCKING_RESEARCH`), both client sizes, the route, the trace and the replay trace.

**2554x1411** (WindowRes 2560x1440 written before a relaunch; the game was closed by its own EXIT button, because
`cli.py stop` with `turns:stop_readiness` waits for a human turn, which never comes at the main menu after a game).
The first replay stopped at step 4: the step picture never came, because PrintWindow timed out while the EXE loaded.
Since then a step picture is retried 3 times 3 s apart and otherwise listed in `missing_pictures`, because it is
the runner's eyes, not a check; checkpoint pictures stay strict. A return to the main menu showed W2's
`front_end main_menu` never matching at this size (24 polls, `unknown`; the background is drawn letterboxed there),
which rec3's last wait needs. Fixed in W2 20253a7e: a size without its own references uses the nearest size's.

**5 of 5 at 2554x1411** (05:57-06:31): `F:/c5-64bit-work/0392/4504/rep-2554b-{1..5}`. Each one: exit 0, 26 of 26 steps,
all 5 checkpoints ok. 451, 458, 470, 473 and 314 s. Every hit re-resolved at the new size:
- front-end pixel hits centre-anchored: SINGLE PLAYER at client (1277,575), centre -130 px as recorded;
- the settler's plot by the camera and `hex`;
- the city screen's production item edge-anchored (start, end): (90,1196), i.e. 836 + 360;
- the tech tree's The Wheel edge-anchored (start, centre): (598,736), i.e. 556 + 180;
- the end-turn button, a control, re-found at (2375,1191) through its rectangle.
Production and research were chosen, as the next steps' end-turn blocking types show (RESEARCH, then UNITS).

**A lost click (4505, stock replay 4 of rec3, 2026-11-16) and the journey-authoring rule (contract v1.9).** Step 11's
click on the end-turn button did not open the tech tree. Its `stable` wait passed after 2.0 s anyway, step 12's tech
click landed on the map, and step 13's ESC opened the game menu, while `screen_closed TechTree` passed at once. Rule:
an action that opens or closes a screen is followed by a judge wait on that screen (`screen_open X` / `screen_closed
X`), never only `stable`. A wait that passes at once because the screen was never open is not proof. The replayer
does not retry clicks, because a retry can act twice. `check-trace` warns (`warnings`, exit 0) on a click, double-click or
key step whose recorded judge state changed the open screens (`screens`, `city_screen`, `popup`) without such a
wait. On rec3 it flags step 8 (B opens CityView) and step 11 (the end-turn button opens TechTree). **W4's journeys
follow this rule.** rec4 is rec3 recorded again with `screen_open CityView` after B and `screen_open TechTree` after
the end-turn button.

### 7.4 Picture checks without a model (W3b, 4505)

`tools/hi/checks.py`, `run(names, pictures, judge_before, judge_after) -> [{"check", "ok", "value", "threshold",
"why"}]` (contract 8.2). Every picture is sampled at 320 px wide (nearest neighbour, so colours stay exact). "ok" is
True, False (a `wrong_picture` finding) or None (the check cannot run: no usable picture, all `source_glitch`, no
judge, no visible change, fewer than 3 burst frames); None never counts as a pass. Pictures marked `source_glitch` or
`glitch` by `capture.py` are never checked. The checks and their thresholds, measured offline on 2026-11-16 (impl-4505)
on every PNG in `F:/c5-64bit-work/0392/4501/pics` and `F:/c5-64bit-work/0392/4502` (461 pictures: probe 4501's
stock, product and 1080p series and bursts, W1's sessions; both routes, client 2554x1411 and 1914x1051, the instance's
Vibrant asset set) `[C]`. Scratch scripts `measure*.py`, `count.py` (not committed; kept in `F:/c5-64bit-work/0392/4505/scripts/` with every other script of 4505).

| Check | Measure | Threshold | Good frames | Glitch frames |
|---|---|---|---|---|
| `not_flat` | the share of the most common colour (channels quantised to 8 levels) | fails above 0.99 (contract: one colour over 99 %) | at most 0.83 (the load screen; tech tree 0.78) | 1.0, all 21 flat frames (20 grey 84,84,84 and 1 black intro frame) |
| `not_black` | the share of pixels with luma below 16 | fails above 0.95 | at most 0.59 (the tech tree; load screen 0.56) | the black intro frame 1.0; grey flats 0.0 |
| `not_frozen` | the share of sample pixels whose luma changed by more than 24 between the first and the last usable picture, tested only when a visible field of the judge's state changed (`turn`, `unit`, `unit_x`, `unit_y`, `city`, `mode`, `city_screen`, `popup`, `screens`) | fails at or below 0.01 | see below | - |
| `no_flicker` | a-b-a triples in a burst: a->b and b->c mean luma difference above 20, a->c below 10 | fails above 1 alternation | 0 in every series of good frames | see below |

Counts (2026-11-16, thresholds above) `[C]`:
- **Good frames, 431 of 461:** `not_flat` and `not_black` passed 431 of 431.
- **Flat glitch frames, 21:** `not_flat` failed 21 of 21 when given as raw files. `not_black` failed the 1 black one.
  Given as capture records marked `source_glitch`, all 21 answer None.
- **UI-less frames, 9:** these are the server's world without the EXE's UI, found by the top band more than 25
  levels from the series' median band. They pass `not_flat` and `not_black` as raw files: their flat share is 0.28-0.77.
  The picture checks cannot reject them by content. They are rejected only by `capture.py`'s top-band test, which marks
  them `source_glitch`, after which every check answers None. So the capture's glitch test is the gate for these
  frames, not the checks.
- **The stock half-drawn frame** (`S-hov-city_b21`, whole-frame mean 91 against about 169) is one alternation in its
  47-frame burst, so `no_flicker` passes it. This is deliberate: one bad frame is a capture glitch, and flicker is a
  picture that keeps coming back.
- **Flicker positive controls:** product PrintWindow series given with their glitch frames left in show 6 alternations
  (`P-rest`, 20 frames), 1 (`P-popup`) and 2 (W1's `p2` burst). With the glitch frames left out (the records' flag) they
  show 0. Flicker on raw product PrintWindow frames would therefore be the capture's, which is why the flag must reach
  the checks.
- **Noise between consecutive good frames with nothing changing** (the changed share): stock at rest at most 0.0064
  (water and unit idle animation); product at rest at most 0.031; a tooltip appearing in a hover burst 0.031-0.041;
  main menu and game menu 0.0.
- **Real screen changes:** opening the game menu 0.98 (product 0.98); city screen 0.50 (1080p 0.63); tech tree
  0.97 (1080p 0.95); options from the game menu 0.057; the video tab 0.024; a drag (camera only) 0.48. The front end's
  single-player screen changes only 0.006, but the judge cannot read there, so `not_frozen` answers None.
- **What `not_frozen` can and cannot see:** a frozen frame (identical pictures) has a changed share of 0 and fails.
  When the world still animates (stock rest noise up to 0.0064, product up to 0.031), a missing small UI change can
  hide behind that noise. Unit selection (the unit panel) was not in the probes' pictures, so its share is not
  measured `[I]`.
- **One turn on the product route (live, 2026-11-16 04:45-04:52 EST, c5h-f, composite dumps, client 1914x1051)**
  `[L]`: the changed share between consecutive checkpoint pictures of the 10-End-Turn trace (7.6, after
  every end of turn) was 0.067-0.112 over the 10 turns. That is 7 to 11 times FROZEN_MIN, so a turn change is never
  taken for a frozen picture (scratch `frozen_turns.py`).

### 7.5 Findings, reports and the run record (W3b, 4505)

`tools/hi/report.py`; the runner's command is `hi.py finding KIND --text "..." [--checkpoint NAME] [--check NAME]`.
Built 2026-11-16 `[C]`; hermetic tests `tools/harness/tests/test_hi_checks_report.py`.

- **A finding** is a trace record `{"t": "finding", "kind", "text", "signature", "step", "ms", "where", "what",
  "pictures", "judge", "route"}`. The runner's finding carries the session's current picture, and that picture goes
  into the Visual Checks feed under the session's ticket with result `fail` (DIAGNOSTIC). The harness's own findings
  are built by `make_finding`: from picture checks (`finding_from_checks`, one `wrong_picture` per check with "ok"
  False) and from the stock control (7.6).
- **The signature** is `<kind>-<where>[-<what>]`, a folder-safe slug: where is the checkpoint's name or `step<N>`,
  what is the failed check, the differing fields or the box's signature. A box's signature is the dialog watcher's.
  The same fault met in another run therefore has the same signature, whatever the runner wrote.
- **The reproduction filter:** `cut_trace` keeps the header and the trace up to the finding (steps up to its step,
  or up to its checkpoint), then the finding and an end record that names the cut. `reproduce` replays the cut trace
  with two callables, one per fresh instance from the same save. A replay reproduces the finding when:
  - `box`: it exits 8;
  - `hang`: it exits 9;
  - any other kind: the same checkpoint fails (for `wrong_picture`, with the same check), or the replay stops (exit 6)
    at the same step.
  2 of 2 makes a report. 1 of 2, or 1 of 1, stays an observation. A `usability` finding is never a report, and a
  replay that raises counts as not reproduced.
- **The report folder** is `F:/c5-64bit-work/<ticket>/hi/reports/<signature>/`. It holds `report.md` (what, where,
  how often it reproduced, the product and stock results with the class, the save with its hash, the build hashes,
  the steps to reproduce written from the cut trace in a player's words), the cut `trace.jsonl`, `before.png` and
  `after.png`, a copy of the starting save, `build.json` (the trace header's build), and `finding.json`. Writing it is
  refused with exit 2 for less than 2 of 2 and for `usability`.
- **The queue note to the MC** (one per signature): `python -I -X utf8 tools/queue/ticketq.py post <ticket> --by <by>
  --kind note --to MC --text "HI-FINDING <kind> <class> <one line>; report <folder>"`.
  - It is a dry run unless both `post` and `live` are given (`report.py note FOLDER --post --live`). The dry run
    returns the command.
  - Only `patch only` and `stock too` get a note, and only for a folder whose `finding.json` shows 2 of 2.
  - `<reports>/posted.json` keeps the signatures already posted, so a second post is skipped.
  - A failed post returns the queue's rc and error, and the signature is not marked as posted.
- **The run record**: `report.py record RUN_DIR [--product R.json] [--stock R.json] [--compare C.json]` writes
  `<run>/run.json` and the page `<run>/report.md`. They hold the header, the step count and exits, the checkpoints
  of each route (a product-against-stock table with the class when a comparison is given) and every finding with its
  status (`report`, `observation`, `usability`), class and folder.
- **Dry run of the whole chain, offline (2026-11-16 03:50 EST, impl-4505)** `[C]`:
  - Script `F:/c5-64bit-work/0392/4505/scripts/dryrun.py` (scratch); outputs in `F:/c5-64bit-work/0392/4505/dryrun/`
    (`summary.json`, `run_A`, `run_B`, `reports_A`, `reports_B`).
  - Input: W1's real stock trace `F:/c5-64bit-work/0392/4502/s1/trace.jsonl` with two checkpoints added. `drag-done`
    is after step 5. `planted` is after step 7 and expects a state the game does not reach (`popup` true, screen
    `/InGame/NotAScreen`).
  - The replays are fake results of `replay.run`'s shape. Their checks are real `checks.run` calls on W1's step
    pictures.
  - Case A, the planted fault on both routes: `drag-done` `same`, `planted` `stock too`. One finding,
    `wrong_state-planted-popup-screens`. Report folder written; the note in dry run reads `HI-FINDING wrong_state
    stock too checkpoint planted: product: field popup, field screens; stock: ...`.
  - Case B, a flat grey product picture at `planted` where stock's is good (both judges agree): `planted` `patch only`.
    One finding, `wrong_picture-planted-not-flat` (not_flat 1.0 on product, 0.51 on stock). Report folder and dry-run
    note written. The steps to reproduce read "Drag from [330, 560] to [530, 560]", "Hold RIGHT for 1000 ms", and so
    on.

### 7.6 The stock control (W3b, 4505)

`tools/hi/stockctl.py` `[C]`, hermetic tests in `test_hi_checks_report.py`.

- **The route switch.** `RouteSwitch(inst, backup_dir, "stock")` changes only one thing: it sets `vpproxy.ini`
  `mode=off` in the instance's `MODS/(1) Community Patch`, using `proxy/tools/live/set_ini.py`'s `apply`, which keeps
  every other byte, CRLF included. With `mode=off` the proxy loads the pin's 32-bit fallback DLL and starts no server.
  - Before the change the file is copied with its SHA-256 into the run folder (`vpproxy.ini.<sha12>.bak`). The copy's
    hash is checked.
  - The restore writes the original bytes back, checks the hash, and runs `provision.py verify NAME`. Verify's rc is
    recorded, never hidden.
  - It refuses with exit 2: while a game runs from the instance (the proxy reads the file at mod activation), when the
    instance has no product layout (no `vpproxy.ini`), and for a restore while the game still runs (the backup is
    named).
  - Used as a context manager, it restores after an error too.
- **The comparison** (`compare(product, stock)`, two `replay.run` results) gives each checkpoint, in the trace's
  order, one class:

  | Class | When | Finding |
  |---|---|---|
  | `same` | passes on both routes | none |
  | `patch only` | fails on product, passes on stock | yes |
  | `stock too` | fails on both | yes |
  | `stock only` | passes on product, fails on stock | none: listed only (contract v1.5, lead 2026-11-16) |
  | `diverged` | stock never reached it (stock stopped earlier) | none: listed, a parity limit |

  A checkpoint that product never reached has no class, and the row says why.

  Findings come only from `patch only` and `stock too`:
  - an expected judge field not met gives `wrong_state`. It becomes `input_ignored` when the product judge is
    unchanged since the previous checkpoint while stock's changed;
  - each failed picture check gives `wrong_picture`.

  The routes are **in step** when there is no `diverged` row, no visible judge field differs between the routes at a
  common checkpoint (the not_frozen field list), and both replays end with the same exit.
- **The live flow** `run_both(trace, inst, backup, prepare, finish, replay)` runs:
  1. product: prepare, replay, finish;
  2. the switch to `mode=off`;
  3. stock: prepare, replay, finish;
  4. the restore by hash, whatever happens in the replays;
  5. the comparison.

  A stop file ends it between phases. `hi.py` never launches a game: `prepare(route)` (launch and load the save
  through the harness) and `finish(route)` (the harness's stop) belong to the caller.
- **`divergence(pairs)`** counts how many of N product and stock replay pairs stayed in step, with the first
  checkpoint out of step for each pair. This is the 10-turn measurement of contract 8.3.
- **Offline result (dry run, 7.5):** the planted fault is classed `stock too` when it fails on both routes and
  `patch only` when it fails on product only; the clean checkpoint is `same`.
- **Live, the 10-turn divergence (2026-11-16 04:45-05:35 EST, impl-4505)** `[L]`:
  - Setting: instance c5h-f (F:, Goldberg), client 1914x1051 (D158), frame cap 30 fps.
  - Product route: run_3308's prep through scratch `pp.py`, which gives the pins `x64-04b1211b-pfdee656` (proxy
    75C40CEC..., server 5B254430..., x64 DLL 29F58E17...) and a `shell_composite.dll` built from `git archive` of
    fdee65668 (CBE312C6..., not byte-equal to 4501's or 4502's builds of the same commit).
  - Stock route: the same instance with `vpproxy.ini` `mode=off`, so the proxy loads the instance's 32-bit DLL
    `i0ship-26234371-ia` (3C2F47DA...).
  - Save: `HI4505-PP-TINY_0008`, a copy of `C5H-0367-TINY_0008` (b2ceb663..., Polynesia, turn 8).
  - Scene set-up before every session and replay, through the Lua channel (scratch `setup.lua`): every unit's
    activity set to sleep (`SetActivityType(ACTIVITY_SLEEP)`; `PushMission(MISSION_SLEEP, ...)` left the units
    awake), the one city on its most expensive building (no Wealth process this early in VP), the research queue
    filled, auto unit cycle off.
  - **The trace** (`F:/c5-64bit-work/0392/4505/rec10b/trace.jsonl`, recorded on product, keys and judge waits only).
    It covers turns 8 to 18 in 14 steps:
    - 10 times `SHIFT+ENTER` with `judge:turn_advanced`;
    - one `ESC` for a TechAwardPopup on turn 15, and one `ESC` for Greece's first-contact leader screen at the end of
      turn 16, each followed by a `turn_active` wait;
    - 11 checkpoints with `--expect-state turn` and the checks `not_flat,not_black`.
  - **The replays use the copy `trace-edit.jsonl`.** Its step 11 wait is a plain 15 s wait, because the recording's
    `turn_advanced` wait reached its deadline behind the leader screen (the channel is silent there) and 4504's
    replayer of that time stopped on any wait deadline. The copy is fine for this measurement (lead-0392). Two fixes
    go to 4504: a step record that carries its wait spec (a judge wait's timeout was lost and replayed as the 30 s
    default), and a recorded deadline replayed as a plain wait of its `took_ms`.
  - Each replay started from an in-game reload of the save (`shell/tests/tap_live.py reload`, about 20-50 s to in
    game) and the same set-up.
  - **Results:**
    - Product 5 of 5 and stock 5 of 5: exit 0, 14 steps, 11 of 11 checkpoints.
    - `compare` gives every checkpoint of every pair `same`.
    - `divergence`: **5 runs, 5 in step, 0 diverged**.
    - The judge's sequence (turn, gold, research, open screens, selected unit) at the 11 checkpoints was identical
      in all 10 replays: turn 8 to 18, gold 18 to 78 in steps of 6, research 1 then 2 from turn 15. The leader screen
      and the tech popup came on the same turns on both routes.
    - Replay times: product 139-238 s, stock 116-157 s.
  - So a 10-turn key-only trace of a quiet Tiny start stays in step between the routes. Not measured: a busier game
    (units moving, wars), or more than 10 turns `[I]`.
- **Live, 4504's journey trace on the stock route (2026-11-16 05:43-06:50 EST, impl-4505)** `[L]`:
  - The trace `F:/c5-64bit-work/0392/4504/rec3/trace.jsonl` was recorded on product by 4504. It has 26 steps:
    1. at the main menu, Load Game, then the top entry, `HI4504-T0`, a turn-0 start;
    2. Continue;
    3. found the city (the B key), with its city screen;
    4. End Turn with the tech tree and a tech chosen;
    5. three turns;
    6. ESC, then Main Menu and Yes.
    Its checkpoints are `founded`, `turn1`, `turn2`, `turn3` and `menu`.
  - Setting: c5h-c (C:), set up like the 10-turn run: run_3308's prep, then `stockclean` and `mode=off`, client
    1914x1051, `frontend.py prepare --mods cp,vp`. The start save is made the newest file of `ModdedSaves/single`
    before each replay.
  - **Results, 8 stock replays (lead-0392: replay 5 plus up to 3 more, for a lost-input rate):**
    - **6 of 8 passed** (replays 1, 2, 3, 5, 6, 8): exit 0, 26 steps, 5 of 5 checkpoints, about 5-6.5 min each.
      "5 of 5 on stock" was therefore not met.
    - Replay 7 stopped (exit 6) at step 10 (`ReturnToMapButton` not found). The B key had founded Moscow (it is in
      the step picture), but the game did not open the city screen and selected the warrior instead. The input was
      not lost: the game's own auto-open of the city screen after founding did not happen, 1 of 8 on stock and 0 of
      5 on product.
    - Stock replay 4 stopped (exit 6) at step 14. Step 11's posted click on the EndTurnButton ("Choose research")
      did not open the TechTree: its `stable` wait ended after 2.0 s with frame differences [0, 1], where replay 3
      shows a jump of 123. Step 12's click on the tech then landed on the map, and step 13's ESC opened the Game Menu.
      Step 13's `screen_closed TechTree` wait passed at once, because the TechTree had never been open.
    - The comparison against 4504's 5 product replays (`rep-1914-1..5`, all exit 0) is in
      `F:/c5-64bit-work/0392/4505/a-compare.json`. Pairs 1, 2, 3 and 5 are `same` at all 5 checkpoints. Pair 4 is
      `same` at `founded` and `diverged` from `turn1` on.
  - **A route difference that no checkpoint asserts: the selected unit.**
    - At `founded`, after the city view is closed, product has no unit selected and stock has the warrior (1002,
      at 44,29).
    - At `turn1` to `turn3`, product selects the Pathfinder (1003, at 45,30) and stock the warrior.
    - The steps in between show it too: after steps 14-15 (End Turn with SPACE) stock selects 1003 where the
      recording and product have 1002, then none.
    - So the next unit chosen after a screen closes or a turn begins differs between the routes, in every pair.
    - Under `compare`'s strict in-step rule (any visible judge field differing at a common checkpoint) every pair is
      out of step. No checkpoint expects the unit, so all classes stay `same`. It is listed as a parity note, not a
      report. Whether the cause is the x64 DLL or the composite path is not known `[I]`.
  - **Lost inputs** (`lostinputs.py`: the judge after each step compared with the recording's, on screens, popup,
    city screen, selected unit and turn):
    - Of 422 posted actions in the rec3 replays, 1 was lost: the stock click of replay 4.
      - Product: 5 × 36, plus 4 at 2554x1411.
      - Stock: 6 × 36, plus 13 in replay 4 and 9 in replay 7.
      - Replay 7's B key was delivered: the city was founded.
      - The lead's interim figure, 1 of 305, counted the first 4 stock replays.
    - Of 120 keys in the 10-turn replays, 0 were lost.
    - Of 13 hovers in W1's product proofs (5.4), 1 move was lost.
    - The cause is not known `[I]`. The fix is in the recording: a step that opens a screen waits for
      `screen_open NAME`, not `stable`, so a lost or late click fails at its own step. The replayer does not retry
      clicks, because a retried click can act twice (lead-0392).
  - **Stopping after a trace that ends at the main menu:** `cli.py stop` with the human-turn hook refused with rc 6,
    "needs a decision". The channel is silent at the main menu, and `turns.py frontend` answers BUSY "a game was
    loaded ... not a front end" for the rest of the run. So the harness cannot prove the stop safe after a journey
    that returns to the menu.
  - The harness fault is ticket 0411 (MC #2382, 2026-11-16 06:47). Until it lands, the MC approved the game's own
    main-menu EXIT and its "Yes" confirm, as posted clicks by the claim's owner, for any HI trace that ends at the
    main menu with no box and no save in progress, reported in the hand-back. Then `cli.py leftovers` and the hash
    checks follow.
  - On c5h-c (2026-11-16 06:49:51): EXIT at client fraction (0.4995, 0.695), Yes at (0.4995, 0.5124) at 1914x1051.
    The process was gone 12-18 s after the Yes, leftovers were clean, and the files restored by hash were unchanged by
    the exit.
  - For the same reason, `turns.py frontend` cannot be used to wait for the menu between replays of such a trace. A
    15 s settle after the replay's own `front_end main_menu` wait was used instead.
  - Its first IDLE after a launch can also come before mod activation starts. Wait for "activation done": the first
    replay's session start otherwise timed out in PrintWindow while the mods were activating.
- **Preconditions for every HI start save, found here** `[L]`:
  - An instance whose `UserSettings.ini` has `SinglePlayerAutoEndTurnEnabled = 1` races a turn trace: the game ends
    every unblocked turn by itself (the first attempt ran from turn 8 to 17 alone).
    `OptionsManager.SetSinglePlayerAutoEndTurnEnabled_Cached(false)` does not change the live option (the uncached
    getter still reads true), so the file must be changed before launch. c5h-f's was set to 0 for the runs, backed up
    by SHA-256 and restored (5ffa161d...).
  - Units must be asleep, and nothing may complete a production choice in the window. Otherwise the force end of
    turn is refused for units (proxy-design.md 26.2).
- **A crash on the first stock load (2026-11-16 05:15:54 EST)** `[L]`:
  - Configuration: the stock-route launch of c5h-f with run_3308's whole product prep still in place apart from
    `mode=off`, that is, D33's `config.ini` arm (Overlay kept on), the composite's keys (composite, tap, lockstep,
    ring and fence), and `VP64Options.ini` `ServerRenderer = 1`.
  - It ended in the fatal "Game Crash" box during the load: 0xc0000005, a write to 0x0000000c, at
    `CivilizationV_DX11.exe+0x004cc892` (file offset); innermost return candidates EXE+0x6365a, +0x339af0, +0x2570e8.
  - Memory: Sub2G committed 1.64 GB, reserved 0.33 GB, free 0.12 GB, largest free block 11.7 MB at 0x6B89C000.
  - DLL `Release-5.4.8-39-g26234371e+ia440878983`; minidump `CvMiniDump_20261116_051554_5.4.8-39-g26234371e+ia440878983_Release.dmp`.
  - The claim's owner closed it under COMMON 29 (`dismiss --fatal`, `stop --after-fatal`). Evidence is copied to
    `F:/c5-64bit-work/0392/4505/crash-051554/`.
  - **Root cause (corrected 2026-11-18 by lead-0398 from ticket 0534, `crash-ledger-audit.md` 6, MC 07:22):** file
    offset 0x4cc892 is **VA 0x8CD492**, the River/Arrows NULL-batch crash (`RenderBatch::Reset` on a NULL batch,
    `research/02-exe-internals/addresses.md`): D33's `config.ini` arm without the proxy's view-mask clear (3516, D116),
    which `mode=off` does not run. It is not address-space exhaustion `[C]` by the site. (Replaced: "the largest free
    block suggests address-space exhaustion `[I]`" and "whether the D33 arm or the composite keys cause it, or a
    one-off, is not known".)
  - **On a clean stock configuration it did not recur**, as the cause predicts: that run took out the D33
    `config.ini` arm and the composite keys, backed up by SHA-256 and restored after. It loaded, and its 5 stock
    replays all passed.
- **The route switch, live** `[L]`:
  - `RouteSwitch.apply` wrote `mode=off` (2a254b7d... -> 654367e7...).
  - Its restore was refused while the crash box held the game. That is the designed refusal; the ini was put back
    by hash from the switch's backup after the stop.
  - On the second run the restore wrote 2a254b7d... back by itself. `provision.py verify` then answered rc 1, but only
    for run_3308's own prep differences (the VP keys in the ini, the composite files). After run_3308's restore,
    verify was ok with no errors.
- **The harness suite's one failure** was
  `test_safety_children.py::test_scan_records_child_by_parent_and_creation_time`, in both full runs: at 0cd2b7b0
  (04:05 EST, 1012 passed) and at 4701a766 (07:05 EST, after merging 4503 and 4504: 1219 passed, 25 skipped).
  - The failure is an extra conhost.exe child: 3 recorded where 2 were expected.
  - It never failed outside the full suite: the file alone under quietlock passed 6 of 6, and the five HI test files
    followed by it passed 196 of 196.
  - It is not in a file of this package. Whether it depends on the order of the full suite or on load is not
    known `[I]`. W1's suite at 55468c75 had 0 failed, so this is a new failure line on the merged branch.

## 8. Release journeys (W4)

Ticket 4506, impl-4506 (account B), 2026-11-16. Code: `tools/hi/journeys/` (the journeys and their index
`journeys.json`), the release stage `tools/release/stages/hi_journey.py` (one row of the validated tier,
`stages/validated.py`), hermetic tests `tools/release/tests/test_hi_journey.py`; the operator's view is
`tools/release/README.md` section 5a.

### 8.1 What the release candidate is `[C]`

- **The current release candidate is the first playable build** (increment 1, the thin client; GitHub pre-release
  of 0324, the owner's play OK 2026-11-15, D150): the pinned layout `x64-04b1211b-pfdee656` (x64 DLL 29F58E17..., server
  5B254430..., proxy 75C40CEC...), the instance-aware 32-bit DLL `i0ship-26234371-ia` (3C2F47DA...) on test instances,
  0324's shipped purity file (6A0D7B6B...). It has **no composite**: the EXE draws everything, so the product route's
  picture source is PrintWindow, as on stock (contract 4.2's composite rule applies only to layouts with the
  composite, which every earlier HI live run used through run_3308's prep).
- `Release-5.4.8` is the only release the tooling knows (`tools/release/releases/current.json`). Team-3500's work
  folder `C:/c5-64bit-work/3500/releases/Release-5.4.8` holds an older candidate build (x64 `99fb2c1c`, 2026-11-14) with
  no instance-aware DLL (`ia: false`), so it cannot run on an instance; it is not ours to write either (lead-0392,
  2026-11-16). The stage therefore runs in a work folder of our own whose `build`, `purity` and `pack` entries point
  read-only at the first playable's bytes (scratch `seed.py`, recipe in 8.4); only the tag's 32-bit DLL (262AEE88...,
  16.8 MB) is copied, because `pack.py` needs it as a file.

### 8.2 The journeys and the deviation from the scope `[C]`

- **release-basic: "set up a new game" became SET UP GAME opened and closed, then a fixed turn-0 save.** A new game
  started from SET UP GAME gets a random map and seed, so no recorded plot, popup or turn sequence could replay on
  it. The journey opens SET UP GAME (screen coverage), goes BACK, and loads `HI-4506-BASIC-T0` (a copy of 4504's
  `HI4504-T0`, SHA-256 68ED61E6...: Tiny, CP+VP, Russia, Continental Drift, turn 0, the settler unmoved: in effect a new
  game with a fixed seed). Lead-0392 accepted this (2026-11-16).
- **release-large: the scene is set up by Lua before the recording** on a copy of a large save, then saved as the
  journey's start save; the set-up is not part of the journey and its header says so.
- **Every save a journey names starts with `HI-`** (contract 9); the index lists the saves a journey makes
  (`made_saves`), which the stage removes before each replay so that no overwrite prompt can appear.

### 8.3 Recording the journeys `[L]`

**Set-up (2026-11-16 08:33 EST on, impl-4506, account B).** Instance c5h-g (F:, Goldberg; claimed short, ticket
0392, address impl-4506, assert policy hold; the WMC's grant). The release candidate laid as the stage lays it
(scratch `rec.py prep`): the instance's DLL already `i0ship-26234371-ia` (3C2F47DA...), a test pack by `pack.py` (the
tag DLL 262AEE88 plus the instance-aware one as a test-only entry, x64 29F58E17, purity 6A0D7B6B), `provision.py
set-proxy` with the pinned proxy 75C40CEC, VP64/manifest.json and purity.tsv, vpproxy.ini `pack=1`;
`GraphicsSettingsDX11.ini` WindowRes 2560x1440 (f1d9cf01...) -> 1920x1080 and `UserSettings.ini` (85f9d806...)
SinglePlayerAutoEndTurnEnabled 1 -> 0, both backed up by SHA-256; `frontend.py prepare --mods cp,vp,eui`. Client
1914x1051, picture source PrintWindow on the product route (no composite), 0 retakes needed on most shots. Mod
activation took 135 s after the launch.

**The mod set is cp,vp,eui** (CP, VP and VP - EUI Compatibility Files, the installer's FullEUI set; MC #2508,
2026-11-16 09:23: HI journeys use the harness default set unless a charter says otherwise). A first recording of
release-basic under cp,vp (08:38-09:11, `F:/c5-64bit-work/0392/4506/rec-basic/`, kept as
`release-basic-cpvp-superseded.jsonl` there) is superseded. The start saves list only CP and VP in their headers: no
save on the instances lists EUI (a UI-only mod is not written into a save), so loading them with cp,vp,eui active is
what a FullEUI player does `[C]`.

**release-basic** (recorded 09:31-09:47 on product under cp,vp,eui, `F:/c5-64bit-work/0392/4506/rec-basic2/trace.jsonl`,
65 steps; the journey is 63 of them): main menu, SINGLE PLAYER (`front_end single_player`), SET UP GAME (`front_end
setup_game`), Back, LOAD GAME, the top entry, Load Game (`front_end load_screen`), a 30 s wait, ENTER (`in_game`), the
settler's plot, B (`screen_open CityView`), Warrior, Return (`screen_closed CityView`); checkpoint `founded`; the end-turn
button (`screen_open TechTree`), The Wheel (`screen_closed TechTree`: with EUI the choice closes the tree); checkpoint
`research` (research 9); ten turns of the end-turn button + SPACE per unit and the end-turn button with
`state.turn>=N`; at turn 8 the finished warrior blocks with PRODUCTION: the end-turn button opens the city screen,
Monument (`screen_closed CityView`: the choice closes it); checkpoint `turn5`; the turn-10 start opens the
TechAwardPopup (`screen_open TechAwardPopup` on the last end of turn), ESC closes it; checkpoint `turn10` (turn 10,
research -1, gold 42); the game menu by its button (`/InGame/TopPanel/MenuButton`), SAVE GAME (`screen_open SaveMenu`),
END + 40 BACKSPACE + `type HI-4506-SAVED` (read back exactly), Save (`screen_closed SaveMenu`; 264,301 B written);
LOAD GAME (`screen_open LoadMenu`), the top entry (HI-4506-SAVED), Load Game (`front_end load_screen`, 10.5 s), 30 s,
ENTER (`in_game`); checkpoint `loaded` (turn 10, research -1, gold 42); the game menu, EXIT TO MAIN MENU, Yes
(`front_end main_menu`, 9.7 s); checkpoint `menu`; EXIT, Yes (no picture: the process was gone within 15 s, leftovers
none). check-trace: clean, no warnings (63 steps, 6 checkpoints, hits control 43, plot 1, pixel 12, 146 actions).

Facts met on the way `[L]`:
- **A click into the save menu's name box takes the box's focus away**: the posted END, BACKSPACE and `WM_CHAR`s after
  it changed nothing (read back through the channel: the default `Catherine_0010 BC-3600`; first recording). Typed
  into the menu as it opens (the box has the focus then, W1 5.2), the same keys gave `HI-4506-SAVED` exactly. A
  journey types without clicking the box.
- **Choosing a production in a city screen that the end-turn button opened (production blocking) closes the city
  screen by itself** (both mod sets); a Return click after it lands on the map. Wait `screen_closed CityView` on the
  choice.
- **With EUI, choosing a tech in the tech tree closes the tree**; an ESC after it opens the game menu. Without EUI the
  tree stays open until ESC.
- **The turn-10 start opens the TechAwardPopup** (a finished tech); an ESC closes it. The game menu is opened by its
  own button, which is the same on both routes and both mod sets.
- **One slip was edited out by hand** (contract 8.1 allows a journey of hand-written actions; the edit is named in the
  journey header's `edited` list; scratch `edit_basic2.py`): step 14's wait `stable` -> `screen_closed TechTree`, and
  steps 15-16 removed (an ESC that opened the game menu on the closed tree, and the ESC that closed it). The replays
  below are what proves the edited journey.
- Mod activation after a launch took 101-135 s on c5h-g (F:).

**release-large** (recorded 10:06-10:18 on product under cp,vp,eui, `F:/c5-64bit-work/0392/4506/rec-large3/trace.jsonl`,
21 steps, unedited; two earlier attempts `rec-large`, `rec-large2` abandoned). The scene save: a copy of 9071's
`9071-LC21B-T200` (e3da1d17..., Large Communitu, 21 civs, turn 200, the Netherlands; CP+VP) loaded on c5h-g by the
harness's auto-load, then the Lua set-up `HI-4506-LARGE-SCENE.setup.lua` (kept beside the save on F:): every unit of the
player asleep, two swordsmen of the player near Amsterdam (49,52): U1 13610 at 47,50, U2 13611 at 46,53, a barbarian
warrior at 47,54; `UI.SaveGame("HI-4506-LARGE-SCENE")` -> `F:/c5-64bit-work/0392/4506/saves/HI-4506-LARGE-SCENE.Civ5Save`
(SHA-256 c01bb2bf..., 1,673,356 B). The set-up is not part of the journey; its header says so. The journey: main menu,
SINGLE PLAYER, LOAD GAME, the top entry, Load Game (`front_end load_screen`), four 60 s waits (a step wait is at most
60 s; the load took about 110-160 s), ENTER (`in_game`; U1 is selected and the camera is on it); U1's plot (plot hit),
right click on 48,50 (`state.unit_x==48`); checkpoint `moved` (unit 13610 at 48,50: the journey selected it); a drag of
the map (300 px down); U2's plot (`state.unit==13611`); right click on the barbarian's plot (the attack: "Your
Swordsman (8 damage) destroyed an enemy Warrior!", U2 moves in; 4 s); checkpoint `attacked`; a click on plot 50,50
(meant for the banner; it re-centres the camera; its 10 s wait reached its deadline and is replayed as a plain wait);
Amsterdam's banner (`screen_open CityView`); Return (`screen_closed CityView`); checkpoint `city`; a drag, RIGHT held
1.5 s, wheel -3 and +3 (each with a `stable` wait); checkpoint `camera`; the game menu, EXIT TO MAIN MENU, Yes
(`front_end main_menu`, 11.2 s); checkpoint `menu`. check-trace clean (21 steps, 5 checkpoints, hits control 4, plot 10,
pixel 4). The game was then stopped by `cli.py stop` at the main menu (0411 merged: `stopped: true`).

Facts met on the way `[L]`:
- **On this setup a left click on the map re-centres the camera** and a click on a city's tile selects a unit in it
  (Amsterdam held 3); the city screen opens from the city banner. The banner is recorded as a `plot` hit (the judge's
  world witness TheBox has the mouse over the banner), which replays because the camera is re-centred by the recorded
  click before it.
- **The scene's combat is the same in every run so far** (2 of 2 recordings): the barbarian warrior destroyed, U2 at
  8 damage. Whether the stock route gives the same result is the stock arm's to show.
- **The load screen's Continue button has no path the judge's `control` read finds** (`/InGame/LoadScreen/...`,
  `/LoadScreen/...`, `/FrontEnd/LoadScreen/...` all nil from the InGame and the default state while it waits; probe
  `probe_ls.py`); `frontend.py ingame` sees it by its Lua state. A journey therefore waits a fixed time before ENTER.

**Instance alignment (MC #2549, 2026-11-16 09:59).** Both journeys were recorded on c5h-g **before** it was aligned to
a real VP 5.4.8 set-up. c5h-g's EUI compatibility mod equalled c5h-a's file by file (44 files and the modinfo
47b0de6b by SHA-256), and cp,vp,eui activated it (`modsToEnable` CP@151, VP@17, 24923240...@1). `provision.py align548
c5h-g` (10:22, impl-4506, on a new claim) then replaced `MinorCivSounds_VoxPopuli.xml`, added `Text/VPUI_tips_en_us.xml`
and copied `(4a) Squads for VP` (not activated by cp,vp,eui); a full `verify` was ok. The stage runs below are after
alignment.

### 8.4 The release stage `[C]`

`tools/release/stages/hi_journey.py`, the README's section 5a. In short: one claimed instance; the release laid with the
`instance` stage's helpers (imported, not copied: `manifest`, `mods_check`, `fix_modinfo`; since ticket 0650,
2026-11-23, the undo entries are the stage's own `undo_fn` records instead of `_cmd`, `_rm`, `_put_back`, section 8.10);
a test pack of its own (`<work>/hi_journey/pack`); per journey and route the mode, the front end, the start save made
the newest, the harness's launch, guard check and activation wait, `hi.py replay`, the journey's end and `cli.py stop`
for what still runs, leftovers; the comparison by `stockctl.py compare`; the restore always (also under a stop file);
`report.md` and `report.json` in `<work>/hi_journey/`, and the validated report's `hi_journey` section.
- **It reports, it does not gate** (corrected 2026-11-16, D162, ticket 0436: a take with `--for-art` gates, a fail or a skip is a stop of the tier; `release-driver.md` section 0436; other takes as below): failed journeys leave it `ok` with `verdict: failed`; journeys that cannot be
  played make it `skipped`. Both count as reached for `--tier validated`, and `report`'s verdict is unchanged by them
  (test `test_the_tier_verdict_is_not_gated_by_a_failed_journey`).
- **Hermetic tests**: `tools/release/tests/test_hi_journey.py`, 33, a fake tool written into the temporary folder and
  the real `stockctl.py compare`; about 6.7 min (each test starts some 40 short processes). The full `tools/release`
  suite (285 at 4506's first run) had one failure, `test_validated_tier_is_candidate_plus_registry`, which pins the
  registry list; with lead-0392's OK the list names `hi_journey` (2026-11-16). At hand-back (12:12 EST, all under
  quietlock): `test_validated_tier*.py` 76 OK, `test_hi_journey.py` 33 OK; the full suite before the last guard-order
  fix: 292 run, 4 failures, all four whole-tier tests where the tools guard came before the skip on missing saves
  (fixed: the guard now comes after the reasons to skip); the harness suite 1404 passed, 0 failed, 25 skipped.
- **Two faults found by the tests and fixed** `[C]`: a stage that reads a tool's output from `RunResult.text` sees the
  driver's own `=== ...` header and exit lines too (the activation wait never saw its IDLE line last and spun for
  600 s); and a restore under the driver's stop file raised `StopRequested` at its first command (the restore now runs
  to its end with the stop check off, then the stop is passed on).
- **A work folder of our own** (lead-0392: team-3500's folder is not ours to write): scratch `seed.py` writes a
  `state.json` for Release-5.4.8 whose `build`, `purity` and `pack` entries are `ok` and point read-only at the first
  playable's bytes (`<pins_dir>/dlls/x64-04b1211b-pfdee656`, `i0ship-26234371-ia`, 0324's purity and pack),
  `config.fork` = an existing fork clone (`D:/c5-64bit-work/9045/fork`, only checked to be a clone), and copies the
  tag's DLL into `inputs/player-x86/`. Then `take_release.py Release-5.4.8 --tier validated --only hi_journey --work
  F:/c5-64bit-work/0392/4506/rel --who 0392` with `C5_RELEASE_HI_INSTANCE=c5h-g C5_RELEASE_HI_BY=impl-4506
  C5_RELEASE_HI_ADDRESS=impl-4506` (scratch `stage_run.sh`).
- **The tools guard** (lead-0392, 10:3x): every command of the stage comes from the driver's tools when named there;
  a driver whose tools are not the defaults (a test's fakes, a `--tools` file) must name all of `NEEDS`, or the stage
  fails before any claim. Found on 2026-11-16 10:25: 3506's whole-tier test (`test_validated_tier.py`, fakes for its
  own tools only) ran this stage once the start saves existed on F:, and the real `set_ini.py` ran against the fake
  instance (the run then failed at the restore). The fixture now also points `C5_RELEASE_HI_SAVES` at an empty folder.

### 8.5 The stage live: acceptance run and the planted fault `[L]`

**Acceptance run (2026-11-16 10:23:17-11:07:32 EST, 44 min, workstation, not for timing).** `take_release.py
Release-5.4.8 --tier validated --only hi_journey --work F:/c5-64bit-work/0392/4506/rel --who 0392 --gate-wait 30` (at
4506's branch after 10:22), exit 0, stage `ok`, **verdict passed**. Instance c5h-g (F:), aligned (align548, 10:22) and
verified before the run; claimed by the stage (short, ticket 0392, impl-4506) and released by it. The release candidate
= the first playable's pinned bytes (8.1); the stage's own test pack (x86 262AEE88 + the instance-aware 3C2F47DA as a
test-only entry, x64 29F58E17, purity 6A0D7B6B), pack=1; client 1914x1051 (WindowRes 1920x1080 from f1d9cf01...),
SinglePlayerAutoEndTurnEnabled 0 (from 85f9d806...), VP64Options.ini 9497f85b... (defaults, untouched); cp,vp,eui.

| Journey | Route | Exit | Steps | Checkpoints | Replay time |
|---|---|---|---|---|---|
| release-basic | product | 0 | 63 of 63 | founded, research, turn5, turn10, loaded, menu: all ok | 570 s |
| release-basic | stock | 0 | 63 of 63 | the same 6, all ok | 499 s |
| release-large | product | 0 | 21 of 21 | moved, attacked, city, camera, menu: all ok | 437 s |
| release-large | stock | 0 | 21 of 21 | the same 5, all ok | 433 s |

- `stockctl.py compare`: release-basic `same` 6 of 6, release-large `same` 5 of 5, both **in step** (no judge field of
  the not_frozen list differs between the routes at any checkpoint).
- release-basic's EXIT and Yes ended the process within 1 s of the replay's end (0.9 s product, 0.6 s stock; the 60 s
  rule never came near); release-large ended at the main menu and `cli.py stop` stopped it there (21 s, 0411). The
  stock replay of release-basic removed the `HI-4506-SAVED` that the product replay had made, as designed.
- Leftovers: all four `cli.py leftovers` answered `[]` (read from the logs: the stage itself recorded them as
  "unreadable" because `cli.py` prints its JSON over several lines; fixed after the run by `json_of`, with the fakes
  now printing indented JSON, test `test_leftovers_fail_the_stage`).
- Restore: ok, every undo done (made saves, both start saves, the two ini files back by hash, the pack files, vpproxy.ini
  back by hash, set-proxy --original, frontend restore with the three files `original`), `verify --quick` rc 0.
- Report: `F:/c5-64bit-work/0392/4506/rel/hi_journey/report.md` and `report.json` (the release folder), the replays
  under `.../rel/hi_journey/runs/`; the validated report there names the hi_journey section and, being a work folder
  with only build/purity/pack seeded, reads "validated tier not reached (survey pending)", as expected.
- Checkpoint pictures went to the Visual Checks feed under issue 4506 (pass, DIAGNOSTIC).

**The planted fault (11:08-11:21 EST).** A copy of release-basic whose checkpoint `founded` also expects
`/InGame/TechTree` open (a screen a changed option would never open there; scratch `plant.py`, index
`F:/c5-64bit-work/0392/4506/planted/journeys.json`), run by the same stage from its own work folder
(`.../rel-planted`, `C5_RELEASE_HI_INDEX`): exit 0, stage `ok`, **verdict failed** (reported, not gated).
- Product: **caught**: exit 6 at checkpoint `founded` after 12 steps (164 s), `screens expected ['/InGame/TechTree'] got
  []`; `replay-stop.json` with the picture now; the game was stopped by `cli.py stop` in game (27 s).
- Stock: exit 5 after 9 steps: **the game's own city screen did not open after B** (Moscow founded, the warrior
  selected, "CHOOSE PRODUCTION" on the end-turn button; `stop_0010.png`), so step 10's `screen_open CityView` reached
  its deadline. The comparison names `founded` `diverged`.

**A known game behaviour that release-basic depends on** `[L]`: the city screen's auto-open after founding the first
city failed on stock in 1 of 8 replays of 4504's trace (4505, 7.6) and 1 of 2 stock replays here (0 of 3 on product
here). release-basic records the city screen opening after B (contract v1.9's screen wait), so such a replay stops at
step 10 with exit 5 and the stage reports the journey failed or incomplete. It is not an input loss (the city is
founded) and not a patch fault (stock). No recorded action is neutral to both states (Return, ESC and a banner click
each act differently whether the screen opened or not), and the replayer has no conditional step.
**Ruling (lead-0392, 2026-11-16 11:2x):** the journey keeps the founding (it is in the scope), and the stage reruns the
whole journey once from a fresh launch and load when a replay stops exactly on this (exit 5, recorded step 10, wait
`screen_open CityView`; the index's `known_flakes`). The verdict is then "passed on rerun (known: city screen did not
auto-open after founding)" with both run records; any other stop, or a second stop, stays failed. This is a
journey-level rerun, not an input retry, so contract v1.9 holds. Hermetic tests: `KnownFlake` in
`test_hi_journey.py` (flake then pass, flake twice, another step not rerun, a journey without the flake not rerun).
Not run live: the rerun path itself (the acceptance run did not meet the flake; the planted run met it before 4506's
rerun code existed).

### 8.6 The journeys of the 43-civ variant (ticket 0580) `[C]` code, `[L]` saves and runs

Ticket 0580, impl-0580 (account B), 2026-11-19, from 0574 Q17 (#7103) and the MC's answer (b) (#7106).

**Why they exist** `[L, 0574, 2026-11-19 17:13]`: rc0b-m43's hi_journey product route stopped at the start. The server
refused `HI-4506-BASIC-T0` by design: server.log `FAILED: save-variant: the save was made with a 22-civilization build
of Vox Populi ..., the running DLL has 43 major civilizations` (exit at 105 s, no crash, no box; run
20261119-171318-10160-1 on c5h-b; the refusal is proxy-design.md 30). The stock route of the same take (the m43
instance-aware DLL) could not play the 22-civ saves either (both journeys exit 2). Both start saves are 22-civ saves,
so no 43-civ build could pass D162 with the kit as it stood.

**The stage** `[C]` (`tools/release/stages/hi_journey.py`):
- Each journey in `journeys.json` names the major-civilisation count its start save was made with (`major_civs`;
  absent = 22; a whole number 2..62, else the index is refused).
- The take's count is the build stage's `major_civs` output (written for a count other than 22), else the config's
  `major_civs` (`--major-civs`), else 22 (`take_major`). The stage plays only the journeys of that count; one named in
  `C5_RELEASE_HI_JOURNEYS` of another count is refused, and a count with no journeys is "skipped" (not played). The
  outputs record `major_civs`.
- D162 per count (`D162_BY_MAJOR`): 22 asks release-basic and release-large; 43 asks release-basic-m43 and
  release-large-m43; any other count is refused for the owner ("no D162 journeys are defined"). `art_check` reads the count
  from the build's outputs, else from the stage's own record.
- The stage's test pack gets `pack.py --major-civs N` for N other than 22: the manifest then records the count, and a
  take without `inputs/player-x86` takes the tag's `(3b) 43 Civs` DLL instead of the 22 one (before 0580 the stage
  passed no count; rc0b-m43 had the (3b) DLL in `inputs/player-x86`, so its pack was right by that file).
- Hermetic tests: `tools/hi/journeys/tests/test_m43_selection.py` (selection, the index field, the take's count,
  D162 per count with negatives, and the committed index: both m43 journeys with count 43, their own start-save
  SHA-256, their traces clean under `check-trace --journey` and with the 22-civ journeys' checkpoint names).

**The inputs** `[L]`: rc0b-m43's bytes (0574's take `F:/c5-64bit-work/0574/rc0b-m43/run`, read only) copied to
`F:/c5-64bit-work/0580/inputs/` and checked by SHA-256 (27 files): the layout `x64-1b87dd18-m43-pa02bda4`, the
instance-aware 32-bit DLL `i0ship-f33fc46a-m43-ia` (A071F976...), the plain `i0ship-f33fc46a-m43` (9300FD34...), the
tag's `(3b) 43 Civs` DLL (01451BD7...), purity.tsv (B6E4C2CD...).

**The instance** `[L]`: c5h-c (C:, Goldberg), claimed for 0580 (MC-B's grant #7137), laid as the stage lays a
release (scratch `C:/c5-64bit-work/0580/rec.py prep`, 4506's recipe with the m43 bytes): set-dll the m43 -ia DLL
(the instance's DLL e589306e... kept), a test pack by `pack.py --major-civs 43` (x86 01451BD7 + A071F976 as test only,
x64 85C0CD53, purity B6E4C2CD, manifest `major_civs: 43`), set-proxy with the layout's proxy, the pack files, pack=1;
WindowRes 1920x1080 (from f1d9cf01...), SinglePlayerAutoEndTurnEnabled 0 and AutoUnitCycle 1 (from 85f9d806...),
vpproxy.ini c7aedfb5... backed up by hash; front end checked `original` before and after every launch.

**The start saves** `[L, 2026-11-19]` (all in `F:/c5-64bit-work/0392/4506/saves/` beside the 22-civ ones; never
committed, D83):
- `HI-0580-BASIC-M43-T0` (SHA-256 513f5ba9c2ee..., 210,104 B): a new game made on c5h-c's **stock route** (mode=off,
  the m43 -ia DLL) by the vp-dll-dev front end's `newGameSetup` (`map=Continental_Drift;size=WORLDSIZE_TINY;ais=3;
  minors=-1;handicap=HANDICAP_EMPEROR;human=CIVILIZATION_RUSSIA`; `human=` is a scratch addition to a copy of the
  skill's MainMenu.lua, `C:/c5-64bit-work/0580/fe-assets/`, setting `PreGame.SetCivilization(0, ...)`), the load
  screen closed by `frontend.py dismiss`, then `UI.SaveGame` at turn 0. Read in game before the save: turn 0, Russia,
  Emperor, 4 majors, `GameDefines.MAX_MAJOR_CIVS` 43, settler 43,14, warrior 44,14, pathfinder 42,13 (unmoved). The
  22-civ `HI-4506-BASIC-T0` is the same set-up (Tiny, Continental Drift, Russia, Emperor, 4 majors) on another map.
  The front-end new game took 17:50:40-17:55 (about 4 min with mod activation; the load screen waited for its click
  the whole time the script polled).
- `HI-0580-L30M43-T200` (43eb3f95..., 2,233,973 B): Large Communitu (87 x 59), **30 majors** (29 alive at T200) and 20
  city states, Prince, spec seeds 580/581, played headless from turn 0 by rc0b-m43's x64 server
  (`server/run_server.py SPEC --turns 200 --bin <layout>/bin --db F:/c5-64bit-work/9071/lc-db/Civ5DebugDatabase.db
  --save-at-start ... --save-out ...`; the client database of 9071's LC21B, metrics-ledger 8.2), exit 0 in 537 s
  (1.0-1.7 s a turn to T65, 2-3.5 s at T100-T170; working set 173-202 MB), 0 engine calls outside the unserved
  allow-list. Player 0 (the active player) is the Ottomans, 4 cities at T200. The turn-0 save `HI-0580-L30M43-T000`
  (ac5a2f5c...) is kept beside it.
- `HI-0580-LARGE-M43-SCENE` (96b54368..., 2,235,495 B): `HI-0580-L30M43-T200` loaded on c5h-c's stock route
  (frontend auto-load), 4506's scene Lua unchanged (`HI-0580-LARGE-M43-SCENE.setup.lua` beside it, 514f3f8a...),
  then `UI.SaveGame`. Its answer: `cap 29,12 U1 18200 at 30,9 -> 31,10; U2 18201 at 27,9 attacks barbarian 18202 at
  28,10; turn 200`. Launch 17:56:55 to in game about 18:01 (the load of a 30-major Large T200 on stock).

**How the m43 journeys were recorded** `[L]`: on c5h-c's **product route** (mode=on) with the client 1914 x 1051, the
size of the 22-civ recordings, so the 22-civ journeys' shot px apply unchanged. Scratch `C:/c5-64bit-work/0580/drive.py`
issues the 22-civ trace's steps one by one through `hi.py step --json -` (the recorded actions without their hits,
the recorded wait spec as `--wait`) in an open recording session, and its checkpoints as `hi.py checkpoint NAME
--expect-state <the same fields>` (the values read from the game now); a step whose target is on the map, or which
must differ, is given by hand. The session's own trace is then stripped (`trace.py strip`) and its header filled in
(save, mods, set_up, `recorded`, `major_civs`, `edited`: scratch `edit_header.py`, as 4506's edit scripts did,
because `session start` takes no save).

**release-basic-m43** (recorded 18:12-18:24 EST on product; `C:/c5-64bit-work/0580/rec-basic/trace.jsonl`):
- A first attempt (18:05-18:10, `rec-basic-a`) met the known flake at its step 10: after B the city was founded but
  the city screen did not open (`screen_open CityView` deadline at 15 s; the picture shows Moscow founded, the warrior
  selected, CHOOSE PRODUCTION on the end-turn button). This is 8.5's game behaviour, now seen on a 43-civ product route
  too (1 of 2 recording attempts). Abandoned; recorded again from a fresh launch.
- The second attempt went through with **every step of the 22-civ journey, unchanged, by drive.py**: the settler was
  selected and centred after the load as in 4504's save, so step 9's click at shot (640, 352) hit the settler's plot
  (43,14); B opened the city screen; the production list's Warrior and the tech tree's The Wheel sat at the same px;
  every turn needed the same two SPACEs (warrior, pathfinder); at turn 8 the end-turn button opened the city screen
  for production (ENDTURN_BLOCKING_PRODUCTION) and Monument closed it; turns 8-9 needed three SPACEs; the turn-10
  start opened the TechAwardPopup. The checkpoints read **the same values as the 22-civ journey**: founded (turn 0),
  research 9, turn5 (turn 5, research 9), turn10 (turn 10, research -1, gold 42), loaded (the same), menu. Only the
  typed save name differs: step 52 types `HI-0580-SAVED` (END, 40 BACKSPACE, `type`; read back exactly through the
  channel). The game's EXIT and Yes ended the process (c5h-c `not running`, leftovers clean).
- check-trace `--journey`: clean, 63 steps, 6 checkpoints, hits control 43, plot 1, pixel 12, 146 actions: the same
  counts as release-basic.

**release-large-m43** (recorded 18:43-18:53 EST on product; `C:/c5-64bit-work/0580/rec-large/trace.jsonl`, 20 steps,
unedited apart from the header):
- Steps 1-6 from release-large unchanged (SINGLE PLAYER, LOAD GAME, the top entry, Load Game, four 60 s waits, ENTER
  `in_game`). **The load on the product route took about 60 s** (Load Game at 18:31:5x, the load screen's Continue
  shown at 18:32:48; read by `frontend.py ingame` beside the session), against about 4-5 min from launch to in game on
  stock (with mod activation); the 240 s of waits are kept for the stock route.
- After the load **U2 (18201) is selected and the camera is on it** (4506's scene Lua ends with `UI.LookAt(P)` on U2's
  plot), unlike release-large where U1 was. The plots' pixels were read with `judge.py plot-pixel` before each click
  (not part of the trace): U1 30,9 at client (1629, 526), its target 31,10 at (1692, 397), U2 27,9 at (957, 526),
  the barbarian 28,10 at (1062, 397).
- The steps: U1's plot (plot hit 30,9; `unit` 18200); right click on 31,10 (`state.unit_x==31`); checkpoint `moved`
  (turn 200, unit 18200 at 31,10); a drag of the map 150 shot px down (release-large's was 300: with 300 px U2's plot
  went under the bottom edge's interface); U2's plot (`state.unit==18201`); right click on the barbarian (4 s);
  checkpoint `attacked` (turn 200); **Ankara's banner** (plot hit 32,8; `screen_open CityView`); Return
  (`screen_closed CityView`); checkpoint `city`; then release-large's steps 15-21 unchanged (drag, RIGHT held 1.5 s,
  wheel -3 and +3, the game menu, EXIT TO MAIN MENU, Yes: `front_end main_menu` in 6.3 s); checkpoints `camera`, `menu`.
  The checkpoint names are release-large's five.
- **The combat differs from release-large's** `[L]`: "Your Swordsman (7 damage) attacked an enemy Warrior (87 damage)":
  the barbarian survives with 13 HP (both attempts gave the same result; whether the replays do is the stage's to show).
- **A first attempt was abandoned** (18:29-18:41, `rec-large-a`): (a) after release-large's 300 px drag and a drag back
  (which moved the map about 690 client px, not 450), U2 was clicked at a new place; (b) with the barbarian alive next to
  Istanbul (29,12, the capital) and Edirne, **a click on Istanbul's banner selected the city for its ranged strike**
  (tooltip "Click on this to have the city conduct a ranged attack"; the city overlay shown, `city` 1131, no
  CityView), and a second click took it back: two steps whose 10 s CityView wait ended at the deadline. A read-only
  Lua read gave the cities and `CanRangeStrikeNow()`: Istanbul 29,12 true, Edirne 28,8 true, Ankara 32,8 false,
  London 32,3 false. Hence Ankara's banner in the journey. A replay meets the same: the save, the actions and so the
  barbarian's survival are the same.
- check-trace `--journey`: clean, 20 steps, 5 checkpoints, hits control 4, plot 9, pixel 4, 23 actions.
- **Privacy edit** (both m43 journeys): the judge's error texts of channel silences carried the instance's local cache
  path (`lua: no answer within 8 s (... cache <path>)`, 9 and 6 places); scratch `scrub.py` replaced it with a
  placeholder and named the edit in the header's `edited`; check-trace clean after it. The 22-civ journeys have none.

**The acceptance run** `[L, 2026-11-19 18:56:29-19:38:32 EST, workstation, not for timing]`: `take_release.py
Release-5.4.8 --tier validated --only hi_journey --major-civs 43 --work F:/c5-64bit-work/0580/rel-m43 --who 0580
--gate-wait 30` with `C5_RELEASE_HI_INSTANCE=c5h-c C5_RELEASE_HI_BY=impl-0580 C5_RELEASE_HI_ADDRESS=impl-0580
C5_RELEASE_HI_TICKET=0580`, exit 0, stage `ok`, **verdict passed**, "D162 HI release journeys of Release-5.4.8: verdict
passed (stage ok) ...; for the owner: yes (both journeys passed on this build, product and stock)".
- The work folder is 0580's own: scratch `seed43.py` wrote a state whose `survey`, `build`, `purity` and `pack` entries
  are rc0b-m43's own (0574's state, read only) with the file paths moved to the SHA-256-checked copies (14 files checked
  against the build's recorded hashes), the config rc0b-m43's (`major_civs` 43, `for_art` true; its `descriptor` key
  dropped: 0574's descriptor has an `options` field this branch's descriptor.py does not know, "unknown field", exit 2
  on the first try). So the candidate the stage recorded is rc0b-m43's: x64 1b87dd18..., i0 f33fc46a..., x64 DLL
  85C0CD53..., proxy 6CEA820E..., server BB4E8F0E..., the m43 -ia DLL A071F976... (`index_committed` true, `major_civs` 43).
- The stage played only the m43 journeys (the take's count from the build's outputs). Its test pack was made with
  `pack.py --major-civs 43` (x86 01451BD7 + A071F976 test only).

| Journey | Route | Exit | Steps | Checkpoints | Replay time |
|---|---|---|---|---|---|
| release-basic-m43 | product | 0 | 63 of 63 | founded, research, turn5, turn10, loaded, menu: all ok | 614 s |
| release-basic-m43 | stock | 0 | 63 of 63 | the same 6, all ok | 480 s |
| release-large-m43 | product | 0 | 20 of 20 | moved, attacked, city, camera, menu: all ok | 412 s |
| release-large-m43 | stock | 0 | 20 of 20 | the same 5, all ok | 434 s |

- `stockctl.py compare`: release-basic-m43 `same` 6 of 6, release-large-m43 `same` 5 of 5, both in step. Leftovers
  none; restore ok (verify rc 0); the claim released by the stage at 19:38:32. No box at any time (dialog watcher:
  `boxes_seen` 0 on every launch). The known city-screen flake did not occur in either replay.
- **One crash without a box, root cause known** `[C: WER report and dump; cause L/C in proxy-design.md 21.5]`: the
  stock game of release-basic-m43 ended 0xC0000005 at its EXIT and Yes (pid 47984, 19:19:23; the stage recorded it as
  `earlier_exit_crash` on the next row and went on, as 2604's rule says). WER: fault module
  `CvGameCore_Expansion2_x86.dll_unloaded`, timestamp 6AFF7245 = the m43 `-ia` test DLL's PE time stamp. This is the
  known exit crash of every stock `-ia` game (proxy-design.md 21.5, tickets 0483/0484: after `UI.ExitGame` the EXE
  unloads the DLL and its main thread's `operator new` faults in the unloaded DLL's `ThunkOperatorNew` thunk); test
  builds only, not the shipped DLL. 0574 saw the same on the 22-civ journeys (#7091). Evidence:
  `<c5h-c>/run/crash/20261119_191923_pid47984/` (dump 138 MB, WER report, instruments.log).
- Report: `F:/c5-64bit-work/0580/rel-m43/hi_journey/report.md` and `.json`; a copy of report.md in
  `tools/release/evidence/0580/hi_journey-report.md`. Checkpoint pictures: Visual Checks feed, issue 0580 (DIAGNOSTIC).

**Not verified** `[I]`: the m43 journeys on the installer-installed layout (C5_RELEASE_HI_INSTALLED) and on rc1-m43
(0574 runs them there); a second replay series (one run per route here); whether the replays' combat gave the same
result (the checkpoint `attacked` expects only the turn, as release-large's does); the m43 journeys at another client
size. (2026-11-21: the installer-installed layout on build 26577625 was played by ticket 0643, section 8.7.)

### 8.7 The m43 journeys on EXE build 26577625, installer-laid layout (ticket 0643, 2026-11-21) `[L]` runs, `[C]` code and records

Ticket 0643 (impl-0643, account B), a run-and-report: no tool, journey or product file changed. Until this run the m43
journeys had passed only on EXE build 4390913 (8.6, c5h-c, a stage-laid layout).

**The layout.**
- Instance: c5h-new-b (Goldberg, F:, Steam build 26577625, mods form `cp,vp,eui`).
- Installer: the rehearsal's TEST installer `VP64-Patch-0.1b-rehearsal-for-VP5.4.8-TEST.exe` (ticket 0626,
  release-builds.md 7.4 (b)). 17,541,760 B, SHA-256 `42497629f440799fcd0fc34a5271752239041208f493a340d987b107afee9a3d`,
  variants std+m43. Run from a copy in `C:/c5-64bit-work/0643/installer-test`.
- **How the 43-civ variant is chosen** `[C: patch64.iss 4.3.3]`: by the SHA-256 of the installed VP DLL. The player's
  state was therefore set first with `provision.py set-dll` to the tag's `(3b) 43 Civs` DLL `01451bd7...` (the copy
  in 0626's m43 take, `inputs/player-x86`). The installer log then reads:
  - `Game EXE: CivilizationV_DX11.exe is Steam build 26577625`;
  - `Vox Populi 5.4.8: the 43-civilization variant of the patch fits it`;
  - `install 0.1b-rehearsal (m43, i3, mods form)`.
  Install took 32 s (exit 0). The DLC tree was identical afterwards; My Games differed by the 27 installed rows only.
- What was installed: the 43-civ take's bytes. Proxy `3a75b018...`, x64 DLL `19de0a29...`
  (`Release-5.4.8-80-g1b87dd185+m43`, x64-0.1b `1b87dd185`), server `f946bd10...`. The pack manifest has
  `major_civs` 43. The options are the rehearsal's set (release-builds.md 7.1).
- How the stage was run: `take_release.py Release-5.4.8 --tier validated --only hi_journey` with
  `C5_RELEASE_HI_INSTALLED=c5h-new-b` and `C5_RELEASE_HI_ROUTES=product` (MC #9064). Its work folder held a copy of
  0626's m43 state (`a1cccb6f...`). The build outputs' `major_civs` 43 make the stage play only the m43 journeys. The
  installed layout was checked against the m43 hashes: found = build.
- Driver: `tools/release/evidence/0643/s20_m43_run.sh` (phases check, install, hi, remove).
- Sound: no checkpoint of either journey reads sound (they read state fields, screens and pictures), so the test
  instances' Effects volume 0 (MC #9288) does not matter here.

**F: was stalling during the runs** (diskwatch's daily records, `tools/diskwatch/`, F: = the USB SSD that holds the
instances). Run 1 (13:07-13:49): 504 samples, F: idle median 2 % and below 10 % in 293 of them, read latency median
75 ms (p95 789, max 1,078), write latency median 246 ms (p95 725). The replay took 1,848 s against 614 s in 8.6, step
for step slower. Pass and fail are what this run judges, not time (MC #9338).

**Results** (c5h-new-b, product route, one game at a time; stage reports in `tools/release/evidence/0643/`, run folders
`F:/c5-64bit-work/0643/work-installed-c5h-new-b-runN`):

| run (EST) | journey | exit | steps | checkpoints | replay s | result |
|---|---|---|---|---|---|---|
| 1 (13:11-13:42) | release-basic-m43 | 1 | 62 of 63 | 6 of 6 ok (founded, research, turn5, turn10, loaded, menu) | 1,846 | all checkpoints passed; the tool died at the exit step (the replayer fault below, ticket 0648); counted as 6 of 6 with the tool fault named (MC #9365) |
| 1 (13:48) | release-large-m43 | 2 | 0 | - | 1 | not played: the session the basic replay left open refused it |
| 2 (13:52-14:02) | release-large-m43 | 8 | 4 of 20 | - | - | **the load ended by the patch** (D118 popup at step 5, the 60 s load waits): the decal-tap fault below |
| 3 (14:07-14:16, rerun 1) | release-large-m43 | 8 | 4 of 20 | - | - | the same, the same record |
| 4 (14:23-14:58, rerun 1 of basic, main with 0648 merged) | release-basic-m43 | 0 | 63 of 63 | 6 of 6 ok | 2,032 | **passed**; the game ended 6.7 s after Yes (the normal path, no `window_gone_at_end`) |
| 5 (18:12-18:20, after 0649; c5h-new-b on C:; composite from main 918a5837a; ServerFarmFields = 1) | release-large-m43 | 0 | 20 of 20 | 5 of 5 ok (moved, attacked, city, camera, menu) | 454.5 | **passed**; the decal set installed (below) |

**Run 5: release-large-m43 after 0649** `[L, 2026-11-21 18:02-18:23 EST]`. The MC returned 0643 for this run (#9588); it
was made on branch `task-0643-m43-large-rerun`.
- Instance: c5h-new-b, moved to C: by 0646, Steam build 26577625, mods form.
- Layout: the same TEST installer (`42497629...`, install exit 0 in 8 s, "the 43-civilization variant of the patch fits
  it"), with these files:
  - proxy `3a75b018...`, x64 DLL `19de0a29...`, server `f946bd10...`, as before;
  - the installed `shell_composite.dll` (`db4bcd44...`) replaced by a build of main 918a5837a (0649 merged): `shell/build.cmd`,
    16:31, `ed9e3460984b353cadfc152d0d85a29911502b289099a9cdb90031d62f2c8760`. The `composite` line of
    `shell_composite.sha256` was pointed at it (0649's recipe, terrain-decals.md 21.5; driver phases `swap` and `unswap`).
- **Option set:** the installed rehearsal set plus `ServerFarmFields = 1` (MC #9652 after D230: the first beta's set
  now carries it). The full set:
  - `[Renderer]`: ServerRenderer 1, ServerUnits 1, ServerCities 1, ServerLandmarks 1, ServerArc 0, ServerPath 0,
    ServerFarmFields 1;
  - `[Performance]`: AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1, HiddenEmbarks 1,
    GameEventsOneCrossing 0.
  The server log reads `ServerFarmFields = 1 (from the file)`.
- **The decal set installs.** The composite logs, for generations 1-4: `generation N leaves out 1 decal record(s)
  outside the world's x range 0 .. 5567 ...; the first: batch 2 slot 22, rectangle (5586.2, 1499.8, ...)`. The server
  logs `decals: generation 1 applied: 871 decals` 122.6 s into its log. There was no D118, no box and no crash.
- LOUD lines: vpproxy 0. Server and render 2 each: `units: UnitRefused ... BadInput: no pose program` for the journey's
  swordsman (18200) and the barbarian warrior (18202). So the server did not draw those two units. 0649's run had the
  same two lines. They are not decals and do not change the journey's checkpoints. MC-B's reading (2026-11-21 18:2x):
  known, ticket 4739 (units made by Lua/InitUnit refused by the server's create), fixed on main 8e5e43146 after this
  installer's server (`f946bd10...`) was built `[I]`; not verified on this layout.
- Restore by hash: the composite and the manifest were put back (`db4bcd44...`, `747dfb01...`) and VP64Options.ini went
  back to `72f99a0b...` before the removal. Removal exit 0. The DLC tree was identical and My Games identical outside
  the run outputs; the instance's DLL `ff0f6e86...` was put back, `verify --quick` was ok, the front end was
  `original`, and there was no box. Released 18:23:27.
- **Starting the stage without 0626's folder.** 0626's work folder was removed at 17:20, and with it the m43 state's
  fork clone and its descriptors. The stage refused twice at 0 s (`--fork ... no such folder`; `the candidate
  descriptor testdata wrote is missing`). `tools/release/evidence/0643/seed_state.py` makes a work copy of the state
  (`a1cccb6f...`), as 0580's seed43 did:
  - the config's descriptor key dropped;
  - `fork` set to `D:/c5-64bit-work/9045/fork` (only checked to be a clone);
  - the testdata outputs pointed read only at rc0b-m43's (`F:/c5-64bit-work/0574/rc0b-m43/run/testdata`, the same tag
    and count).
  The build outputs, which carry the product hashes the installed layout is checked against, are untouched.
- Run on C: the replay took 454.5 s, against 412 s in 8.6 on F: and on build 4390913.

- The stage's D162 line on run 4 reads "verdict passed (stage ok)" and then a refusal by D162, because release-basic-m43's
  stock route and release-large-m43 were not played. That is the expected line for a product-only run of one journey.
- release-large-m43 was not run a third time: two runs failed the same way on the same record (`batch 2 slot 22`), and
  the cause is known.
- No crash, no dump and no C runtime or WER box in any run. The only boxes were the two D118 popups. Each was closed
  by the claim's owner after its report folder was read (`cli.py dismiss --button Close --patch-popup`, 14:04:58 and
  about 14:16). Each popup switches the patch off (`vpproxy.ini` gains `failed=1`, and config.ini goes back to the
  player's own). The installed helper's `vp64helper.exe on` (what "Try again next start" does) switched it on again:
  vpproxy.ini and config.ini were back at the install-time hashes `f80d7590...` and `4a40b8bb...`.
- **The stage does not restore after a box** `[L]`: runs 2 and 3 have no restore record, so the journey's
  GraphicsSettingsDX11.ini and UserSettings.ini stayed on the instance. Run 4 then took those as its "before" state.
  They were put back by hand from run 2's kept backups, by SHA-256 (`f1d9cf01...`, `85f9d806...`). After the removal:
  - the DLC tree was identical;
  - My Games was identical outside the run outputs (apart from the instance's own DLL `ff0f6e86...`, put back by
    `set-dll --previous`);
  - `verify --quick` was ok and the front end was `original`.
  An operator of an installed-layout run checks the two ini files after any boxed run. (Corrected 2026-11-23, ticket
  0650: the stage now keeps an undo record and `hi_journey.py finish` does this restore, and a start on an instance
  left so is refused; section 8.10. Replaced: "the owner restores the two files by hand from the kept backups".)
- Disk load: F: stalled in every run. Run 2: idle median 0 %, read latency median 285 ms. Run 3: idle 0 %, 163 ms.
  Run 4: idle 0 %, 387 ms (p95 920). Run 4 also ran inside 14:40-14:57, when another team wrote build trees to F:
  (MC-B 14:59). No step failed on a wait or a deadline. The replays took about three times as long as 8.6's (2,032 s
  against 614 s for basic).

**The decal-tap fault: release-large-m43's load is ended by the patch** `[L: runs 2 and 3; C: shell/composite/decal_tap.cpp
batch check, read 2026-11-21]`.
- The server stops with `decals cannot install: no decal set within 20000 ms after the load (... state FAILED LAYOUT,
  generation 0; the reader: 0 refused ...; the slow-load bound 120000 ms, ticket 0491)`. Then come server-death
  PeerDied, the D118 popup, and the game ended.
- The composite's log, at the tap's first read:
  - run 2: `decal_tap: LOUD: FAILED LAYOUT: batch 2 slot 22: rectangle (5578.25, 1505.4, 5567, 1564.42) or priority
    0.7 not valid`;
  - run 3: the same slot with `(5586.6, 1499.88, 5567, 1568.62)`.
- The tap's batch check refuses any record whose min x is above its max x, and with it the whole set. With no first
  set, the server's install deadline ends the game.
- The record sits at the east edge of the 87-column map: 87 x 64 = 5,568 world units (64 per hex width,
  overlay-system.md) `[I]`. impl-0649 reports `[L, theirs]` that it is a road curve wholly past the east edge, and that
  the same record is there on build 4390913 (a stock census on c5h-c). On 4390913 the tap's first set is usually an
  early, partial one, which is why 0580's run passed.
- Layer: the product's decal tap, triggered by the save's map content. It is not the journey, the 43-civ variant, the
  EXE build or the installed layout. The same class was seen in play at T1100 by 0491 (fog-of-war.md, run b2), where it
  was not fatal because a good set was already kept.
- The fix is ticket 0649 (impl-0649, branch `task-0649-decal-seam-rect`: leave the record out). Until it lands, any
  save with a decal across the wrap seam can lose its load to D118 whenever the first set is complete.
- Line proposed to the MC for research/06 known-crash-causes.md (not edited here; tickets 0637 and 0641 hold it):
  "Decal tap refuses a wrap-seam decal rectangle at the first set: the load ends in D118 (decals cannot install) on
  maps with a decal across the seam (0643, build 26577625; fix 0649)".
- Evidence is kept in `F:/c5-64bit-work/0643/evidence/evidence-decal/` (both runs' composite, server, vpproxy and render
  logs, the D118 report folders, a copy of the save), with the install, removal and tree listings beside it. 0649 has
  its own copies.
- The release-large-m43 run after 0649 was a follow-up under 0643's number, on c5h-new-b's new C: copy (MC #9435). It
  passed (run 5 above, 2026-11-21 18:20).

**A replayer fault found: the last step's picture races the game's exit** `[L: run 1; C: tools/hi/replay.py
step_picture, capture.shot, actions.window_of, read 2026-11-21]`.
- release-basic-m43 ends with the exit box's Yes (step 63: no wait, a step picture). Run 1 played steps 1-62 with all
  6 checkpoints ok. Then the replay died: `{"error": "instance c5h-new-b (pid 12976) has no window yet", "exit": 1}`
  at 13:42:24, with no summary line. The game itself ended in order: vpproxy `detach: after the orderly shutdown` at
  13:42:25, no box, no crash folder.
- The cause: `step_picture` catches `capture.NoPicture` and `actions.Refused`. It does not catch
  `actions.ActionError`, which `window_of` raises when the window has already gone after Yes. So the last step's
  picture races the exit. 0580's own recording has no file for step 63, and its acceptance run passed because the
  window still stood.
- The uncaught error also leaves the HI session active. The next journey of the same stage run is then refused at
  once (`a session is active on c5h-new-b ... end it first`, exit 2): release-large-m43 was not played in run 1. The
  stale session was ended with `hi.py -i c5h-new-b --by impl-0643 session end`.
- Layer: the harness (`tools/hi`). It is not the journey, the 43-civ variant, the EXE build or the installed layout.
  The 22-civ release-basic ends the same way, so the candidate's own stage can meet it too.
- Fix: ticket 0648 (MC queue #9365; the acceptance is 0643's proposal): `step_picture` treats an `ActionError` after
  a step where the game may end as a missing picture, and a replay ends its session on any error. MC #9365 counts run
  1's basic journey as 6 of 6 checkpoints with the tool fault at the exit step named, not as a journey failure.

**The stock route (stock-43) on this layout: not playable** `[C: stages/hi_journey.py installed_layout and _lay_installed;
L: 0626's installed stock route, release-builds.md 7.6]`. The ticket asked whether it fits in 30 minutes. It does not:
- On the installed layout the stock route is vpproxy.ini `mode=off`, which loads the installed fallback: VP's own 43-civ
  DLL (`01451bd7...`). That DLL has no Lua channel, so the journeys' judges cannot read the game (0626: exit 5 at the
  first `in_game` judge). The `mode=off` flip also leaves config.ini in the D33 form, which gives the red map.
- stock-43 as 0580 played it needs the m43 instance-aware DLL laid by the stage on its own layout: the 0626 m43 take
  built it, `i0iam43` `BA520010...`. That is the candidate take's own hi_journey stage (MC #9064), not an installed
  layout. So step 3 was not run.

**Not verified** `[I]`: both m43 journeys in ONE stage run with the fixed composite (basic passed in run 4 before 0649,
large in run 5 after it); a second basic replay
with the window gone at Yes (the 0648 path was not met live, run 4 exited the normal way); the m43 journeys on the
candidate's stage-laid layout on build 26577625 (both routes).

### 8.8 The game's own end at the last step (ticket 0648, 2026-11-21) `[C]`

A journey that ends by the exit box's Yes can lose the game's window before the replay takes that step's picture
(0643's run 1 of release-basic-m43 on build 26577625, step 63). Since 0648, `tools/hi/replay.py` treats a window
that is gone at the trace's last step as a missing picture (a `missing_pictures` row with `window_gone` true, the
summary's `window_gone_at_end`, a stderr line and the instance log's `hi_replay_window_gone`) and passes; at any
other step, the last step of a shorter `--until` range included, it is exit 1. Every replay stop, an unexpected
harness error included, writes `replay.json` and `replay-stop.json` with the step and the reason and ends the HI
session, so the next journey on the instance is not refused with 'a session is active'. Before 0648 an
`ActionError` from the step picture ended `hi.py` with exit 1, no summary and the session left active.

Limits: a replay killed from outside (a stage timeout, a stopped task) or a session opened by hand still leaves
its session active; it is ended with the tool's own `session end` by its owner. A session whose owner no longer
exists has no form of its own yet (c5h-b's session of 2026-11-19 was ended on 2026-11-21 with the opener's
address on the MC's word). Not live-checked as a gone window: 0643's run 4 ended by the normal path (the game
closed 6.7 s after Yes).

### 8.9 A key without effect: one counted retry (ticket 0706, MC #11436, 2026-11-23) `[C]`

The flake: ticket 5024's take 7 (2026-11-23 08:07-08:17, stage oldexe, c5h-a, EXE 4390913, installer-laid layout,
product route, the workstation loaded by the take's own stages) stopped release-basic at recording step 28 with exit
5: End Turn's wait `state.turn>=4` reached its deadline. Recording step 27 (replay step 25: End Turn click, 800 ms,
SPACE) had been posted and reported delivered (WM_KEYDOWN, 60 ms, WM_KEYUP), but the judge after it still read
`ENDTURN_BLOCKING_UNITS` with the Pathfinder (1003) selected at (45,30) where the recording read
`NO_ENDTURN_BLOCKING_TYPE`. The other 8 SPACEs of turns 0-3 took effect; step timing matched (7.5 s against 7.1 s).
Cause `[I]`: the game lost one posted key under load; not a product fault shown (the same journey passed on c5h-b,
EXE 4390913, on 2026-11-22, validation-tiers-m2.md 8.3). Evidence, not in git: `F:/c5-64bit-work/0630/tierI-meas7/`
(oldexe-take7-report.md, oldexe-take7-run/: trace.jsonl, replay-stop.json, stop_0026.png).

What `tools/hi/replay.py` does since 0706:

- **Which keys.** A step whose last action other than a wait is a `key` delivered in the recording, and whose
  recorded judge changed over the step (the previous step's judge against this step's) in one of `turn, block,
  unit, unit_x, unit_y, mode, popup, city_screen, screens` (`EFFECT_FIELDS`). The changed fields with their
  recorded after-values are the expected effect. Offline count over the committed journeys (2026-11-23, scratch
  `C:/c5-64bit-work/0706/k0706.py`): release-basic 24 of 67 keys (22 SPACE, B at step 10, ESC at step 51),
  release-basic-m43 22 of 67 (B at 10, ESC at 49), release-large and release-large-m43 0 of 1, focus-window 0.
- **The check.** The judge is read (`Resolver.state`, the same read a checkpoint uses) just before the key; after
  the step's own wait the judge is read up to 10 times 0.5 s apart (`EFFECT_POLLS`, `EFFECT_POLL_S`). The effect is
  there when one expected field differs from the reading before the key, or every expected field already reads as
  recorded (then nothing is pressed again, whatever did it). B and ESC have judge-condition step waits
  (`screen_open CityView`, `screen_closed TechAwardPopup`): their wait stops the replay first if the key was lost,
  as before, and after a passed wait the check finds the effect, so they are never pressed twice. The retry matters
  for the SPACEs, whose step wait is 1000 ms.
- **The retry.** No effect: the key is pressed once more after a box check and the deny check (one retry per step,
  never more), and the judge is polled again the same way. Every retry is recorded, never silent: the replay
  trace's step record gets `"retries": [{step, replay_step, action, key, retry: 1, why, expected, before,
  after_first, delivered, after_retry, outcome}]`, the summary row of the step `"retries": 1`, the summary's
  `"key_retries"` list (in `replay.json` and in the stage's `<journey>-<route>.result.json`, which holds the whole
  summary), one stderr line `replay: step N: key SPACE pressed once more (...)` and the instance log's
  `hi_replay_key_retry`. The stage-level record is `key_retries` in that result.json; a count in the stage row of
  `tools/release/stages/hi_journey.py` waits for ticket 0634 (MC #11671).
- **A second miss** gives outcome "no effect after the retry either ..." and the replay goes on: the next wait that
  needs the effect (End Turn's `state.turn>=N`) stops it with exit 5 as before. A box at the retry stops it with
  exit 8 and the retry's record (outcome "refused"). A judge that cannot answer (unavailable before the key or on
  every poll) presses nothing again.
- **Cost.** One judge read before each checked key and at least one after; with the effect present the first poll
  finds it (no sleep). A missed key costs up to 2 x 4.5 s of polling plus the retry.

Hermetic proof (2026-11-23, `tools/hi/tests/test_replay_key_retry.py`, 8 tests, `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
python -I -m pytest -q tools/hi/tests/test_replay_key_retry.py`): a fake game that plays release-basic's turn loop
from the posted messages (SPACE skips the selected unit, End Turn with no unit left ends the turn, the k-th SPACE
dropped on request). Dropping the turn's second SPACE once: one retry at that step, outcome "effect after the
retry", the replay passes all turns (9 SPACEs posted for 8 recorded). Two drops in two steps: two retries, one each.
Negative control, every SPACE dropped: one retry at each of the two SPACE steps, both "no effect after the retry
either", and the replay still stops at the End Turn wait (`state.turn>=1`) with exit 5. No judge answer: no retry. A
box at the retry: exit 8 with the record. The HI replay suites around it pass unchanged (`tools/hi/tests/`,
`test_hi_trace.py`: 89; `test_hi_sweep_trace`, `test_hi_realinput`, `test_hi_phase`, `test_hi_checks_report`: 331;
`tools/release/tests/test_hi_journey.py` and `test_tier_hi_journey.py`: 118 passed in 11 min 35 s;
`test_safety_static.py`: 55 passed).

Live `[L]` (2026-11-23 13:44:53-13:57:37, MC-B grant #11705): release-basic replays unchanged with this change on
c5h-new-b (EXE 26577625, mods548), product route on the stage-laid layout (`C5_RELEASE_HI_ROUTES=product
C5_RELEASE_HI_JOURNEYS=release-basic`, `take_release.py Release-5.4.8 --tier validated --only hi_journey` in a copy
of cand2/tier3's take, branch at c017143a6): 63 of 63 steps, 6 of 6 checkpoints, 552.8 s (the 2026-11-22 c5h-b
pass: 558 s), `key_retries` none (no key was pressed twice; no "pressed once more" line in the replay's log), no missing
picture; the stage's restore and release exit 0. take_release ended with exit 10 only because D162 counts the stock
route and release-large as not played. Not shown: that a second press is what a lost key needs on a loaded machine
(the take-7 stop was not reproduced live; the hermetic test models it).

### 8.10 Finishing the restore after a box (ticket 0650, 2026-11-23) `[C]`

The fault (8.7, `[L]` 2026-11-21): a journey that ends on a box (a crash box or the patch's D118 popup) leaves the game
running, the claim kept and the instance laid, by design (D29, D155: never pressed). The stage's restore never ran
afterwards, and nothing could run it: the undo list was a list of Python closures that ended with the take's process.
So the journey's GraphicsSettingsDX11.ini and UserSettings.ini stayed on the instance, and `provision.py verify
--quick` did not see it (both are mutable files there: a change is listed under `changed`, not an error). The next run
took them as its "before" state.

The fix, in `tools/release/stages/hi_journey.py`:

- **The undo record.** From the claim on, every undo entry is also written as data to `<work>/hi_journey/undo.json`
  (format 1). The head names the instance, its root and folder, the claim's owner, ticket and address, the tag, the
  work folder, whether the layout is installer-installed (with its layout hashes), and `state`: `open`, `left` (with
  the reason), or `restored`. Each entry has a `kind`:
  - `put_back`: a file and its kept copy with its SHA-256, or `kept: null` when the file did not exist before;
  - `rm`: a file;
  - `provision`: a provision.py command, here set-proxy --original;
  - `set_dll`: the DLL of before the run;
  - `frontend_restore`;
  - `clear_made`: the made saves' prefixes.

  Each entry also has `done` and `result`. One function, `undo_fn`, builds the callable from an entry, so the stage
  and the finish run the same code. The entries that `fix_modinfo` (the instance stage) appends as closures are read
  from the closure's variables (`spec_of`). An entry it cannot describe would be `opaque`, which a finish reports as
  left changed.
- **The instance's marker** `<instance>\run\hi_journey_unrestored.json` names the record. It is written at the claim
  and removed only by a restore that ends ok. A start that finds it, with a record that is not `restored`, fails
  loudly (`LOUD: ... never restored ... Finish it first: python -I tools/release/stages/hi_journey.py finish "<record>"`).
  That start lays nothing, releases its own claim, and lists the marker under `unrestored` in its outputs. A marker
  whose record says `restored` is stale and is removed. A run from before 0650 left no marker and is not detected.
- **The finish form.** `python -I tools/release/stages/hi_journey.py finish <take work folder | its hi_journey folder
  | undo.json> [--tools FILE]` runs after the claim's owner has closed the box. It holds the take's driver lock (it
  refuses while a take runs in that folder) and checks, without changing anything:
  - no game (`cli.py status`);
  - no box: the watcher's marker; a crash without a box and with no game is not one, as for 0533;
  - no box and no patch popup: `dialogwatch.py once`, which also sees the D118 popup after the game has ended;
  - no processes left (`cli.py leftovers`).

  If any of these is there, it refuses with exit 4 and `REFUSED, nothing changed`. Otherwise it takes the claim back:
  it renews it for the recorded owner, or claims the instance for that owner when it is free. Another party's claim is
  exit 4. Then it runs every entry not yet done, in reverse order. It checks the front end and runs `provision.py verify
  --quick`; on an installed layout it re-checks the layout's hashes instead. It writes `<work>/hi_journey/restore.json`
  (`by: finish (ticket 0650)`) and a section in report.md, removes the marker, and releases the claim (exit 0). A failed
  entry is exit 1: the claim and the marker stay, the entries that are done are recorded, and a later finish runs only
  what is left. A record already restored is exit 0 with nothing done. The stage's own restore writes `restore.json`
  too (`by: the stage`).
- **set-dll back by hash** (the scope addition of 2026-11-21 16:26, lead-0630's dry run 1). On main at 87a5e2efd the
  stage already restored from its kept copy (`set-dll --dll <kept>`), not `--original`. But it never checked the copy,
  and the copy had a fixed name (`kept/CvGameCore_Expansion2.dll`), so a later run in the same work folder would
  overwrite it. Now:
  - the copy lives under its own hash (`kept/dll-<sha16>/`) and is checked when it is made;
  - before set-dll, the copy must hash as the DLL of before the run (else `REFUSED: the kept DLL ...`, nothing set);
  - after set-dll, the instance's DLL must hash so too (else `LOUD`).

  Either failure is a failed entry, and the claim is kept. The tier stage's own copy (`tools/release/tiers/stages/`)
  is epic 0630's, as the ticket says.

Tests (`tools/release/tests/test_hi_journey.py`, class `FinishAfterBox`, 6, hermetic, the fake tool extended with
`dialogwatch once`, a patch popup, and another party's claim):

1. A left-running run finished: the files are back by hash, `restore.json` is written, the marker is gone, the
   record is `restored`, and a second finish changes nothing.
2. The finish is refused, with nothing changed, for each of: a game, a box, the popup, processes left, another
   party's claim.
3. A start on an instance with unrestored journey settings is refused: nothing laid, no launch, the claim released.
   After a finish, the next start's `settings_before` hashes are the instance's own.
4. A kept DLL that does not hash is refused and the claim kept. After the copy is put right, the second finish runs
   only set-dll and verify.
5. A normal run leaves a restored record and no marker.
6. A finish without a record is exit 2.

The six passed in 95 s, through quietlock, on 2026-11-23 (started about 16:08; the issue log's 16:09 line was
written while they ran). Results of 2026-11-23:

- `test_hi_journey.py` with `test_tier_hi_journey.py`: 134 passed in 2,208 s (16:28-17:05).
- The release-tiers suite (`python -I tools/suites/run_suites.py --suites release-tiers`, through quietlock, branch
  merged with main at 19:57): 870 passed, 0 failed, 0 skipped in 5,720 s (19:58-21:35). With it, the runner's
  always-run `safety-static`: 55 passed.

The workstation carried about ten other heavy jobs throughout. An earlier run of the release-tiers suite was stopped
after 75 min, at 18:21:59, for a quiet hour, with no result.

Negative control, 0643's sequence offline (2026-11-23, ended 16:26:52 for main's stage and 16:27:00 for the branch's, `[C]` hermetic). The fixture is the one above; the
script stays in scratch and was not committed. Main's stage (87a5e2efd) was laid as the runbook's offline fix check
lays it, in `C:/c5-64bit-work/0650/old/tools/...` with the harness's `dialogwatch.py` beside it, because the stage
finds its repository from its own path. The sequence:

1. Run 1 meets a box at the first replay.
2. The owner closes the box and ends the game by hand, without a restore.
3. Run 2 starts on the same instance.

After run 1, the instance carries the journey's GraphicsSettingsDX11.ini `ede1ab7e...` and UserSettings.ini
`09d1058a...`; its own are `3c055a59...` and `9f02f38d...`.

- **Main's stage:** run 2 is `ok`, with `settings_before` = the journey's hashes, and its restore leaves them on the
  instance. The fault is reproduced: the instance's own files are lost for good.
- **This branch's stage:** run 2 fails with `LOUD: c5h-t still carries an earlier hi_journey run that was never
  restored ...`. It lays nothing, and the journey's settings wait for the finish.

Not verified live: no box was provoked on a real instance for this ticket. The finish's tools are the real
`dialogwatch.py once`, `pool.py renew` and `claim`, and `cli.py leftovers`, whose behaviour is read from their source
(`dialogwatch.py` main, `pool.py` claim, renew and release).

### 8.11 The long start saves made again on VP 5.5 (ticket 0771, 2026-11-25) `[L]`

Why: upstream 0848d52cc (#13643, in Release-5.5) writes a new field in `CvPlayer::Serialize` without a save-version
guard, so no 5.4.8 save loads on any 5.5 build (impl-0766, `[C]` from the source, thread 0763 #15156). Every start
save the tiers and journeys read was made again on 5.5. This section is the long ones (impl-0771, account B); the
short ones (Tiny T8 twin, HI basic T0 std and m43, modpack T19) are ticket 0766's. Record of every file, hash, recipe,
wall time and load check: `tools/hi/evidence/0771/saves.tsv`; drivers in `tools/hi/evidence/0771/scripts/`, specs in
`.../specs/`. The saves themselves are at `F:/c5-64bit-work/0771/saves/` and never committed (D83).

| New save (VP 5.5) | SHA-256 | Bytes | Replaces |
|---|---|---|---|
| `VP55-HUGE_0350-B` (the huge reference, below) | `87fbdfec...6f8af1a8` | 1,977,604 | `VP8P-HUGE_0350` and its copies (c499d3c6) |
| `VP55-HUGE_0350` (REJECTED: ends at 394, below) | `b3338aa7...a2efe2d` | 2,125,826 | `VP8P-HUGE_0350` = `Release-5.4.8-HUGE_0350` = `TB-HUGE_0350` = `HI-KIT-MODERN-HUGE-T350` (c499d3c6) |
| `HI-0771-LC21B55-T200` | `3a660ea8...6539825` | 1,751,160 | `9071-LC21B-T200` = `HI-KIT-MEDIEVAL-LARGE-T200` = `HI-4508-LARGE-T200` (e3da1d17) |
| `HI-4506-LARGE-SCENE-55` | `c5ad17b4...aeba0e344` | 1,752,484 | `HI-4506-LARGE-SCENE` (c01bb2bf) |
| `HI-0771-L30M43-55-T200` | `a942b7e5...cdc2c4b8294` | 2,134,639 | `HI-0580-L30M43-T200` (43eb3f95) |
| `HI-0580-LARGE-M43-SCENE-55` | `bf7ab4bd...63d0c424d` | 2,136,063 | `HI-0580-LARGE-M43-SCENE` (96b54368) |

**The recipes** (what the old saves were, read from their headers with `c5h save --json`, the 9071/0580 specs and the
DLL project's memory log; the new games add only player 0's civilisation, so that the active player is the old one):
- Huge: `Assets\Maps\Continents.lua`, Huge (128 x 80), Standard, Ancient, Prince, 8 majors, 24 city states (the size's
  default), quick combat on, player 0 Egypt; the old one was the owner's own autoplay of 2026-09-23 (GameId 118) with a
  random seed, the new one has seeds 7710/7711. At turn 350 both are 1930 AD with Egypt in the Modern era.
- Large 21: 9071's `spec-lc21.json` unchanged (Communitu_79a, Large 87 x 59, 21 majors, 20 city states, Prince, seeds
  9071/9072) + Netherlands in slot 0. Large 30 (43-civ build): 0580's `spec-l30m43.json` unchanged (30 majors, 20 city
  states, seeds 580/581) + Ottomans in slot 0, resolved for 43 slots (`scripts/resolve43.py`, as `run_d10.py`
  `x86_spec_for` does: c5h's resolver knows 22 slots).
- Scenes: 4506's set-up Lua unchanged (`scripts/large-scene.setup.lua`, 514f3f8a, the same file 0580 used) on the
  T200 save loaded in a 5.5 client with the -ia DLL, then `UI.SaveGame`.

**How the T200s and the T350 were played** `[L]`: headless in civ5-headless (`c5h run`, the 32-bit host) with the
Release-5.5 tag's own 32-bit DLLs from impl-0766's verified tree (22: `fd2fc01f...`, 43: `b65231d5...`; lead-0763's OK
#15278: the -ia DLLs were not linked yet, and the instruments add no saved field), driver `scripts/c5h_run.sh`
(quietlock, `--low-priority`, `--no-addins --no-profiles`, `--mod` CP v152 + VP v17). The old LC21B/L30M43 saves were
headless too (the x64 server, which has no 5.5 build yet). Wall times on the busy workstation, below-normal priority:
Large 21 27.5 min (8.3 s a turn), Large 30 37 min (11.2 s), Huge 74 min (12.6 s; peak working set 352 MB).
- **The database**: `c5h dbcache --snapshot cache/db/snapshot/4DF730138BA1F0EE/snapshot.db --mods-dir <5.5 tree>/user/MODS
  --mod <CP>:152 --mod <VP>:17 --mod <EUI>:1 --dll <tag DLL>` (12 s), copied to `F:/c5-64bit-work/0771/db/
  vp55-cpvpeui-c5hbuilt.db` (1f487697...). It has the client form's counts: 45 civilisations (43 playable), 44 leaders,
  120 minor civilisations, 832 tables, the same as the 5.4.8 client database 9071 used (`lc-db`), so metrics-ledger
  8.2's assert (a civilisation the client lacks) cannot arise. Not compared row by row with a 5.5 client's own database
  (c5h-55-a's cache held only the base game's at 17:05).
- **TRAP: the game folder writes the header's DLC list.** The host lists every keyed package under `<game>\Assets\DLC`
  (civ5-headless `HHSaveGame.cpp` `SaveDlcList`). An instance's install has Vibrant Textures (`VT`, three keyed
  packages), so the first pass (game folder `F:/c5-instances/c5h-h/install`) wrote **16** DLC where every client-made
  and old save lists **13**; `run_d10.py`'s x86 arm copies the toolkit folder without the DLC a save lacks, so such a
  save does not match there. All three were played again on a copy of the toolkit game folder without
  `VP_MODPACK_5_4_8_NEUTRALS` and `VT` (113 MB, `shutil.copytree` as `run_d10.py` `GameDirs` does): 13 DLC. Same seeds,
  database and DLL gave **identical `final-state.json`** in both passes (sha256 equal for all three games): the
  headless game is deterministic, only the header differed.
- **TRAP: `cmd /c c5h.cmd` through Git Bash** ("The syntax of the command is incorrect"; the DLL path's parentheses and
  `/c` rewriting): call `python -m civ5headless` with `PYTHONPATH=<civ5-headless>/python` instead (the driver does).

**The load checks** `[L, 2026-11-25 17:40-18:20, workstation, hidden, watcher per launch]`: instance c5h-55-d (C:,
profile mods55, EXE 26577625; made for this by MC-B's order #15536 because F: was saturated, 0 % idle and about 1 s
per read from 17:08: a launch from F: and a headless load onto F: both stalled), the -ia DLLs set with
`provision.py set-dll` (22: `63e997c1...`, 43: `4ef94f1d...`), `scripts/load55.sh put|one|info|scene|save|stop`. Every
save loaded with the human seated (`autoplay 0`), the game on, the turn as named; every stop by `cli.py stop` rc 0, no
box. In game 248-314 s after launch for the Large saves. The 43-civ build reads `MAX_MAJOR_CIVS` 43 in game. Details per
save in `saves.tsv`. The client draws the world red on c5h-55-d's stock route (its config.ini carries the D33-style
`[DisableSystems]` of its template c5h-new-a): diagnostic only, not a property of the saves.

**350 -> 400: a huge reference must outlive turn 400** `[L]`: the D10 save test plays the huge save from 350 to 400.
The first `VP55-HUGE_0350` (seeds 7710/7711, all five victories as the old one) **ends at turn 394 in a diplomatic
victory** (player 6), headless on the tag DLL (`huge55-350to400-b`, 24 s a turn), so it is not the reference. Lead-0763's
rule (#15641): the first recipe-faithful seed whose game passes 400 is the reference; if none does, the same seeds with
only time and domination victories (as ticket 0175 did for REF-350), the deviation named in the descriptor's note.
Three games were played to 350 from 18:38 (`specs/huge55b-t350.json` seeds 7712/7713, `huge55c-t350.json` 7714/7715,
`huge55-nodiplo-t350.json` 7710/7711 with `victories` [TIME, DOMINATION]; driver `scripts/huge_chain_runs.sh`), each then
350 -> 400. **Result: all three passed 400 with no victory** (20:03-20:25, 23-25 s a turn); B was the first to end, so
**`VP55-HUGE_0350-B` (seeds 7712/7713, the old recipe with all five victories) is the huge reference**: SHA-256
`87fbdfecc920f89d8e7c6be8be6ee0b6ae3bb404c124e0c5f95d56fd6f8af1a8`, 1,977,604 B, turn 350 (1930 AD), Egypt Modern, 7 of 8
majors alive, 21 city states; loaded once on c5h-55-a (F:, -ia 63e997c1): in game 419 s after launch, turn 350, Egypt
human, autoplay 0. The other two (C 7714/7715, ND 7710/7711 time+domination) are kept as spares in
`F:/c5-64bit-work/0771/saves/candidates/` (hashes in `saves.tsv`). Lesson: a huge reference game is not proved by
reaching its turn; play it to the save test's last turn before adopting it (here 20 min).
**F: was the bottleneck** `[L, 2026-11-25 17:08-19:40]`: with the run folder on F: (0 % idle, about 1 s per read) a boot
stalled 15 min copying the 50 MB localization database and failed on c5h's 900 s stall timer (twice), and the turns ran
8.3-12.6 s each; with the run folder on C: the same huge game booted in under a minute and ran 0 -> 350 in 35-38 min
(6.0-6.5 s a turn). Put c5h run folders on C: (a huge run folder is 10-70 MB) and pass `--stall-seconds 3600`.

Not verified: the saves on the x64 5.5 server (0764's builds did not exist); a row-by-row comparison of the c5h-built
database with a 5.5 client's; the journeys on these saves (ticket 0770 re-records them).

### 8.12 The journeys on VP 5.5 (ticket 0770, 2026-11-25) `[C]` code; recordings below as they land

Ticket 0770 (impl-0770, account A), from epic 0763 (D259, VP 5.5). **Why** `[C]`, impl-0766 from upstream's source
(#15156 on thread 0763): upstream 0848d52cc (#13643, in Release-5.5) adds `m_iNextObjectID` to `CvPlayer::Serialize`
with no save-version guard, so no 5.4.8 save loads on a 5.5 build; the four start saves were made again on 5.5 by
ticket 0766 (research [headless-and-parallel-testing.md](headless-and-parallel-testing.md) 6.5 on its branch), and the
journeys' traces (clicks on the old maps' plots and units) are recorded again here.

**How the stage picks the set (code, 2026-11-25)** `[C]`: `tools/release/stages/hi_journey.py` (`load_index(path, vp)`,
`take_vp(ctx)`; the tier stage `tools/release/tiers/stages/hi_journey.py` calls the same):
- Each journey of `journeys.json` names the VP release its start save was made on: `vp` (absent = the index's
  `default_vp`, absent = `5.4.8`). A release's journeys keep the role names D162 asks for (`release-basic`,
  `release-large`, `release-basic-m43`, `release-large-m43`) with trace files of their own (`*-55.jsonl`), so
  `D162_BY_MAJOR` and `owner_check` are unchanged. `load_index(path, vp)` returns only that release's journeys (the
  entries are not changed: the tier's `entry_hash` of a 5.4.8 entry stays as committed); `vps` lists every release.
- The take's release: `C5_RELEASE_HI_VP` when set (recorded in the settings only when set), else the version in the
  take's tag (`Release-5.5` -> `5.5`), else in its descriptor's tag (a `--base-commit` take named otherwise), else the
  index's `default_vp`. A release with no journeys is "skipped" (not played; on a take for the owner, D162 refuses).
- An index that names no release at all (no `vp`, no `default_vp`: every index before 0770, the release tests'
  fixtures, a planted copy) is one set played on every release, as before.
- The 5.4.8 entries stay as they are until beta 5 ships (the lead's scope); the 5.5 start saves sit in the same
  `saves_dir` (`F:/c5-64bit-work/0392/4506/saves`) under their own names.
- Tests: `tools/hi/journeys/tests/test_vp_selection.py` (hermetic; the committed index per release: own saves, own
  traces, check-trace clean).

**The recording plan** `[I]`: the stock route of a 5.5 test instance (CONTRACT.md 2: the 5.5 tag's 32-bit `-ia` DLL
with the Lua channel, no proxy, no server; 0766's `vp55-ia` 63e997c1... for 22 civs, `vp55-ia-m43` for 43), mods
cp,vp,eui, 1920x1080, `SinglePlayerAutoEndTurnEnabled` 0 and `AutoUnitCycle` 1 as the stage lays them (backed up and
restored by SHA-256). The 5.4.8 journey's steps are issued one by one into an open recording session (0580's method,
8.6: scratch `drive.py` adapted, with the typed save name replaced), the steps whose target is on the map given by hand.
Scratch (not committed): `C:/c5-64bit-work/0770/` (`rec.py` prep/stockcfg/put/newest/up/made/stop/restore on the stock
route, `drive.py SRC FROM TO [--type OLD=NEW]`, `hi.sh`, `edit_header.py`, `scrub.py`).

**The start saves** (all copied into `saves_dir` `F:/c5-64bit-work/0392/4506/saves/`, never committed; the kept originals
are 0766's `C:/c5-64bit-work/keep/vp55-saves/` and 0771's on its branch `tools/hi/evidence/0771/saves.tsv`):

| Journey | Save | SHA-256 | Made by |
|---|---|---|---|
| release-basic | `HI-0766-BASIC55-T0` | 70c9bb15... | 0766 (#15490): 4506's recipe on 5.5 (Tiny, Continental Drift, Russia, Emperor, 4 majors; settler 20,17, warrior 21,18, pathfinder 21,16) |
| release-basic-m43 | `HI-0766-BASIC55-M43-T0` | 19bbfd71... | 0766 (#15613), the 43-civ -ia DLL |
| release-large | `HI-4506-LARGE-SCENE-55` | c5ad17b4... | 0771 (#15617): U1 131198 at 59,27 -> 60,28; U2 131199 at 57,25 attacks the barbarian at 58,26; U1 selected after the load |
| release-large-m43 | `HI-0580-LARGE-M43-SCENE-55` | bf7ab4bd... | 0771 (#15617), 43-civ DLL: U1 131255 at 9,45 -> 10,45; U2 131256 at 7,44 attacks the barbarian at 7,43; U2 selected after the load |

**A trap met first: the red world on today's 5.5 instances** `[L]`: every mods55 instance made on 2026-11-25 (c5h-55-a,
-c, -d) copied a `config.ini` carrying the 64-bit patch's `[DisableSystems]` keys (Decal, Forest, FOW, FluidFOW,
YieldIcons, DistanceFog = 1; impl-0766 #15580, impl-0771 #15565): on the stock route the world draws as a flat deep red
map (units, banners, yield icons and the interface drawn; no terrain, no hex grid). Game state is not affected (render
switches only). release-basic was recorded on c5h-55-c in that state (the warning reached the thread a minute before
the recording began and was read after it); its replays run with the six keys set to 0 (scratch `rec.py stockcfg`, the
file backed up by SHA-256, a7bdd289... -> 3f8363be..., restored at the end). The fix of the template is 0766's.

**release-basic on 5.5** (recorded 2026-11-25 17:56-18:56 EST on c5h-55-c, stock route, client 1914 x 1051;
`F:/c5-64bit-work/0770/rec-basic/trace.jsonl`, 64 steps; journey `tools/hi/journeys/release-basic-55.jsonl`):
- Every step of the 5.4.8 journey, unchanged, by `drive.py`, except one addition: after the load the settler is
  selected and centred (plot 20,17 at client (957, 526) = shot (640, 352), as on 5.4.8); B opened the city screen;
  Warrior at the same px; The Wheel at the same px of the tech tree (research id 9 again); each turn needed the same two
  SPACEs; **at the end of turn 7 the AI's first contact opened Washington's leader screen** (not on the 5.4.8 map): the
  end-turn step's `state.turn>=8` wait reached its 150 s deadline with the channel silent (the leader screen silences
  it), and one step was added by hand: a click on **Goodbye** (shot (1186, 659), a centre-anchored pixel hit) with the
  wait `state.turn>=8` (3.4 s). The pointer rests on DECLARE WAR's tooltip after the end-turn click (the button sits
  next to the end-turn button's px), so nothing may be clicked there blindly. Turn 8: the end-turn button opened the
  city screen for production (Warrior finished), Monument closed it; turns 8-9 three SPACEs; the turn-10 start opened
  the TechAwardPopup, ESC closed it. Save as `HI-0770-SAVED` (263,531 B), load it by the game menu, main menu, EXIT, Yes.
- Checkpoints (values read now): founded (turn 0), research (turn 0, research 9), turn5 (turn 5, research 9), turn10
  (turn 10, research -1, **gold 72**; 42 on 5.4.8), loaded (the same), menu.
- check-trace `--journey`: clean, 64 steps, 6 checkpoints, hits control 43, plot 1, pixel 13, 147 actions (147 intent
  records kept, as the m43 journeys keep theirs). Privacy: 10 judge error texts carried the instance's local path,
  replaced by a placeholder (scratch `scrub.py`, named in `edited`).
- The game's EXIT and Yes ended the process with 0xC0000005 and no box (watcher signature `exe-crash: 0xC0000005 ...
  (no proxy crash record)`, 18:55:22): the known exit crash of every stock `-ia` game (8.6; proxy-design.md 21.5, tickets
  0483/0484), here on the 5.5 `-ia` DLL. In a recording session (not a replay) the watcher's journey-exit rule (0540)
  does not apply, so it is marked as a crash.
- Timing on C: (F: was saturated): launch to activation 106 s; a front-end step 30-60 s; an in-game step 20-50 s; the
  whole recording 60 min with the drivers' pauses.

**Stock replays of release-basic (5.5)** `[L]` (`hi.py replay ... --route stock`, scratch `replay.sh`: prep, the six keys
0, the start save newest, the made save absent, launch, replay, restore by hash; c5h-55-c, 22-civ -ia 63e997c1, cp,vp,eui,
client 1914 x 1051):

| Run | Started (EST) | Exit | Steps | Checkpoints | Replay time | Output |
|---|---|---|---|---|---|---|
| 1 | 2026-11-25 20:05:33 | 0 | 64 of 64 | founded, research, turn5, turn10, loaded, menu: all ok | 580 s | `C:/c5-64bit-work/0770/rb-stock-1/` |
| 2 | 2026-11-25 20:20:01 | 0 | 64 of 64 | the same 6, all ok | 559 s | `C:/c5-64bit-work/0770/rb-stock-2/` |

- Step 38's recorded deadline wait (the leader screen) is replayed as a plain wait capped at 60 s
  (`recorded_deadlines`), then Goodbye's centre-anchored click closed the leader screen both times: the AI's first
  contact at the end of turn 7 is the same in every run so far (recording and 2 replays).
- Each replay's EXIT and Yes ended the game with the known `-ia` exit crash, which the watcher now names as such
  (`exe-crash ... at the game DLL's unload after UI.ExitGame (-ia MemoryImports; known, harmless: 0483)`, the journey's
  exit rule 0540).
- **The picture folder decides the speed** `[L]`: a first replay with its output on F: (saturated by other jobs,
  2026-11-25 19:01) took 45-100 s per step (16 steps in 13 min; stopped at the coordinator's word for the owner's
  window); with the output on C: the whole 64-step replay took 9.3-9.7 min. The recording (on F:) was slow for the same
  reason. Run journeys with their pictures on a disk that is not saturated.
- A stopped replay leaves its session open (`session.json` `step` 16); `hi.py session end` closes it.

**release-large on 5.5** (recorded 2026-11-25 20:10-20:25 EST on c5h-55-d, stock route, 22-civ -ia DLL set by
`provision.py set-dll` over VP 5.5's own fd2fc01f, config.ini six keys 0; `C:/c5-64bit-work/0770/rec-large/trace.jsonl`,
21 steps; journey `tools/hi/journeys/release-large-55.jsonl`):
- Steps 1-6 from release-large unchanged (SINGLE PLAYER, LOAD GAME, the top entry, Load Game, four 60 s waits, ENTER
  `in_game`); step 4's picture was a load-screen glitch (exit 3 recorded, the click and its wait ok). After the load U1
  (131198) is selected and centred, as on 5.4.8. Pixels read with `judge.py plot-pixel` before each map click (not part
  of the trace): U1 59,27 at client (957, 526), its target 60,28 at (1062, 397), U2 57,25 at (439, 843), the barbarian
  58,26 at (597, 673).
- U1's plot (plot hit 59,27); right click on 60,28 (`state.unit_x==60`, 78 ms). **Then the game's unit cycle selected U2
  and moved the camera to it** (AutoUnitCycle 1: U1 had no moves left after the step onto 60,28), which on 5.4.8 did not
  happen; so one step was added that only waits for it (`state.unit==131199`), and checkpoint `moved` expects turn 200,
  unit 131199 at 57,25 (the cycle happens only after U1's move, so the check still witnesses it). A 100 shot px drag of
  the map (from 480,230 on water); U2's plot (`state.unit==131199`); right click on the barbarian (4 s): **"Your
  Swordsman (6 damage) destroyed an enemy Warrior!"** (5.4.8: 8 damage); checkpoint `attacked` (turn 200); **Rotterdam's
  banner** (plot hit 58,29 at offset (-68, 14); `screen_open CityView`, 187 ms; Amsterdam's banner sat at the bottom edge
  of the picture); Return (`screen_closed CityView`); checkpoint `city`; the camera drag from 560,440 to 320,290 (5.4.8's
  640,400 was a resource icon here); release-large's steps 16-21 unchanged (RIGHT held 1.5 s, wheel -3 and +3, the
  game menu, EXIT TO MAIN MENU, Yes: `front_end main_menu` in 7.5 s); checkpoints `camera`, `menu`. The checkpoint
  names are release-large's five.
- check-trace `--journey`: clean, 21 steps, 5 checkpoints, hits control 4, plot 9, pixel 4, 23 actions; 6 local paths
  scrubbed. The game was stopped at the main menu by `cli.py stop` with the human-turn hook (a plain `cli.py stop`
  refuses on a game instance: "stop needs a human-turn check").

**Stock replays of release-large (5.5)** `[L]` (c5h-55-d, 22-civ -ia, the six keys 0, cp,vp,eui, 1914 x 1051):

| Run | Started (EST) | Exit | Steps | Checkpoints | Replay time | Output |
|---|---|---|---|---|---|---|
| 1 | 2026-11-25 20:31:2x | 0 | 21 of 21 | moved, attacked, city, camera, menu: all ok | 453 s | `C:/c5-64bit-work/0770/rl-stock-1/` |
| 2 | 2026-11-25 20:43:10 | 0 | 21 of 21 | the same 5, all ok | 462 s | `C:/c5-64bit-work/0770/rl-stock-2/` |

The game was stopped at the main menu after each replay by `cli.py stop` with the human-turn hook, restored by hash.

**release-basic-m43 on 5.5** (final recording 2026-11-25 22:18-22:31 EST on c5h-55-d, stock route, the 43-civ -ia DLL
4ef94f1d set by `set-dll`, the six keys 0; `F:/c5-64bit-work/0770/rec-basic-m43-c/trace.jsonl`, 64 steps; journey
`tools/hi/journeys/release-basic-m43-55.jsonl`):
- **On this save the city screen opens by itself after founding in about half the runs** `[L]`: not in recording
  attempts a (20:56) and b (21:04), yes in stock replay 1 of attempt b's journey (22:03, which therefore stopped at its
  step 11 with exit 6: the end-turn button is hidden under the city screen; no wait condition, so the stage's known-flake
  rerun could not have caught it), yes in a test replay to step 10 (22:13) and yes in the final recording (2 of 4).
- **ENTER is neutral to both states** `[L, 2026-11-25 22:15, c5h-55-d]`: in the city screen that opened by itself ENTER
  closed it (screens [] after 3 s); on the map with production blocking ENTER did nothing (state unchanged after 3 s).
  The journey's steps 10-13 are therefore: B with a plain 5 s wait; ENTER with `screen_closed CityView`; the end-turn
  button (CHOOSE PRODUCTION) with `screen_open CityView`; Warrior with `screen_closed CityView` (choosing a production in
  a city screen the blocking opened closes it, 8.3), no Return step and no known flake. check-trace warns once on step 10
  (its picture changed the open screens with only an `ms` wait): deliberate, the next step's wait holds in both states.
- Attempt b's journey (committed at 45fe30060, replaced at 2ed6da34b) kept the recorded deadline at step 10 and opened the
  screen by the end-turn button; its replay 1 is the exit-6 stop above (`F:/c5-64bit-work/0770/rbm-stock-1/`).
- Everything else as release-basic: settler 47,8 at shot (640, 352); research id 9; two SPACEs a turn; at turn 8 the
  production block and Monument; the turn-10 TechAwardPopup; save HI-0770-SAVED, load, main menu, EXIT, Yes. No leader
  screen on this map. Checkpoints: founded (turn 0), research 9, turn5, turn10 and loaded (turn 10, research -1, **gold
  40**), menu. check-trace exit 0 (the one warning above): 64 steps, 6 checkpoints, hits control 43, plot 1, pixel 12,
  147 actions; 9 paths scrubbed. The EXIT and Yes ended the game with the known `-ia` exit crash (a recording session:
  marked as a crash by the watcher, #16071).

**Stock replays of release-basic-m43 (5.5)** `[L]` (run in parallel on c5h-55-d and c5h-55-c, 43-civ -ia, the six keys
0, cp,vp,eui, 1914 x 1051):

| Run | Instance | Started (EST) | Exit | Steps | Checkpoints | Replay time | City screen after B |
|---|---|---|---|---|---|---|---|
| 1 | c5h-55-d | 2026-11-25 22:36:00 | 0 | 64 of 64 | founded, research, turn5, turn10, loaded, menu: all ok | 583 s | opened by itself; ENTER closed it |
| 2 | c5h-55-c | 2026-11-25 22:36:30 | 0 | 64 of 64 | the same 6, all ok | 605 s | did not open; ENTER did nothing |

So both states of the founding were met and passed. In run 2 the replayer pressed ENTER **once more** (8.9's one counted
retry for a key without effect: no judge change in 10 reads 500 ms apart), which is harmless here because ENTER does
nothing on the map. Outputs `F:/c5-64bit-work/0770/rbm2-stock-1/`, `rbm2-stock-2/`.

**Product-route replays of the 22-civ journeys (5.5)** `[L]`: one each on c5h-55-c with ticket 0765's 5.5 product build
as its 20:58 game laid it (0765's `p3_game.sh` form: `research/02-exe-internals/evidence/4967/lay.py prep <inst> --build
C:/c5-64bit-work/0765/x64final --work <pp> --composite-dll <0765 shell-build>/bin/shell_composite.dll --composite-ini
.../shell_composite.ini`, then `lay.py helper <inst> --work <pp> on`; the x64 server a2627c8ac, proxy 966a85b3..., the
instance's 22-civ -ia DLL 63e997c1 kept as the 32-bit DLL; no test pack), scratch `pp.sh`, restore by `lay.py restore`
(provision verify rc 0) and the settings files by hash:

| Journey | Started (EST) | Exit | Steps | Checkpoints | Replay time | Output |
|---|---|---|---|---|---|---|
| release-basic | 2026-11-25 22:55:11 | 0 | 64 of 64 | founded, research, turn5, turn10, loaded, menu: all ok | 694 s | `F:/c5-64bit-work/0770/rb-product-1/` |
| release-large | 2026-11-25 23:15:07 | 0 | 21 of 21 | moved, attacked, city, camera, menu: all ok | 464 s | `F:/c5-64bit-work/0770/rl-product-1/` |

The recordings carry over to the product route unchanged (the world drawn by the server, pictures from the composite).
release-basic's EXIT and Yes ended the product game with no crash and no box (the `-ia` exit crash is the stock route's).
**Not done:** the product replays of the two m43 journeys: no 43-civ 5.5 product layout existed on 2026-11-25 (lead-0763
#16054); release 0759's take replays all four on its candidate.

**Summary on 2026-11-25** (stock route, cp,vp,eui, 1914 x 1051, EXE 26577625): all four 5.5 journeys replayed 2 of 2
with exit 0 and every checkpoint ok; the 22-civ pair 1 of 1 on the product route. The 5.4.8 journeys stay in the index
unchanged (no `vp` and no `default_vp` in the index: read as 5.4.8) until beta 5 ships; after it, the lead decides
whether the index's `default_vp` becomes 5.5 or the 5.4.8 entries go.

**Instances afterwards**: c5h-55-c and c5h-55-d released with the six `[DisableSystems]` keys 0 in config.ini (the
template's files kept by hash in `C:/c5-64bit-work/0770/bak/`); c5h-55-d not removed (a coordinator's call).

**release-large-m43 on 5.5** (recorded 2026-11-25 21:22-21:53 EST on c5h-55-c, stock, 43-civ -ia, the six keys 0;
`F:/c5-64bit-work/0770/rec-large-m43/trace.jsonl`, 20 steps; journey `tools/hi/journeys/release-large-m43-55.jsonl`):
- Steps 1-6 from release-large-m43 unchanged (step 4's picture a load-screen glitch, exit 3 recorded). After the load
  **U2 (131256) is selected and centred** (as on 5.4.8's m43 scene). Pixels by `plot-pixel`: U1 9,45 at shot (991, 265),
  its target 10,45 at (1132, 265), U2 7,44 at (640, 352), the barbarian 7,43 at (720, 450).
- U1's plot (plot hit 9,45; U1 selected, the camera stays); right click on 10,45 (`state.unit_x==10`; U1 keeps
  moves, stays selected); checkpoint `moved` (turn 200, unit 131255 at 10,45); a 100 px drag up (from 300,170, sea);
  U2's plot (`state.unit==131256`); right click on the barbarian (4 s): **"Your Swordsman (12 damage) attacked an
  enemy Warrior (94 damage)"**, the barbarian survives with 6 HP (5.4.8's m43 scene: 7 and 87); the game's cycle then
  selected U1 and moved the camera to it; checkpoint `attacked` (turn 200). Cities by a read-only Lua read
  (`CanRangeStrikeNow`): Istanbul 8,42 true (beside the surviving barbarian; its banner would select the ranged
  strike, 8.6), Edirne 12,40, Ankara 9,46, Bursa 11,51, Surabaya 12,45 false. **Ankara's banner** (plot hit 8,47 at
  (79, 12); `screen_open CityView`, 170 ms); Return; checkpoint `city`; a drag from 520,470 (the bay) to 760,320;
  release-large-m43's steps 15-20 unchanged (step 18's picture a glitch, exit 3 recorded; `front_end main_menu` 13.3 s);
  checkpoints `camera`, `menu`. check-trace clean: 20 steps, 5 checkpoints, hits control 4, plot 9, pixel 4, 23
  actions; 6 paths scrubbed.

**Stock replays of release-large-m43 (5.5)** `[L]` (c5h-55-c, 43-civ -ia, the six keys 0, cp,vp,eui, 1914 x 1051):

| Run | Started (EST) | Exit | Steps | Checkpoints | Replay time | Output |
|---|---|---|---|---|---|---|
| 1 | 2026-11-25 21:57:49 | 0 | 20 of 20 | moved, attacked, city, camera, menu: all ok | 446 s | `F:/c5-64bit-work/0770/rlm-stock-1/` |
| 2 | 2026-11-25 22:09:07 | 0 | 20 of 20 | the same 5, all ok | 442 s | `F:/c5-64bit-work/0770/rlm-stock-2/` |

## 9. Runners and explorers (W5)

Sections 9.1-9.2: ticket 4507 (W5a), impl-4507 (account B), 2026-11-16. Sections 9.3-9.6 are 4508's.

### 9.1 The runner's charter `[C]`

`docs/briefs/hi-tester.md` is the whole brief of a runner. It is written for a Sonnet agent that has never seen the
project. It covers:
- the rule of contract section 1;
- what the owner hands over: instance, address, ticket, route, width, pure, mod set (default `cp,vp,eui`, MC #2508),
  charter, time;
- the CLI as built, with the step answer read field by field and the exit codes with what to do for each;
- the screen layout at width 1280, from this ticket's pictures;
- the safety rules: boxes (D155, COMMON 29), the deny list, the hotkeys of 12.3, saves under `HI-` names only, no
  window changes, `cli.py stop` only on the owner's word;
- findings by kind, with a fixed text form, including the end without a box of 12.5 (recorded as `wrong_state` with
  the exact last command, until 4511 adds a kind);
- ending a session; one hour and 150 steps per charter (MC #2408); a stuck rule;
- the report form to the owner, without the D47 ending.

Where the CLI differs from the contract, the charter follows the code at `epic-0392-hi-harness@70465e33`:
- `hi.py judge` is still "not built yet" (`hi.py` `NOT_BUILT`). The charter sends judge reads to `tools/hi/judge.py -i
  NAME state|snapshot|control PATH` instead.
- Judge waits are written `--wait "judge:COND,TIMEOUT_MS"`. Expressions take the form `state.turn>=10` (spaces
  optional, `judge.py` `_EXPR`).
- In the front end, only `in_game`, `front_end NAME` and `stable` can be waited on (`FRONT_END_OK`).

### 9.2 The reading test: which model reads the screen, at which width

**Question:** can a runner read the game's UI from a step picture, and at which width? The answers the judge knows
make this a measurement, not an impression.

**Set-up (2026-11-16 09:30-09:50 EST, impl-4507) `[L]`:**
- Instance c5h-a (C:), stock route, the instance's own DLL `i0ship-26234371-ia`, mods `cp,vp,eui`.
- Client 1914x1051: WindowRes 1920x1080, written before the launch and restored by SHA-256.
- Frame cap 30 fps. Save `HI4507-TINY_0008`, a copy of `C5H-0167-TINY_0008` (b2ceb663..., Tiny, Polynesia, turn 8).
- One HI session (`F:/c5-64bit-work/0392/4507/rt1`, 25 steps, every input a posted `hi.py step`).
- For each of 15 scenes a scratch script (`scene.py`, not committed; kept in `F:/c5-64bit-work/0392/4507/scripts/`) did three things:
  1. took a `shot` and kept the full-resolution frame `current_full.png` as `F:/c5-64bit-work/0392/4507/frames/sNN.png`;
  2. read the judge's `state` and `snapshot` right after the frame;
  3. read `state` again and refused the scene if turn, gold, unit, city, screens, popup or research had changed in
     between. This check is needed: right after the load, the game moved from turn 8 to turn 9 between a step's
     picture and the next read.

The 15 scenes:
- the load screen;
- the map at turns 9 and 10;
- tooltips: the top bar's gold, a plot, the city banner, a growth notification, the end-turn button;
- the Warrior and the Pathfinder selected;
- the city screen; the map after choosing production;
- the tech tree (F6), the game menu, the save screen with a typed name, the economic overview, the Civilopedia.

**Questions** (`tools/hi/reading/questions.json`):
- 56 questions on the 15 scenes, grouped by kind: turn 3, gold 6, unit 6, screen 7, production 6, tooltip 10,
  number 6, text 12.
- **32 are judge-backed:**
  - 18 from the `state` read;
  - 10 from the snapshot (gold rate, population, production, moves left in 60ths);
  - 1 from a control's text (the save-name box);
  - 3 derived (a production cost from the snapshot divided by the city's +4).
- The other 24 were read by impl-4507 on the full-resolution frames and checked against the judge where a value
  overlaps (for example the tooltip's income 7 = rate 6 + expense 1): 22 by that reading, 2 from the save.
- Answers hold numbers, identifiers and short keywords, never a game text in full (D83).
- The pictures stay on F:.

**Method:**
- `reading.py prepare` scales each frame to 960, 1280 and 1914 px with `capture.scaled`'s call. This is what a session
  at that width shows; 1914 is the native frame.
- One runner per (model, width), so that no runner sees a picture twice. Each runner is a background sub-agent of
  impl-4507 given `prompt-wNNNN.md` (`tools/hi/reading/runner-prompt.md`). It may only Read the questions file and
  the 15 PNGs, and Write one answers file. It runs no program, touches no game and holds no instance.
- `reading.py grade` scores the answers:
  - numbers: the first number in the answer;
  - text: required, accepted and rejected keywords;
  - the typed name: exact, ignoring case.
- Runs granted by the WMC through lead-0392 at 09:56: Sonnet x3 (at most 2 at a time) and Opus x3 (one at a time).

**Results (2026-11-16 09:57-10:01 EST) `[L]`.** Runner time: Sonnet 146 s over three runs, Opus 187 s over three runs (0.04 h and 0.05 h). Tokens are the harness's count for the whole sub-agent run (16 file
reads and one write), as the parent saw it. Runners cannot see their own count; every answers file says `null`.

| Model | Width | Correct (all 56) | Judge-backed (32) | Tokens | Wall time | Unsure (self-marked) |
|---|---|---|---|---|---|---|
| Sonnet | 960 | 55 (98 %) | 31 | 57,443 | 46 s | 8 |
| Sonnet | 1280 | 56 (100 %) | 32 | 65,535 | 46 s | 7 |
| Sonnet | 1914 | 56 (100 %) | 32 | 86,931 | 54 s | 4 |
| Opus | 960 | 56 (100 %) | 32 | 57,683 | 57 s | 3 |
| Opus | 1280 | 56 (100 %) | 32 | 65,442 | 61 s | 2 |
| Opus | 1914 | 56 (100 %) | 32 | 87,068 | 69 s | 2 |

- **The one miss:** Sonnet at 960 named Agriculture (the researched tech, drawn gold) as the tech being researched.
  The right answer is Pottery, drawn teal, judge `research` 1. All six runners marked the banner's production icon
  (q025) unsure, and four marked the research question (q028). These are the two hardest items: a 20 px unit icon
  and a colour cue in the tech tree. Opus at 960 got both right.
- **Cost per picture:**
  - From 960 to 1914 the run's tokens grow by about 30,000 over 15 pictures, about 2,000 per picture.
  - That matches the API's image cost of roughly one token per 28 x 28 px patch `[I, from the vision docs, not
    counted here]`: about 650, 1,150 and 2,570 tokens per picture at 960x527, 1280x703 and 1914x1051.
  - 1914 is below the current models' 2,576 px long-edge limit `[C, the API's model notes]`, so the native frame
    reaches the model unscaled `[I for the Read tool's own path]`.
- **The set saturates: 5 of 6 runs scored 56 of 56.** It shows that Sonnet reads early-game Tiny pictures well
  enough at 1280. It does not rank the models.
  - The pictures come from a Tiny early-game save with one city and two units.
  - **Open item for 4508's first campaign review:** a harder set, taken from a mid-game or large save. It needs:
    - a busy top panel;
    - small numbers in tooltips;
    - overlapping units and stacked flags;
    - a crowded city screen (long production and building lists, a queue).
  - The same tool (`reading.py`) and the same scene check serve it.

**Chosen default: width 1280, model Sonnet** (lead-0392 reviews).
- At 1280 Sonnet answered everything right, as Opus did at every width. Sonnet at 960 lost the one colour cue.
- Price: Opus costs twice Sonnet per token ($4 against $2 per million input tokens, the API's list). Opus at 960
  (57,683 tokens) therefore costs about 1.8 times Sonnet at 1280 (65,535) for the same score.
- 1914 costs about 2.2 times the image tokens of 1280 and bought no correct answer here.
- The charter keeps `zoom` for small numbers: a zoom region is cut at full resolution, so a runner at 1280 reaches
  native detail where it needs it.
- Opus brings nothing measurable for reading on this set. Its place in the proposal (4.5: "Opus where W5's reading
  test says Sonnet cannot read the screen") is not triggered.
- Contract 4.1's default (`--width 1280`, `session.DEFAULT_WIDTH`) therefore stays; no code change.

**Live observations on the way `[L]`** (stock route, the set-up above):
- **`SHIFT+ENTER` did not end turn 9 while units needed orders.**
  - Before the key the judge read `block_name` `ENDTURN_BLOCKING_UNITS` and `turn_active` true. The key was delivered
    as VK 13 with Shift (step 15).
  - After the key, the step's 180 s wait `state.turn>=10` reached its deadline: still turn 9, the same block.
  - `SPACE` on each unit, then a click on the end-turn button, ended the turn in under 150 s.
  - 4510 saw `SHIFT+ENTER` end turn 8 (12.2) on the product route with `cp,vp`. Its block there is not recorded.
  - Whether the game's force-end refuses a units block, or the posted Shift did not reach the game's key handler, is
    open `[I]`.
- **A hover on the city banner was recorded as a `plot` hit** ([50,15], `at` [84,55]), not as UI (step 24).
  - The banner tooltip showed in the picture.
  - W2's world witness (`/InGame/PlotHelpManager/TheBox`) evidently reported the mouse over the map there.
  - A gap in the hit classification; lead-0392 passes it to 4511.
- **The judge's `screens` list does not include the Civilopedia.** After a right click on the gold it read `screens`
  empty and `popup` false while the Civilopedia filled the picture (scene s13).
- **Choosing a production in the city screen closed it by itself**, and the camera moved (s04 -> s05).
- **The game at turn 9 after the load went on to its AI turn at once.** Step 1's picture is turn 8 (`turn_active`
  false). The next read, about 20 s later, said turn 9 with the human's turn active. Pictures and reads must be
  paired (the scene check above).

Records:
- Frames and judge reads `F:/c5-64bit-work/0392/4507/frames/` (sNN.png, sNN.json).
- Prepared pictures, prompts and answers `F:/c5-64bit-work/0392/4507/reading/` (w0960, w1280, w1914, `answers/`,
  `runners.tsv`).
- The session `F:/c5-64bit-work/0392/4507/rt1/`.
- Restored at 09:50: the settings backup equal by hash, the three front-end files "original", the save copy removed
  (kept on F:), c5h-a released.

### 9.3 The explorer charters and the finding pipeline (4508) `[C]`

Ticket 4508 (W5b), impl-4508 (account B), 2026-11-16. Code: `tools/hi/charters/` (the charters, their index
`charters.json`, the owner's driver `campaign.py`, hermetic tests `test_campaign.py`).

**The charters.** One file per screen or habit; each is the per-session part of a runner's brief
(`docs/briefs/hi-tester.md` section 2): start state and save, goal, what to cover in order (about 8 minutes a part),
out of bounds beyond the brief's 5.2, and what the report names. Defaults from the index: product route, `cp,vp,eui`
(MC #2508), width 1280 (9.2), 60 minutes and 150 steps (MC #2408), Sonnet.

| Charter | Save (an `HI-` copy, on F:, by SHA-256) | Ends turns |
|---|---|---|
| city-screen | `HI-4508-LARGE-T200` = `9071-LC21B-T200` (e3da1d17...; Large Communitu, 21 majors, VP 5.4.8, turn 200, the Netherlands; corrected 2026-11-16 by lead-0398 from 4600's live read and the save header, was "the Shoshone") | no |
| tech-tree | the same | no |
| diplomacy (and the overviews) | the same; no deal, war or gift confirmed | no |
| camera-selection | the same; moves of at most two units, no attack | no |
| save-load | `HI-4508-TINY-T8` = 4507's `HI4507-TINY_0008` (b2ceb663...; Tiny, Polynesia, turn 8); saves `HI-4508-SL-*` | up to 3 |
| options | the same; the VIDEO tab never opened; the owner backs up every `.ini` of the user folder | no |
| pure-proof (not part of a campaign) | `HI-4508-TINY-T0` = 4504's `HI4504-T0` (68ed61e6...; Tiny, Russia, turn 0) | to turn 5 |

Charters that do not end turns keep a session on one turn, so its trace replays (an AI turn in between would make the
replay diverge, 7.6).

**The pipeline, as built** (`campaign.py`; contract section 10):
1. `findings RUN` lists the runner's findings in the session trace.
2. `harden RUN --finding N --charter C [--expect JSON] [--checks ...] [--gone]`: the owner's copy of the trace. The
   header's `save` is filled from the charter (`session start` writes `null` there). A judged finding (`wrong_state`,
   `input_ignored`, `wrong_picture`) gets a checkpoint inserted after its step, with what the player expected as
   `expect` (judge fields) or picture `checks`: a replay can only fail a checkpoint (`report.reproduced`), so a runner
   finding without one never reproduces. A box or hang needs none. `--gone` marks the end of the game without a box
   (12.5): a run reproduces it when the replay ends with the process gone and no box. The finding is re-signed
   (`<kind>-<checkpoint>-<fields>`); the runner's signature is kept as `runner_signature`.
   Added during the campaign (9.4): `--changed auto|full|x,y,w,h` for an `input_ignored` whose effect the judge cannot
   read (checks.py `changed`, contract v1.11: the step's picture against the one before it, evaluated on each
   replay's step pictures; a pass with the top band moved and the same open screens is refused as a capture
   glitch), and `--skip` for steps the finding does not need (a minimal cut). `retally PIPE` recomputes the tally
   and the class from the replays already run; the stock class is read against the first product replay that
   reproduced.
3. The cut (`report.cut_trace`) is checked with `trace.validate` before anything runs.
4. `repro PIPE -i NAME`: twice a fresh game from the save (`frontend.py prepare --mods --load`, `cli.py launch
   --assert-policy hold`, `frontend.py ingame`, the load screen closed once by `frontend.py dismiss`), `hi.py replay`
   of the cut, `cli.py stop` with the human-turn hook; 2 of 2 or an observation (`report.reproduce`'s rule). A box
   after a replay stops further launches (the box is the owner's, COMMON 29).
5. `stock PIPE -i NAME`: `stockctl.RouteSwitch` (`vpproxy.ini mode=off`, restored by hash), one replay, the class
   from `stockctl.compare` / `classify_finding` against the first reproduction.
6. `report PIPE --ticket T --by A [--post --live]`: `report.write_report` and the MC's note (`report.queue_note`, a
   dry run unless both flags).
7. `yield CAMPAIGN.json`: findings and reports per charter hour, and the share the reproduction filtered out.

**Found on the way (2026-11-16 10:37, offline on 4505's `rec10b` trace) `[C]`:** `report.cut_trace` wrote its end
record without `ms`, `exit` and `steps`; `trace.validate` gives two schema problems for it, and `hi.py replay` refuses
a trace with schema problems (exit 1). So no cut trace could be replayed, and the 2-of-2 filter had only run on fake
replays (7.5's dry run). 4511 fixes `cut_trace` (task-4511-hi-sweeper@62dd25e8); until it is merged `campaign.py`
repairs its own cut (`_fix_end`, a no-op on a complete end record).

**The release candidate for the campaign** is 4506's (8.1): the first playable build `x64-04b1211b-pfdee656` with
`i0ship-26234371-ia`, laid onto the instance the way 4506's recording did (scratch `C:/c5-64bit-work/0392/4508/prep.py`,
a copy of 4506's `rec.py` that also backs up every `.ini` of the user folder and copies the three `HI-4508-*` saves in).
No composite: PrintWindow is the picture source on both routes.

### 9.4 The first campaign `[L]`

Release candidate as 9.3; instances c5h-c and c5h-a (C:, claimed by impl-4508, ticket 0392, assert policy hold);
client 1914x1051 (WindowRes 1920x1080, D158); frame cap 30 fps; Sonnet runners started by impl-4508 as sub-agents
with `campaign.py prompt` (WMC grants: runner 1 at 10:30, runners 2-6 at 10:51, via lead-0392). Records:
`F:/c5-64bit-work/0392/4508/hi/<session>/` (trace, step pictures, `run.json`, `report.md`), the campaign record
`F:/c5-64bit-work/0392/4508/campaign.json`.

**Runner 1, city-screen** (c5h-c, 2026-11-16 10:38:40-10:48:43, `hi/city-1`):
- 10 minutes of its 60, 34 steps, 112,435 tokens (about 3,300 per step), 72 tool uses. The session ended cleanly,
  turn 200, no box.
- Covered: open from the city list (the list's name only moves the camera; its icon opens the screen), close by
  Return and `ESC`, the Next/Prev arrows, the production list (wheel, tooltips), queue add and remove, focus changes
  and back, a great-work slot (opens the Culture overview), the Civilopedia by a right click on a building.
- Not reached: buying (38 gold, the cheapest item 42), opening by a banner or a notification, citizen tiles (a
  puppet city), specialists, the tile-buy mode.
- **One finding, `usability` step 11:** "after Next/Prev the map behind the city screen is zoomed out with yield
  icons; the first open showed a close-up". **Filtered by the owner as capture timing:** steps 4 and 8 were taken as
  `judge:screen_open CityView` returned, while the camera was still flying to the city (no city and no ring in the
  picture); step 11 is the settled city view. Lead-0392 accepted. Lessons, committed before runner 2: the brief's
  4.2 (a `stable` wait before judging a screen's look) and every charter's "Using the hour" (keep exploring until
  minute 50; another way when a part fails).
- Not checked by the runner: tile-buy hex labels "12" against a tooltip "You need 50 Gold to buy this land" (could be
  two different numbers; unverified).

**The pipeline proven live on runner 1's trace, with a planted finding** (c5h-c, 10:56-11:12):
- A copy of the session (`hi/city-1-planted`) with an owner finding after step 11; `harden --expect
  '{"city_name": "Rotterdam"}'` (the game shows Amsterdam there) gave checkpoint `planted-rotterdam` and the cut of
  11 steps.
- `repro`: two fresh games from `HI-4508-LARGE-T200` (in game after 178 s and 165 s), each replay 11 of 11 steps in
  65 s (the recording took 198 s: recorded deadline waits are replayed as their `took_ms`, judge waits end early),
  each stopped at the checkpoint with exit 6, `city_name` expected Rotterdam, got Amsterdam: **2 of 2**.
- `stock`: `vpproxy.ini mode=off` (backup fbe61a65..., restored to the same hash), a fresh game (139 s to in game), 11
  of 11 steps in 64 s, the checkpoint fails the same way: class **stock too** (as planted: the expectation is wrong on
  both routes). RouteSwitch's own `provision.py verify` answered rc 1 after the restore, as expected on an instance
  carrying the release candidate's layout (set-dll and set-proxy differ from the provisioned manifest; the prep's own
  restore verifies after the layout is removed).
- `report`: the folder `hi/reports/wrong_state-planted-rotterdam-city-name/` (report.md with 22 player steps, the cut
  trace, after.png, the save copy, build.json: EXE 04cfdc71..., DLL 29f58e17..., server 5b254430..., finding.json)
  and the MC's note as a **dry run only** (planted, never posted).
- So a runner's real trace replays step for step on fresh games of both routes, and the filter, the class and the
  report work on it. Two small defects seen: report.py's default report root is `F:/c5-64bit-work/<ticket>/...`, outside
  the epic's folder (campaign.py now passes `<hi>/reports`); and the steps to reproduce print a judge wait as "Wait
  None ms" (report.py's `describe_action`; told to the lead).


**Runner 2, tech-tree** (c5h-a, 10:59:30-11:24:36, `hi/tech-1`):
- 25 minutes (it stopped after covering the charter's list twice), 72 steps, 186,953 tokens, 151 tool uses; clean end,
  turn 200, no box.
- Findings: `input_ignored` step 19 (a left drag on the open tech tree does not scroll it; the wheel, the scroll bar
  and its arrows do) and four `usability` notes (an empty "Unique Actions:" bullet in the Future Tech tooltip; the
  judge silent 8 s after three F6 in one step; the queue badge text runs into a short tech name; held arrow keys
  with the tree open pan the map behind it).
- A click with a modifier has no short form in `hi.py step`; the runner found `--json -` with `"mods": ["SHIFT"]`
  (now a row in the brief's 4.1).

**The drag finding through the pipeline: a witness by pictures** `[L]`:
- The judge cannot read a tree's scroll position, and no picture check of 7.4 says "the picture should have changed",
  so an `input_ignored` of this kind could not pass the 2-of-2 filter. Added on lead-0392's word (2026-11-16 11:2x):
  `checks.py` `changed` (contract v1.11 line in 8.2): the step's picture against the one before it, over a region,
  fails when at most `CHANGED_MIN` = 0.01 of the sampled pixels changed. Measured on runner 2's pictures (sampled at
  320 px): the ignored drags 0.0003-0.0032 of the whole picture, a wheel scroll of the tree 0.2534, a tooltip 0.0236.
  `campaign.py harden --changed auto|full|x,y,w,h` evaluates it on each replay's step pictures (`replay_pair`
  follows the replay trace's `replay_of`, so a cut with gaps works).
- First attempt (11:29-11:41, cut of steps 1-19, `--checks not_flat` only): the product replay stopped at step 17
  (exit 6, `/InGame/TechTree/TechTreeScrollBar` not found on this client after 6 tries: the runner's drag of the
  scroll bar). So `harden --skip` leaves out steps a finding does not need: the minimal cut is steps 1-14 and 19.
- Second attempt (11:41-11:57, `--changed auto`, the drag's box): replay 1's step picture was **a PrintWindow frame of
  the world without the UI** while the judge had the tree open (capture's retake rule accepted it, two such frames
  agreeing); replay 2's drag ended over a tech and its tooltip counted as a change. Two consequences in campaign.py:
  a `changed` that passes while the picture's top band (capture's rows 0-39) moved by more than capture's `BAND_DIFF`
  (25) with the same open screens is refused as a capture glitch (`ok: null`); and the region is the era bar strip
  (`0,0.146,1,0.022`), which scrolls with the tree and is away from the cursor.
- Third attempt (11:57-12:08, two fresh product games) and the stock replay of the second: the strip unchanged in all
  three (0.0000): **2 of 2, stock too**. Report folder `F:/c5-64bit-work/0392/4508/hi/reports/input_ignored-step19-changed/`.
  Lead-0392 (#2635): not posted to the MC; listed in the campaign summary as "stock too, design, not posted" (drag
  scrolling is not a behaviour the tree offers on either route).

**Runner 3, diplomacy** (c5h-c, 11:16:50-11:59:10, `hi/diplo-1`):
- 40 minutes, 73 steps, 183,116 tokens, 143 tool uses; clean end, turn 200, no box. It opened the World Congress,
  the Diplomacy Overview (three tabs), the corporations, demographics, espionage, military, notification log, vassal,
  religion, economic and victory overviews, three leader screens (England, Mongolia, Rome) with trade tables that it
  cleared or refused, a DEMAND screen, and one city state's popup; nothing was proposed or confirmed. Top-bar numbers
  agree with the overviews (approval 66 %, supply 15/16, faith +22.5 shown as +22, gold -9.02 shown as -10).
- Findings: `input_ignored` step 60 (a click on a city state in the Diplomacy Overview opens no popup while the overview
  stays: judge `screens` DiploOverview only; at step 18 the same kind of click closed the overview and opened the
  CityStateDiploPopup at once), with its duplicates step 62 and `wrong_state` step 68 (after the overviews were
  closed the CityStateDiploPopup was open), and one `usability` (a combat preview shows through the World Congress
  screen). The judge was silent (8 s per step) on every leader and trade screen.

**The city-state finding through the pipeline** `[L]` (c5h-a, 12:08-12:56):
- `harden --expect '{"screens": ["/InGame/CityStateDiploPopup"]}'` after step 60 (what step 18 showed for the same
  kind of click). The cut is all 60 steps (leader screens, trade tables refused, overviews).
- Product: two fresh games, each replay 60 of 60 steps (12.9 min and about 12 min; the judge is silent 8 s on every
  leader and trade screen), the checkpoint read `screens` [DiploOverview] both times: **2 of 2**. Stock: 60 of 60 steps
  in 9.7 min, the same reading: **stock too**. Report folder
  `F:/c5-64bit-work/0392/4508/hi/reports/input_ignored-owner-cs-popup60-screens/`. Lead-0392 (#2674): not posted;
  "stock too, player-visible oddity, not posted" in the campaign summary. Rule for the rest of the campaign (#2674):
  only findings that are product-only (or worse on product) go to the MC as notes, each after the lead's review.
- So a 60-step runner trace with leader screens, trade tables and seven overviews replays step for step on both
  routes, three times out of three.

**Runner 4, camera-selection** (c5h-c, 12:04:50-12:43, `hi/camera-1`):
- 37 minutes, 104 steps, 257,772 tokens, 216 tool uses; clean end, turn 200, no box. **No grey, black or missing
  tiles, no flicker and no frozen picture** after any pan (drags in all directions, a five-drag chain, held arrow keys
  2-4 s, minimap clicks and a minimap drag), zoom (5-25 wheel ticks both ways, strategic view by F10 and by its
  button) or unit jump; two units moved and a few slept or fortified, as the charter allows.
- Findings, all picture-only and low confidence: `wrong_state` 73 (with a unit selected, a left click on an empty plot
  moves the camera toward it), `wrong_state` 89 (a click on the city list's name does not select the city: EUI's
  design, runner 1 saw the same; filtered by the owner, lead-0392 agreed), `wrong_picture` 94 (after ESC from the city
  screen the tile-yield and ownership overlay stays on the map until a drag).
- **73 and 94 could not be replayed:** the cut's product replay stopped at step 3 (exit 6: "plot [72, 5] not under
  the cursor at its pixel now, nor after a search by moves"): the runner's map drags start on plots near the
  picture's top edge (y 100 of 703), and after two replayed drags the camera is not where the recording had it.
  A trace that pans by drags from edge plots does not replay; they stay observations (unverified).
- Facts for the brief: **a positive wheel count zooms in** (the brief's 4.1 said the opposite; fixed 12:52 on the
  lead's word, #2668); Z does nothing, F sleeps or fortifies, SPACE is "do nothing", M move mode; selecting a city sets
  the interface mode `INTERFACEMODE_CITY_RANGE_ATTACK`.

**Harness gaps seen by the campaign** (for the lead's list; not fixed here, lead-0392 #2668):
1. **Burst frames marked glitch though good:** every frame of two bursts of runner 4 has `"glitch": true` while the
   frames and a shot just after are fine: `F:/c5-64bit-work/0392/4508/hi/camera-1/burst_0069_0[0-2].png` and
   `burst_0099_0[0-3].png` (capture.py's band rule against the session's reference band; `burst` does not retake).
2. **A PrintWindow frame of the world without the UI accepted as a step picture** (product route, first playable,
   tech tree open by the judge): `F:/c5-64bit-work/0392/4508/hi/tech-1/pipeline/input_ignored-step19-changed/`
   first `repro1/step_0015.png` (since overwritten by the rerun; the measurement is in this section's drag entry).
   capture's retake accepts a changed top band when the retake agrees; two such frames agree.
3. **UI hits classed as plots:** the city list's name (runner 4 step 89) and the city banner (9.2) are recorded as
   `plot` hits: the world witness (`PlotHelpManager/TheBox`) reports the mouse over the map there.
4. **Map drags from edge plots do not replay** (runner 4's cut, above).
5. `report.py`'s steps to reproduce print a judge wait as "Wait None ms" (4511 changes it).

**Runner 5, save-load** (c5h-c, 12:48:30-13:31:20, `hi/saveload-1`):
- 42 minutes, 81 steps, 174,733 tokens, 141 tool uses; clean end, in game, no box. Four saves (`HI-4508-SL-1..4`) by
  the game menu and six loads (game menu and main menu) all landed on the right turn and gold. The load click to
  "Continue" about 10 s, Continue to a settled map 3.3-3.7 s, exit to the main menu 8.9-11.7 s; three turns played.
  QUICK SAVE was left alone (it writes "QuickSave", not an `HI-` name).
- Findings: `wrong_picture` 73 (the loaded game's world shows blended over the load screen's leader picture, on every
  load) and one `usability` (ENTER on the exit-without-saving box closes the box without exiting).
- **The load screen, a parity look** (13:33-13:41, c5h-c, `HI-4508-TINY-T8` auto-loaded, `cli.py shot` while
  `frontend.py ingame` read `load_screen_ready`, DIAGNOSTIC, `F:/c5-64bit-work/0392/4508/loadshot/`): the world shows
  through the load screen the same way on product and on stock: an observation, not the patch. Whether a player sees
  it or PrintWindow composes it is not checked.
- Harness notes: `--wait judge:front_end load_screen` returned at once while the old menu still showed; a step
  picture during a load timed out once (exit 3).

**Runner 6, options** (c5h-a, 13:06:50-13:37:40, `hi/options-1`):
- 30 minutes, 64 steps, 186,187 tokens, 134 tool uses; clean end, turn 8, no box. Game, interface and audio options
  changed and set back (Map Info Delay ended 1.4 s instead of 1.5 s; the instance's `.ini` files were restored by hash
  in any case), the minimap panel's toggles, the strategic view; the VIDEO and MULTIPLAYER tabs never opened.
- Findings: `input_ignored` 42 and one `usability` (the minimap panel's Hide Recommendations does not follow the Game
  Options checkbox while it is open).
- **Through the pipeline** (c5h-c, 13:58-14:19): the ignored click is at step 41 (the click at 42 is the one that
  worked), so the owner restated the finding there (`hi/options-1-owner41`). Witness `changed` over the checkbox
  (`0.449,0.505,0.0234,0.0356`): 0.0000 between steps 40 and 41 of the recording, 0.2679 when the box toggled. The
  options persist in the instance's `.ini`, so `campaign.py --reset-user-files` (new) puts a snapshot of c5h-c's
  prepared `.ini` back before every fresh game. Product 2 of 2 (0.0000 each; 41 steps in about 6 minutes), stock the
  same: **stock too**. The screen was ready before the click by the judge (`screens` OptionsMenu_InGame at step 40) and
  by the picture (the Interface tab drawn), and the click failed 3 times of 3: deterministic, not a lost posted input.
  Report folder `F:/c5-64bit-work/0392/4508/hi/reports/input_ignored-step41-changed/`.

### 9.5 The pure-mode proof `[L]`

**Done 2026-11-16 13:50-13:56 EST** (runner run-4508-pure, Sonnet, sub-agent of impl-4508; WMC grant 11:17 via
lead-0392). Instance c5h-c, product route (the first playable, laid as 9.3), `cp,vp,eui`, width 1280, client
1914x1051, save `HI-4508-TINY-T0` (68ed61e6..., Tiny, Russia, turn 0, the settler unmoved). Session
`F:/c5-64bit-work/0392/4508/hi/pure-1` (`session start --pure`).

- **Result: the runner founded Moscow and played turns 0 to 5 from pictures alone**, in 16 steps (34 actions) and
  6 minutes; 85,339 tokens, 45 tool uses, 7 minutes of runner time. No finding, no box. The owner's judge read
  afterwards: turn 5, no screen open.
- **The trace is pure:** the header says `pure: true`; no step carries a `judge` field; every hit is a raw `pixel`
  hit (10) or none (keys, waits: 24); 34 write-ahead `intent` records (contract v1.10); `check-trace` clean.
- **How the runner knew a turn had ended, by pictures:** "Turn: N" and the year at the top right (confirmed once by a
  `zoom`), the research counter "Pottery (N)" counting down, the end-turn button's text going back to "A UNIT NEEDS
  ORDERS"; during the AI turn the picture showed "PLEASE WAIT" with "Processing Turn For City States".
- **Steps per turn:** turn 0 ten (found by `B`; the end-turn button's CHOOSE PRODUCTION opened the city screen and
  the Warrior choice closed it; CHOOSE RESEARCH opened the tech tree, Pottery; the warrior moved one plot by a right
  click, 3/3 to 2/3 moves; SPACE on each unit; the end-turn button; a `wait 8000` plus `stable`); turns 1 and 2 two
  each; turns 3 and 4 one each (SPACE twice, the end-turn click and the wait in one step).
- **What the runner could not know without the judge:** unit coordinates, whether the turn was active during the AI's
  processing, exact gold and yields (the yield numbers beside the city banner were too small; no zoom tried).
- So a Sonnet runner in pure mode plays an early game's turns as fast as with the judge; the judge is for checking,
  not for driving. Not tested in pure mode: a mid-game save, a long session, a fault.

### 9.6 The yield and the campaign size `[L]`

The first campaign (2026-11-16 10:38-13:37 EST; six charters, Sonnet runners, product route on the first playable,
`cp,vp,eui`, width 1280; instances c5h-c and c5h-a, at most two runners at a time). Record:
`F:/c5-64bit-work/0392/4508/campaign.json`; `campaign.py yield` on it gives the table. Minutes are the runners' own
session minutes (`session start` to `session end`).

| Charter | Minutes | Steps | Tokens | Findings | Reproduced 2 of 2 (class) | Observations | Usability |
|---|---|---|---|---|---|---|---|
| city-screen | 10 | 34 | 112,435 | 1 | 0 | 0 | 1 (filtered as capture timing) |
| tech-tree | 25 | 72 | 186,953 | 5 | 1 (stock too: drag does not scroll the tree) | 0 | 4 |
| diplomacy | 40 | 73 | 183,116 | 4 (2 duplicates) | 1 (stock too: city-state popup waits behind the overview) | 0 | 1 |
| camera-selection | 37 | 104 | 257,772 | 3 | 0 | 3 (one EUI design, two not replayable) | 0 |
| save-load | 42 | 81 | 174,733 | 2 | 0 | 1 (stock too by a parity look: the world shows through the load screen) | 1 |
| options | 30 | 64 | 186,187 | 2 | 1 (stock too: the first checkbox click after the screen opens is ignored) | 0 | 1 |
| **total** | **184** | **428** | **1,101,196** | **17** | **3** | **4** | **8** |

- **Yield:** 5.5 findings per charter hour; 0.98 reproduced reports per hour; **0 product-only findings**: every
  reproduced fault is the same on the stock route (EUI or game behaviour), so nothing went to the MC as a finding
  (lead-0392's rule, #2674: only product-only or product-worse findings are posted). The filter: of the 7 non-usability,
  non-duplicate findings, 3 reproduced 2 of 2 and 4 stayed observations (`filtered_share` 0.57); none of the 3
  reports was product-only, so the stock control removed all of them as patch faults.
- **No world-picture fault on the product route** in 37 minutes of camera work on a Large turn-200 map (runner 4) and in
  the other sessions: no grey, black or missing tiles, no flicker, no freeze. The first playable has no composite (the
  EXE draws everything, 8.1), so this says little about the renderer work of later increments.
- **Cost:** 1.10 million runner tokens over 6 charters (about 184,000 per charter, 2,600 per step); runner wall time
  about 3.2 hours (Sonnet). The owner's pipeline time on top: 3 reproduced findings at 2 product + 1 stock replay each
  (about 15-45 minutes of an instance each, mostly loads of a Large save: 99-199 s to in game), 2 parity looks.
- **Runners stop early:** 10, 25, 40, 37, 42 and 30 of 60 minutes, even with "Using the hour" in the charters after
  runner 1 (they stop when the list is done twice). A charter hour therefore buys about 30-40 minutes of exploration.
- **What did not work in the pipeline** (9.4's gaps): findings that the judge cannot see need a picture witness
  (`changed`, built in this campaign); camera traces with drags from edge plots do not replay; a picture-only
  `wrong_picture` (an overlay that stays) has no automatic check.

**For the MC's review of the campaign size** (proposal, not a decision): on a release candidate whose world is drawn
by the EXE, six one-hour charters found no patch fault and cost about 1.1 million Sonnet tokens plus about 4 hours of
two instances. The yield of product-only faults is the number that matters, and it was 0 here; the campaign is worth
its cost on a candidate that changes what the player sees (the composite and the server's renderer of increment 3),
with charters aimed at those changes (camera and world pictures, loads, the city screen on a large map), and with the
mid-game reading set of 9.2's open item. Not measured: a campaign on a renderer build; Opus runners.

## 10. The real-input lane (W6)

Ticket 4509, impl-4509 (account B), 2026-11-16. Code: `tools/hi/realinput.py`; tests
`tools/harness/tests/test_hi_realinput.py`; the named exception `HI_REAL_INPUT_OFF_WORKSTATION` in
`tools/harness/tests/test_safety_static.py` (MC ruling #2407, 2026-11-16 07:22).

### 10.1 The module and its fence [C]

- **The fence.** `realinput.fence()` raises `RealInputRefused` (exit 2) when the SHA-256 of the lower-cased host name
  (`socket.gethostname()` or `COMPUTERNAME`) is the workstation's, the constant of `steam_launch.py` (issue 0390; a test
  checks the two are equal), and when no host name can be read. Every function of the module that reaches user32,
  SendInput or the harness's window modules calls it as its first statement; user32 is loaded only by
  `_load_user32()`, which fences a second time with the host name read directly (a test that fakes the name cannot
  reach the real user32). The harness modules that load user32 at import (`actions`, `winapi`) are imported only
  past the fence. Hermetic tests prove the refusal on the workstation for every entry point and the CLI with a spy on
  `ctypes.WinDLL`/`windll`: no user32 call (59 tests, 2026-11-16 07:58).
- **The machine in records (ticket 0433, 2026-11-16) [C].** Host names are compared in memory only: what HI writes (the trace header's `host`, `probe`'s record) comes from `realinput.host_alias()`: `workstation` by the name's hash (first, so a `TESTBED_HOST` set on the workstation cannot relabel it), else `C5_MACHINE_ALIAS` / `TESTBED_HOST` when it names a host of `tools/testbed/hosts`, else `other` (`tools/harness/tests/test_hi_host_alias.py`, 15 tests). The journeys recorded before this carried the workstation's own name; replaced by `workstation`, check-trace clean.
- **The named exception.** One `SendInput` site, `_send` (all events of one action in one call, so the system inserts
  them unbroken). `test_safety_static.py` lists `("hi/realinput.py", "_send")` and fails when the site moves (a new
  site) or disappears (a stale entry), and `test_hi_real_input_sites_call_the_fence_first` fails when `fence()` is not
  its first statement (positive controls in `test_fence_first_check_has_teeth`).
- **The lane.** The eight input functions of `tools/hi/actions.py` (move, click, double_click, drag, wheel, key, hold,
  type_text) with the same signatures; `real_lane(target)` puts them in place of actions.py's for its duration, so
  `actions.run`, `actions.hover`, the replayer (`tools/hi/replay.py`) and the judge's hover searches (`judge.py`,
  `uimap.py` call `actions.move`) deliver real input unchanged, and switches the game's focus guard off (shared-memory
  `enabled` 0: no activation veto, no virtual cursor, no pretended activation; the guard DLL stays loaded because the
  instance's document redirection is in it, so "no hooks of ours" is approximated by hooks that pass everything
  through). Both are put back on exit.
- **Before every action:** the box check (exit 8), clipping (exit 2), the deny list (as on the posted lane), then the
  desktop check: the input desktop is `Default` (`OpenInputDesktop`; a locked screen or a secure prompt cannot be
  opened), the session is not locked (`WTSSessionInfoEx` SessionFlags), the game's main window is the foreground
  window and not minimised; a point must be on the game's own window (`WindowFromPoint` root). Otherwise exit 2 and
  nothing is sent. Moves are absolute (`MOUSEEVENTF_ABSOLUTE | VIRTUALDESK`) in physical pixels (the process is
  per-monitor DPI aware); keys carry the scan code and the extended flag; `type` is `KEYEVENTF_UNICODE`; `hold` repeats
  key-downs at the system's typematic rate. Every event carries `dwExtraInfo` 0x48495249 ("HIRI").
- **Releases always go out.** A key or button that went down is released also when the focus was lost while it was
  held (a stuck key on the machine is worse); nothing new is pressed then (`_release` refuses a press).
- **Focus without keys.** `focus_game` restores a minimised window (`SW_RESTORE`), moves only the game's window in the
  z-order (topmost, then not topmost) and calls `SetForegroundWindow`, then with `AttachThreadInput`, then
  `SwitchToThisWindow`. Unlike `steam_launch.foreground` it sends no ALT tap (MC #2407: keys only to the foreground
  game).
- **The testbed job.** `realinput.py job` is `tools/testbed/jobs/live_session.py` with two more ops, `realinput` (this
  module's commands in the job's instance) and `hi` (hi.py's), driven by `tools/testbed/live.py --host laptop send JOB
  '{"op": "realinput", "args": [...]}'`. The job claims its instance in its own pool before the launch (owner
  `hi-<ticket>-job`, the legacy form whose 4-digit id is the running ticket), because hi.py's sessions need a claim
  with a ticket and the laptop has no queue store; the testbed runner already gives the job the instance alone.
  **Corrected 2026-11-16 by lead-0398 (0398, MC #2873):** the owner was a fixed `hi-0392-w6-job`, so every later
  ticket's laptop sessions and pictures carried ticket 0392 (seen by 4602). The job now takes `--ticket ID` (or
  `C5_HI_JOB_TICKET`) and refuses with exit 2 before anything starts without a 4-digit ticket
  (`realinput.job_ticket`, `job_owner`; tests in `test_hi_realinput.py`). Submitted
  with `--quiet --headed`: one real-input run at a time and nothing else of ours on the laptop meanwhile (MC #2407).
- **Nobody at the machine.** The probe refuses to send anything when the session's last input (GetLastInputInfo) is
  under 120 s old.

### 10.2 The first step: the logged-on session, the focus and a locked screen (the proposal's section 10)

**Setting of every laptop run here** (2026-11-16 08:01-09:30 EST, impl-4509): testbed jobs submitted `--quiet
--headed` (one real-input run at a time, nothing else of ours on the laptop), `realinput.py job --load
HI4509-TINY_0008`: instance `tb1` (the laptop's own Steam EXE, `commission.py set-exe`), the testbed's copy of the
game with **Vibrant Textures** (asset set: VT present, quiet-testbed.md 21.4; not the laptop's own Steam install), mod
set **cp,vp,eui** (live_session's `frontend.py prepare --mods cp,vp,eui`, the harness default, MC #2508), the stock
route (the instance's own 32-bit DLL), the release Tiny save REL548-TINY_0008 (SHA-256 b2ceb663..., Polynesia, turn 9;
the same save as 4501's) shipped with the job, client 3825 x 2087 physical px in a 3840 x 2160 window at (0,0), UI
1530 x 835 (the game is DPI-unaware at 250 %). Every job ended with the game closed through its own menu (EXIT TO
WINDOWS, Yes; real clicks, or posted clicks in job a843 before real keys were allowed), then live_session's restore by
SHA-256 and `provision.py verify` 0. Records: `F:/c5-64bit-work/0392/4509/jobs/<job>/`.

**What the laptop's session is [L, jobs 20261116-080633-e9c4, -081423-730b, -082029-a843, 2026-11-16 08:09-08:24].**
The owner's session 1 is logged on, but `WTSQuerySessionInformation(WTSSessionInfoEx)` reads **SessionFlags 0 (locked)
with connect state 1 (WTSConnected, not WTSActive)**, while **no LogonUI.exe or LockApp.exe runs** in any session,
`OpenInputDesktop` opens **"Default"**, the foreground window is the taskbar (`Shell_TrayWnd`), the screen saver is
off (`SPI_GETSCREENSAVEACTIVE` 0) and nobody had touched the machine for 20 h (GetLastInputInfo). The power scheme
turns the display off after 600 s on mains (VIDEOIDLE); CONSOLELOCK is not reported by `powercfg /q`. A whole-screen
grab shows the desktop (taskbar, the game's window frame), not a lock screen. [I: a console session left connected
but not active, as after the display went off; the cause was not read.]

**The first step's answers [L, job 20261116-082029-a843 08:22 and -083021-f7c4 08:33]:**
- **Focus:** a plain `SetForegroundWindow` from the job (in the owner's session through the session helper) put the game in
  the foreground at the first try, both times; no key was needed.
- **Moves:** 10 of 10 (a843) and 20 of 20 (f7c4) real absolute moves arrived exactly: `UIManager:GetMousePos()`
  equalled client px x 1530/3825 to the pixel, cursor error 0.
- **Keys:** 3 of 3 real ESC pairs opened and closed the game menu (its `IsHidden` read true, false, true).
- **Pictures:** with the game in the foreground and its guard off, a whole-screen grab shows the game (mean 162);
  with the game in the background (the job's start, guard on) the client area is black in the grab and in PrintWindow
  (mean 0.0). So on the laptop the real lane has pictures; quiet-testbed.md 21.3's black pictures were a game in the
  background.
- **So a locked screen did not occur and the laptop stays logged on as it is.** Nothing was changed on the laptop.
  The lead accepted this (08:4x); no question to the MC.
- **The lock flag alone no longer refuses** (changed 2026-11-16 08:28, commit after a843; the lead's conditions met):
  the module first refused on SessionFlags = locked (job e9c4 08:09: "the session is locked: no real input", nothing
  sent). Since the flag is set while no lock screen exists and input arrives, the rule is now: refuse when a lock
  screen's UI is shown in the session (corrected 2026-11-24, ticket 0732: a LogonUI/LockApp top-level window that is
  visible and not DWM-cloaked; was "a LogonUI/LockApp process runs in the session", see the next point), when the flag
  is set and that cannot be ruled out (the process or window list unreadable), or when the input desktop is not
  "Default"; and as before when the game is not the foreground window or is minimised. A key can therefore never go to a lock screen's password box. Negative tests:
  `test_each_remaining_refusal_fires_with_nothing_sent` (5 states x key, click, move, type: exit 2, nothing sent) and
  `test_cli_act_prints_the_refusal_and_exits_2`.
- **A posted-lane fact on the laptop [L, a843 08:24-08:28]:** with the game in the foreground and its guard on
  (`enabled` 1), posted keys did nothing (`cli.py key ESC` did not open the game menu, `cli.py key ALT+F4` did not close
  the game), while posted clicks worked (the menu button, EXIT TO WINDOWS, Yes). [I: the cause was not read.]

- **A resident LockApp.exe is not a lock screen** `[L]` + `[I]` (2026-11-24, ticket 0732): at 22:53 the laptop showed
  LockApp.exe pid 2004 in session 1 with the input desktop "Default", SessionFlags "locked", WTSConnected, the game in
  front and a whole-screen grab without a lock screen (vc-20261124-225312-laptop-4180); `refusal()` refused ("the lock
  screen runs in this session"). The owner says the laptop has been unlocked all along since its display was set never to
  turn off. Windows keeps LockApp.exe resident and suspended after an unlock `[I]`, so its presence is not a witness.
- **The lock witness since 0732** `[C]` (2026-11-24, `tools/hi/realinput.py`): `_lock_ui()` = `lock_ui_verdict(procs,
  session_id, windows)` (pure): True only when LogonUI.exe or LockApp.exe **of this session** owns a top-level window
  (EnumWindows of the job's desktop) that is `IsWindowVisible` and not cloaked (`DwmGetWindowAttribute(DWMWA_CLOAKED
  = 14)` reads 0; a cloak that cannot be read counts as shown); False when no such process runs in the session or none
  of its windows is shown; None when the process list, the session id or the window list cannot be read, or the
  enumeration returned no window at all while a lock process runs. The other refusals are unchanged (input desktop not
  "Default", SessionFlags locked with lock_ui None, the game not the foreground window or minimised). A real Win+L
  switches the input desktop away from "Default" `[I]` (OpenInputDesktop is then refused or names "Winlogon"), so it is
  refused by the desktop check whatever the window witness reads. `session_facts()` (in `state`, `probe`, journey
  records) now also carries `lock_windows` (hwnd, pid, visible, cloaked, class, title of the lock processes' windows,
  and the count of all top-level windows) and `lock_ui`. **`realinput.py lock-state`** prints the desktop state and
  these witnesses with no game and no instance (read-only, exit 0; refused with 2 on the workstation). Hermetic tests:
  `test_hi_realinput.py` section 4 (verdict table, the reader with a fake user32/dwmapi through the real ctypes
  callback, `desktop_state()`+`refusal()` with the 22:53 state, the CLI). Live proof on the laptop: see 10.5.
- **The window reader against the real APIs** `[L]` (workstation, 2026-11-24 23:4x, read only: a scratch script that
  replaced `fence()` for the readers alone and removed `_send`; no input of any kind): no LockApp/LogonUI ran there, so
  `lock_ui_reading()` read `lock_processes []`, `lock_ui False`. `_lock_windows()` on the resident shell hosts
  (TextInputHost, SearchHost, StartMenuExperienceHost, ShellExperienceHost, ApplicationFrameHost) enumerated 672
  top-level windows and read TextInputHost's `Windows.UI.Core.CoreWindow` "Windows Input Experience" as **visible
  True, cloaked 2** (DWM_CLOAKED_SHELL): a resident UWP window is visible by IsWindowVisible and hidden only by its
  cloak, which is why the cloak, not visibility alone, is the witness. [I: LockApp's suspended CoreWindow is the same
  kind; its own reading on the laptop is in 10.5.]

### 10.3 The focus-and-window journey

`tools/hi/journeys/focus-window.jsonl` has a format of its own (CONTRACT 11): alt-tab, minimise, restore and window
changes are not contract actions, so the journey is a list of steps whose "do" items are contract actions (points as
client fractions) or lane ops (`focus`, `minimise`, `wait`, `hex`, `click_control`, `exit_wait`), each step with
expectations read from the window and the Lua channel (foreground, minimised, client size, menu open, a plot under the
centre). It is run by `realinput.py journey`, not by `hi.py replay`, and `check-trace` and the release stage skip
`focus-window*` (lead, 4506). While a journey runs, a **decoy window of our own** ("HI real-input decoy (ticket 4509)")
is shown and next in the alt-tab order, so that alt-tab and a minimised game hand the foreground to it, never to a
program of the machine's user, and from it the harness may bring the game back.

Runs [L, laptop, 2026-11-16]:

| Job | Steps passed | What stopped it | Found |
|---|---|---|---|
| f7c4 08:36 (no decoy) | 1-3 | 4: alt-tab, the game stayed in the foreground | no other window to switch to on that desktop: the decoy was added |
| 6362 08:5x | 1-6 | 7: Win+Down did not minimise | the game's window style 0x16C80000 has no minimise box (WS_CAPTION, WS_SYSMENU only), so Win+Down does nothing; minimise is now the taskbar's own window call (`ShowWindow(SW_MINIMIZE)`) |
| b4ff 09:0x | 1-8 | 9: edge scrolling | alt-tab to the decoy and back (client 3825 x 2087 kept), ESC after the switch, minimise (iconic, client 0 x 0, window at -80000) and restore (client as at the start) all held |
| b4ff, from 10 | 10-12 | 13: the first ESC after Alt+F4 did not open the menu | Alt+Enter twice: no change to the window (style, client); Alt+F4: the game stays |
| 62cf 09:1x (BindMouse 2) | 1-8 | 9: edge scrolling | `OptionsManager.GetBindMouseMode()` read 2 (set in UserSettings.ini before the launch, restored by SHA-256 after); still no scrolling |
| 62cf, from 10 | 10-13 | 14: the confirm's Yes | EXIT TO WINDOWS found by `HasMouseOver` (a column of real moves) and clicked; the visible Yes is not `/InGame/GameMenu/Yes` (that control has no mouse-over and its `ExitConfirm` is hidden) |

- **Edge scrolling did not happen** in any run: the cursor held 2.5-4 s at each of the four client edges (real
  moves, exact), BindMouse 0 or 2, the map zoomed out or zoomed in by 6 real wheel ticks (the wheel worked); the plot
  under the centre stayed (50,13). [I: the window's border keeps the clipped cursor at least 3 logical px from the
  screen's edge (client from physical x 8 to 3832 of 3840 at 250 %), and the game tests the screen's edge; or the
  camera is held to the small explored region of turn 9. Not settled; full screen would tell, see the next point.]
- **Alt+Enter and Alt+F4 are not handled on the harness's instance** [L]. [I: the game handles both in its global
  low-level keyboard hook, which the focus guard refuses at the launch while the game is in the background
  (focus_guard.cpp, `SetWindowsHookEx(LL)` refused: the guard counters of every launch show 1 call, 1 altered); turning
  the guard off later does not install it.] So **windowed to full screen and back cannot be tested by keys on a
  harness instance**; the in-game video options are blocked by the game ("may not be changed while in game", 4501
  4.8), which leaves the front end's options (not done here).
- After a (real) Alt+F4 the next ESC is taken without opening the game menu; a second ESC opens it (twice, b4ff and
  f7c4) [L]; the journey's step 13 sends ESC until the menu opens (at most 2).
- **The exit confirmation is not the game menu's own** [L, 9180 09:3x]: after a real click on EXIT TO WINDOWS the
  visible "This will close Civilization V..." box has a Yes that neither `/InGame/GameMenu/Yes` (no mouse-over) nor
  the judge's `under` (no candidate) names, and `/InGame/GameMenu/ExitConfirm` reads hidden in the InGame and the
  GameMenu states. The journey clicks that Yes centre-anchored: 14 UI px below the client's centre (scaled by client/UI
  px), which was (1912, 1078) on the laptop.
- **Focus after a foreground program of the machine** [L, 9180]: with explorer in the foreground, a plain
  `SetForegroundWindow` was refused and the `AttachThreadInput` form worked; from the decoy (our process) the plain call
  works.
- **Pictures on the real lane** [L]: the journey's step pictures are whole-screen grabs of the client rectangle
  (`--feed` puts them into the feed, DIAGNOSTIC, D112); the game in the foreground gives the player's frame (mean
  149-160, menu 40). The harness's PrintWindow on the laptop gives the 1530 x 835 back buffer in the top-left
  corner of a black 3825 x 2087 picture, in the foreground and in the background alike (its `not_black` value is
  about 0.85, under the 0.95 threshold, so checkpoints pass).

**The recorded run [L, job 20261116-095100-13a9, commit of 09:50, 2026-11-16 09:53:31-09:54:57 EST]: 14 of 14 steps
passed, exit 0**: start; ESC opens and closes the menu; alt-tab to the decoy and back (focus by a plain
`SetForegroundWindow`, client 3825 x 2087 kept); ESC after the switch; Win+Down (nothing), then minimise (iconic) and
restore (foreground, client as at the start); edge scroll recorded (plot under the centre (50,13) before and after);
Alt+Enter twice recorded (no change); Alt+F4 recorded (the game stays); ESC until the menu opens; EXIT TO WINDOWS (by
`HasMouseOver`), Yes (centre-anchored): the game ended by itself. Then the job's restore: the three settings files
equal by SHA-256, the save copy removed, `frontend.py restore`, `provision.py verify` 0. Pictures: 8 entries in the
Visual Checks feed under 4509, `vc-20261116-095334-laptop-c003` to `vc-20261116-095437-laptop-f897`, asset set
VT present (55 files, 45b3b433...) on `C:/c5-testbed/instances/tb1/install`, all DIAGNOSTIC. Record:
`F:/c5-64bit-work/0392/4509/jobs/13a9/out/live/journey-final/journey.json`.

**The install of these runs (lead, from the WMC's 0423 question; read only)** [L, job 13a9's logs]: the testbed's test
copy (`tb1`), not the laptop's Steam install; mods Community Patch v151, Vox Populi v17 and "(3a) VP - EUI
Compatibility Files" v1, the same modinfo versions as the workstation's 5.4.8 MODS, with EUI's UI loaded (EUI tech tree
98.106, top panel 98.109 in Lua.log); **the instance's DLL is a project i0-ship build of bbfacca1** ("Build Nov 11 2026
02:45:43" in Lua.log), neither the 5.4.8 release DLL nor the current pin 26234371. Reported to the lead.

### 10.4 The equivalence check: one trace, both lanes, one machine

[L, job 20261116-093951-d90b, 2026-11-16 09:42-09:47 EST, laptop, the setting of 10.2.]
- **The trace** is an ordinary hi trace (`hi.py session start ... step ... checkpoint`), recorded on the laptop on the
  **posted lane** with the game in the background behind the decoy (job op `decoy show`): 6 steps and 5 checkpoints,
  `F:/c5-64bit-work/0392/4509/jobs/d90b/out/live/rec1/trace.jsonl`: checkpoint `start` (turn, screens); `F6` with
  `screen_open /InGame/TechTree`; checkpoint `techtree`; `ESC` (`screen_closed`); `ESC` (`screen_open
  /InGame/GameMenu`); checkpoint `menu`; a click on RETURN TO GAME, recorded as the control
  `/InGame/GameMenu/ReturnButton`, with `screen_closed`; checkpoint `back`; wheel +3 and -3 at the centre (plot
  hits); checkpoint `end`. Checks `not_flat,not_black`. No turn ends, so the game's state at the end is the start's
  (turn 9, no screen) and the replays run back to back in the same game.
- **The replays**, two pairs: posted lane (`realinput.py replay --lane posted`: `replay.run` unchanged, the game in
  the background behind the decoy), then real lane (`--lane real`: `replay.run` inside `real_lane()`, the game in the
  foreground by a plain `SetForegroundWindow`; every action delivered with `"lane": "real"`, the judge's hover
  searches by real moves).
- **Result: the same, 2 of 2.** Each of the four replays: exit 0, 6 of 6 steps, 5 of 5 checkpoints ok; the judge's
  expected fields at each checkpoint equal on both lanes (`realinput.equivalence`): `start` {turn 9, no screen},
  `techtree` {TechTree}, `menu` {GameMenu}, `back` and `end` {turn 9, no screen}. The ReturnButton hit re-resolved to
  the same client point (1912, 1784) on both lanes; the wheel's plot hits re-resolved a few px apart (the camera).
  Picture checks: every checkpoint `not_flat` and `not_black` ok on both lanes (values 0.84-0.93).
- **The posted lane in the background works on the laptop** (the same posted ESC that did nothing with the game in
  the foreground, 10.2): posted keys open and close the menu once the decoy has the foreground [L, 9180 09:31].
- The ten real-lane checkpoint entries of the feed (`vc-20261116-094519-laptop-94d3` ... `-094646-laptop-450e`) were
  amended to "HI real-input lane": `replay.py` labels every replay checkpoint "HI posted lane" (a note for W3a: the
  label does not know the lane).
- Not covered by this trace, **not verified**: world-view actions (a left click selecting a unit, a camera drag, a
  right-click move order with its checkpoint), typed text, double-clicks, AI turns. The lead asked for the world-view
  actions if one more laptop job of at most 20 min allowed it (10:0x): it does not, because a move order changes the
  game, so each replay needs its own fresh load (a job of about 4-5 min each: recording plus 2 x 2 replays is 5 jobs).
  The trace was kept to actions that leave the game's state as it was.
- **These results are about the input lane, not the DLL**: every laptop run of W6 used tb1's DLL, the i0-ship build of
  bbfacca1 (`bbfacca1-ia`, "Build Nov 11 2026 02:45:43"), not the pin 26234371 that COMMON 18 asks tests to use; the
  testbed's copy is shared and was not changed (lead, 10:0x; reported to the WMC by the lead).

### 10.5 Not verified / open

- Edge scrolling: never seen on the laptop in windowed mode (10.3); full screen could not be reached by keys on a
  harness instance (Alt+Enter is the game's low-level hook, refused by the guard at the launch). A windowed-to-full-
  screen test needs the front end's video options (no channel there; positions by picture), not done.
- A resolution change from the options (in game the video options are blocked by the game; the front end's are not):
  not done.
- What a locked screen does: no lock screen occurred on the laptop (10.2); the module refuses on a lock screen's UI by
  rule, which the hermetic tests prove, but a live lock was not produced (it would need the owner to lock the machine;
  still true after 0732, 2026-11-24).
- **0732's live proof on the laptop** `[L]` (2026-11-24 23:41:49, job 20261124-234104-a00a, headed, slots 1, about 1 s,
  `realinput.py lock-state` at branch task-0732-realinput-lockapp, no game, no input sent; window granted by MC-B, desk
  0136 #13495): input desktop "Default", SessionFlags 0 (locked) with connect state 1 (WTSConnected), LockApp.exe pid
  2004 resident in session 1 (the same pid as at 22:53) **with no top-level window at all** among the 184 that
  EnumWindows returned on the job's desktop, so `lock_ui` False; foreground `Shell_TrayWnd` (explorer), screen saver
  not running, idle 480 s; `refusal()` with the game assumed in front: none. So the fixed check reads the laptop as
  unlocked, as the owner says it is. **Not verified:** a real lock read live as locked (needs the owner to lock the
  laptop once; not asked in 0732's window); `realinput.py -i <inst> state` with a game (lock-state reads the same
  witnesses without one); 5042's SendInput series (its own follow-up).
- The capability word `real-input` (D134) for the laptop: prepared as its own commit for the MC's review (the queue's
  vocabulary, the seed, and a refusal of the word for the workstation); the live store is changed only by the MC
  (`ticketq.py capabilities --add laptop=real-input --by MC`).

## 11. Not verified

The proposal's section 10, until each item is settled here.

## 12. Coverage read from the game (W7)

Ticket 4510, impl-4510 (account B), 2026-11-16 07:30-09:30 EST. Design: the Architect's addition
`research/07-architecture-decisions/architect-2026-11-16-human-input-coverage-addition.md` sections 3 and 6 (D159).
Code: `tools/hi/coverage.py`; hermetic tests `tools/harness/tests/test_hi_coverage.py`. Scratch scripts are kept in
`F:/c5-64bit-work/0392/4510/scripts/` (not committed). Run folders: live `F:/c5-64bit-work/0392/4510/live/`, offline
`F:/c5-64bit-work/0392/4510/offline/`. Each holds:
- `coverage.md`, the report page;
- `coverage.json`, every entry with its status and reason;
- `coverage/`, with the candidate table, the inventories, the lists and the copied call tables.

None of it is committed (D83).

### 12.1 The three measures, as built `[C]`

| Measure | Total, read by the run | Covered when | Witness |
|---|---|---|---|
| UI inventory | every control of W2's candidate table (`uimap.candidates`, the install's and mods' UI XML at run time) | hovered, left-clicked and right-clicked in the run's traces | the trace's `control` hits (`hover`; left `click`; right `click`) |
| Game actions | 12 lists, read through a fixed read-only Lua chunk (`LISTS_CHUNK`): popup types, notifications, missions, interface modes, unit commands, diplomacy events, diplomacy screen states, UI controls, city tasks, trade items, end-turn blocking types and worker builds; also the hotkeys `GameInfo` binds to missions, interface modes, commands, controls and builds | exercised once by input | a step's delivered input, with the judge's state before (the previous step's) and after it (12.3) |
| Boundary calls | every method of the proxy's call table: `proxy/model/interfaces.json` and `policy-table.tsv`, generated from the DLL source at the pin's commit 04b1211b (1,615 methods, every one matched to a policy; *0602, 2026-11-20: the model is rewritten at every pin move, even when `regen_model.py` reports it EQUAL, so its `source.commit` follows `deps/dll.lock`; now 1fd51353e, pin batch 5, still 1,615 methods. Procedure: `new-vp-release-checklist.md` step 10*) | the call table of the run's process counts at least one call | the server's `proxy-calls.tsv`, or the proxy's `vpproxy-calls.tsv` when the server died (12.4) |

- **An entry that is not covered keeps a reason**, so a report always accounts for the whole total.
  - **UI.** `partial`: touched, but not with every mark. `excluded`, with one of three reasons:
    - refused by W2's deny list (`deny.DENY_PATHS`);
    - unreachable: the cursor-following tooltip contexts `PlotHelpManager` and `PlotHelpText`;
    - not in the running UI: the XML names the control, but `LookUpControl` found nothing in any inventory.

    `open`, also with one of three reasons:
    - hidden whenever its screen was inventoried;
    - its screen was open, but the control was not exercised;
    - its screen was never seen open.
  - **Game actions.** `open`, either "not reached by input" (for the lists the state witnesses) or "no witness: <the
    judge read that would give one>" (12.3).
  - **Boundary.** `excluded` for the 308 methods whose policy is `unused` ("the DLL never calls it"); `open` for any
    other method that was not called, with its policy named. A run with no call table is "not measured", never zero.
- **The real-input reason** is supported by the code (`real_input` in `measure_ui`), but no entry carries it yet.
- **The API for W8 (4511):**
  - `screen_controls(inst, screen)` (W2's `uimap.candidates`);
  - `live_inventory(judge, table)`: the open contexts and, for each, every control's own flag and its effective state
    (shown, and every named XML ancestor in the same context shown), plus `parent_open` for nested contexts;
  - `read_lists(judge)`;
  - `measure(...)` and `write_report(...)`;
  - the CLI:
    - `coverage.py -i NAME inventory|lists --out RUN`;
    - `coverage.py calls VP64_RUN_FOLDER --out RUN`;
    - `coverage.py report RUN --trace T ... [--inventory F] [--lists F] [--calls F] [--table F]`.
  - Exit codes: 0; 1; 2 (a pure session holds the instance); 4 (the judge cannot read); 8 (a box, checked before any
    read).
- **The lists chunk is under the contract 6.2 proof** (lead-0392, 2026-11-16).
  - It stays in `coverage.py`, and `_run_chunk` passes it through `judge_lua.checked` before every send.
  - The tests scan it and run it in lupa against fake enum and `GameInfo` tables.
  - They also plant six writes and check that each is refused before anything is sent: a `Set` call, an enum
    assignment, `Events.`, `UI.Set`, `Network.Send` and a global assignment.
  - The inventory itself uses only W2's `contexts` chunk, in batches of 600 paths: no input, no move.

### 12.2 Live totals on one Tiny save, product route `[L]`

**Set-up (2026-11-16 08:06-08:25 EST, c5h-c, C:, claimed `--ticket 0392 --address impl-4510`).**
- **Product prep:** run_3308's prep through scratch `pp.py`.
  - Pins `x64-04b1211b-pfdee656`: proxy 75C40CEC..., server 5B254430..., x64 DLL 29F58E17....
  - `shell_composite.dll` 802977AF..., built by `shell\build.cmd` from `git archive` of fdee65668. Again not byte-equal
    to the earlier builds of that commit.
  - 3308's composite keys, and the D33 arm with Overlay on.
- **Mods:** `cp,vp`, with no EUI compatibility mod, as in 4504's traces. EUI itself is the install's DLC `UI_bc1`, which
  loads whatever the mod set.
- **Settings:** client 1914x1051 (D158) and `SinglePlayerAutoEndTurnEnabled = 0`, both written before the launch,
  backed up by SHA-256 and restored.
- **Save:** `HI4510-TINY_0008`, a copy of `C5H-3600-TINY_0008` (b2ceb663..., the Tiny Polynesia save of 4501, 4503 and
  4505, turn 8), auto-loaded by `frontend.py prepare --load`.

| Measure | Total read live | Notes |
|---|---|---|
| UI inventory | **4,475 controls in 122 contexts** (build key a908b01dfcc5) | the same totals as W2 counted on c5h-d with EUI's compatibility mod active: the table reads every MODS folder, active or not (12.6) |
| Game actions | **620 entries in 12 lists** (below); 138 hotkeys | NO_* and NUM_* bounds are left out (14 names); 12.2 compares with the Architect's rough count |
| Boundary calls | **1,615 methods** (308 `unused`); the first live process reached **494**, by the proxy's own counters | the server of that process died, so its table is the proxy's (12.4) |

The 620 game-action entries by list:

| List | Entries |
|---|---|
| popups | 117 |
| notifications | 152 |
| missions | 60 |
| interface modes | 25 |
| unit commands | 10 |
| diplomacy events | 41 |
| diplomacy states | 44 |
| UI controls | 48 |
| city tasks | 23 |
| trade items | 18 |
| end-turn blocking | 28 |
| worker builds | 54 |

The Architect's rough count from `CvEnums.h` (105 popups, 143 notifications, 26 interface modes, 11 commands) is near
these but not equal. VP adds popups, and the notification list comes from the database.

- **The lists are read in the `InGame` state.** The `Main` state has no `ButtonPopupTypes`: the first read there
  returned 11 lists and named that one missing. `InGame` has every enum table and `GameInfo` (`LISTS_STATE`).
- **Inventories while each screen was open** (`F:/c5-64bit-work/0392/4510/live/coverage/inventory-00N.json`):

| Screen open | Contexts open | Their named controls | Shown | Not in the running UI | Instance templates |
|---|---|---|---|---|---|
| world view | 22 | 338 | 141 | 29 | 106 |
| Civilopedia (a right click on the top panel's gold) | 23 | 616 | 177 | 33 | 148 |
| economic overview (a left click on the gold) | 23 | 347 | 148 | 29 | 106 |
| tech tree (F6) | 23 | 342 | 146 | 29 | 109 |
| game menu (ESC) | 23 | 383 | 167 | 29 | 108 |
| city view (the end-turn button: choose production) | 19 | 480 | 258 | 44 | 108 |

  Some single contexts:

  | Context | Named controls | Shown | Not in the running UI | Instance templates |
  |---|---|---|---|---|
  | Civilopedia | 278 | 41 | - | 42 |
  | city view | 158 | 90 | 15 | 8 |
  | tech tree | 4 | - | - | 3 (its buttons are all instanced, as W2 found) |
  | game menu | 45 | 25 | - | - |
  | top panel | 59 | 40 | 4 | - |
  | notification panel | 31 | - | - | 76 |

  **How much of a screen the inventory sees:** everything that has a name. Instanced items (tech buttons, list rows,
  notifications, unit actions) are reached only by `pixel` hits. They are counted per screen as templates, not as
  items: how many items a template makes is not readable without counting them in a picture `[I]`.
- **Nested contexts open under a hidden parent.** At the world view, `/InGame/EconomicOverview/EconomicGeneralInfo`
  and `/InGame/DiploOverview/RelationsPanel` report their own flag shown while their parents are hidden. W2 met the same
  flags with LoadMenu under a hidden GameMenu, but here the screen is not really open. `live_inventory` records
  `parent_open` from 4510's last commit on, so a report can tell the two cases apart; the six inventories above
  predate it.
- **The live trace** (`live/hi/trace.jsonl`, 13 steps; report `live/coverage.md`):
  - UI: 1 control covered: `/InGame/TopPanel/GoldPerTurn`, hovered, right-clicked (the Civilopedia's Gold page) and
    left-clicked (the economic overview). The end-turn button is partial.
  - Game actions: 6 covered:
    - BUTTONPOPUP_ECONOMIC_OVERVIEW and BUTTONPOPUP_CHOOSEPRODUCTION (their screens opened);
    - INTERFACEMODE_SELECTION;
    - CONTROL_FORCEENDTURN (SHIFT+ENTER, turn 8 -> 9);
    - CONTROL_TECH_CHOOSER (F6);
    - ENDTURN_BLOCKING_PRODUCTION (cleared by the production choice).
  - Boundary: 494 of 1,615.

### 12.3 Witnesses for game actions `[C]`, and the reads still missing

An entry counts only when the step delivered an input.

- **Interface modes:** the state's `mode_name` after the step.
- **End-turn blocking types:** a `block_name` that was seen, then gone after a later step's input.
- **Popups:** a context that is newly open, and whose UI Lua compares a popup's type with exactly one `BUTTONPOPUP_*`.
  - The Lua is found by file name in W2's XML resolution order (`lua_index`). A context's Lua is often not beside its
    XML: EconomicOverview.xml is in Expansion2, and its Lua elsewhere.
  - GenericPopup also takes the `PopupLayouts[...]` of every `PopupsGeneric\*Popup.lua` it includes (17 types).
  - A context that shows several types counts as "seen, not identified" (the tech tree shows 3).
  - On c5h-c, 49 contexts name 72 types. 51 of the 117 types are named by no in-game context's Lua (for example
    BUTTONPOPUP_MENU: the game menu opens without one). They stay open with that reason `[I]`.
- **Missions:**
  - MISSION_MOVE_TO: the same unit's plot changed after the input.
  - MISSION_FOUND: a settler was selected, then it is gone and a city screen is up `[I]`.
- **The end-turn button:** a left click on `/InGame/WorldView/ActionInfoPanel/EndTurnButton`, after which the turn
  advanced (CONTROL_ENDTURN).
- **Hotkeys:** a key that matches exactly one entry's `GameInfo` hotkey, after which a visible field of the state
  changed.
  - Unit lists count only when a unit was selected before the key.
  - A key shared by several entries is never guessed:
    - B is MISSION_FOUND, MISSION_RANGE_ATTACK and INTERFACEMODE_RANGE_ATTACK;
    - F is MISSION_SLEEP, MISSION_FORTIFY, MISSION_GARRISON and a build.

**The judge read that would witness the rest, per list** (`NEEDS`; for 4511 or a later W2 change):

| List | Missing read |
|---|---|
| notifications | the active player's notification list before and after the input (`GetNumNotifications`; type and dismissed flag per index) |
| missions | the selected unit's activity and mission-queue head, moves left and plot; the city count, for founding |
| unit commands | automation flag, level or promotion count, unit type (upgrade), the unit gone (delete), owner (gift) |
| diplomacy events | war state per team, the leader screen's DiploUIState, the deal list |
| diplomacy states | the leader screen's current DiploUIState (kept in LeaderHeadRoot's own Lua: a read in that state) |
| UI controls | a handler map (which control calls `Game.DoControl(CONTROL_X)`) and each control's effect |
| city tasks | the selected city's production queue head, razing flag, ranged-strike state |
| trade items | the DiploTrade screen's deal items (`UI.GetScratchDeal()`, read only) |
| worker builds | the unit's build type after the order, and the plot's build progress |
| popups shown by a multi-type context | the popupInfo the context was shown with (held in that context's Lua) |

**Hotkeys a sweeper must keep out** `[C, GameInfo read live]`. These are input hazards for W8, and W2's deny list
refuses only ALT+ENTER today:

| Key | What it does |
|---|---|
| CTRL+SHIFT+R | CONTROL_TOGGLE_RESTART_GAME |
| CTRL+SHIFT+O | observer mode |
| CTRL+SHIFT+L | AI takeover |
| CTRL+SHIFT+P | switch player |
| ALT+Q | retire |
| CTRL+L, CTRL+F11 | load |
| F11 | quick save |
| CTRL+S, CTRL+ALT+S | save |

### 12.4 Boundary counts: per process, and which file `[L]`

- **When the server writes `proxy-calls.tsv`.** The server writes the file (only methods with calls > 0) into its run
  folder `Logs/VP64/run/<id>/` whenever it ends by itself, also when the EXE died.
  - On 2026-11-16 at 08:37:39 the EXE crashed with 0xC0000005 (12.5), and the server still wrote the table on its exit
    3 ("the engine is gone").
  - 4504's and 4505's processes, stopped by `cli.py stop`, have it too.
- **When the server itself dies, it writes none.** The proxy then writes `vpproxy-calls.tsv` at its own exit, with
  columns `method`, `direction` (out, in or local) and `calls`. `coverage.py calls` copies that file instead and marks
  it "proxy side". Both files use the model's method keys.
- **The counts are per process, not per trace.**
  - 4504's c5h-a process 20261116-035431 holds rec1, rec2, rec3 and the replays from 03:54 to 05:32. A trace's
    boundary share is that of the process it ran in.
  - Runtime protocol rows (`kRt_*`, 17 in a table) are outside the model and are listed apart.
- **No `cli.py stop` after a main-menu exit was tried** (0411's path). The server writes its table after an EXE death,
  so this measure does not need a clean exit.

### 12.5 Live observations on the way `[L]`

**A hover over plot 51,13 ends the game: stock too, but each route ends differently.**
- **Product, 2 of 2** (08:24:44 and 08:46:59). The first came after the judge's `plot-pixel` move. The second came from
  a deliberate 3 s hover step at client (1077,674).
- **What stops on product.** The server's x64 DLL stops on `PRECONDITION(eImprovementNum > -1)` in
  `CvGlobals::getImprovementInfo` (CvGlobals.cpp:4280).
  - It is called by `CvLuaPlot::lIsResourceConnectedByImprovement`, a UI Lua call through the proxy (`rt:LuaCall`).
  - Symbolised with the layout's PDB. crashes.log says "Deliberate stop", 0xc000001d at
    CvGameCore_Expansion2.dll+0x78d6be.
  - The likely caller `[I]` is EUI's `PlotHelpManager.lua:624`,
    `plot:IsResourceConnectedByImprovement( revealedImprovementID )`, which passes -1 on an unimproved plot.
- **How product ends.** The proxy then ends the EXE with exit code 3 (`on_server_death=exit`). There is no box and no
  message: the game window simply goes.
- **Stock, 1 of 1** (08:53:15): the pin's 32-bit DLL `i0ship-26234371-ia`, with the same save, mods and hover.
  - The DLL shows its own fatal "Error" box, `CvGlobals.cpp:4244 eImprovementNum > -1`.
  - The box was closed under COMMON 29 (`dismiss --fatal`, then `stop --after-fatal`).
- **What is patch-only.** The fault is the DLL's, on both routes. What is patch-only is that the product route gives
  the player no error message. That seems to go against D118 (an error popup and a report) `[I]`.
- **A release set-up does not meet it** (corrected 2026-11-16 09:23 from the MC's #2508, after 0423; replaced "whether
  a release-candidate set-up meets it is not checked `[I]`"). It needs EUI's PlotHelpManager without VP's EUI
  compatibility files, which the hand-picked `cp,vp` set gives because EUI sits in the install's DLC; no VP 5.4.8
  player has that set. HI journeys, explorers and findings use the harness default `cp,vp,eui` (the installer's
  FullEUI set) unless a charter says otherwise, and a finding names its mod set. The DLL guard (0421) and the popup for
  the DLL's fatal stop (0422) stay open: both are right either way.
- **Records.**
  - Findings: `F:/c5-64bit-work/0392/4510/finding1/hi` (product, `wrong_state-step0`) and `finding1-stock/hi` (stock,
    kind `box`).
  - Evidence: `crash-082444/`, `crash-084659/` and `crash-085315-stock/`.
  - Reported to lead-0392 in the same turn.

**A silent EXE death during a load, product route** (08:37:39, the second launch, the same set-up as above).
- The EXE exited with 0xC0000005 about 122 s after launch.
- There was no box and no crash log: the install's `crashlogs` folder was unchanged.
- `vpproxy.log` stops at 77.5 s, just after the first `ICvGame1::Update`. Lua.log ends with EUI's "Finished loading EUI
  top panel".
- Evidence: `F:/c5-64bit-work/0392/4510/crash-083739/`.
- The third launch, unchanged, loaded.
- Compare 4505's stock load crash of 05:15:54 (7.6): the same D33 arm and composite keys, but `mode=off`, and an access
  violation in the EXE with a fatal box. They are not one pattern (corrected 2026-11-18, lead-0398; was "open `[I]`"):
  05:15:54 is the River/Arrows NULL batch at VA 0x8CD492 (7.6), this one faulted at VA 0x8E81C3
  (`crash-ledger-audit.md` 4.1, `research/02-exe-internals/crash-load-8e81c3.md`).

**EUI without its VP compatibility mod.** It logs `TopPanel.lua:976: attempt to call method
'GetYieldPerTurnFromVassals' (a nil value)` on every top-panel tooltip frame (Lua.log). This is noise in any `cp,vp`
session, and a hint that `cp,vp` with the install's EUI is not a supported mod set `[I]`.

### 12.6 The coverage of 4504's and 4505's recorded traces (rec3, rec4, the 10-turn trace) `[C]`

The report is `F:/c5-64bit-work/0392/4510/offline/coverage.md`.

**Inputs.**
- The candidate table is c5h-a's (03:58), the instance rec3 and rec4 were recorded on.
- The lists and the world-view inventory are this ticket's live reads on the same pins. They stand in for the
  recordings' own, and the page names them as sources.
- The call tables are the three processes' `proxy-calls.tsv`: c5h-a 035431 (rec3), c5h-a 063741 (rec4) and c5h-f
  044309 (rec10b).

**Results.**
- **UI: 0 of 4,475 covered.**
  - Four controls are touched, left only: the end-turn button, ReturnToMap, MainMenuButton and the game menu's Yes.
  - 54 are excluded: 24 by the deny list, 3 tooltip contexts, and 27 not in the running UI.
  - 4,417 are open.
  - Pixel hits: 8 in the front end, 2 in the city view, 2 in the tech tree.
- **Game actions: 9 of 620 covered:**
  - BUTTONPOPUP_CHOOSEPRODUCTION and BUTTONPOPUP_TECH_AWARD;
  - MISSION_FOUND (the settler rule) and MISSION_SKIP (SPACE);
  - INTERFACEMODE_SELECTION;
  - CONTROL_ENDTURN (the button) and CONTROL_FORCEENDTURN (SHIFT+ENTER);
  - ENDTURN_BLOCKING_RESEARCH and ENDTURN_BLOCKING_UNITS.
- **Boundary: 565 of 1,615 covered;** 308 excluded as unused.

**The candidate table is not the running UI's.** W2's table reads every MODS folder whether its mod is active or not,
so 29 to 44 named controls are "not in the running UI" in each inventory. The inventory, not the table, says what a
screen has.

### 12.7 Open items (lead-0392, 2026-11-16: to 4511's scope, not fixed here)

1. **A step whose action ends the process is missing from the trace.** W1 writes the step record after the step's
   picture. The hover step of 08:46:59 is absent from `F:/c5-64bit-work/0392/4510/finding1/hi/trace.jsonl`, which
   holds only the header. The finding therefore has no step to cut at and no replay to reproduce it.
2. **The contract has no finding kind for "the process ended without a box".** The product finding was recorded as
   `wrong_state` (`finding1/hi`). On product such an end is the proxy's exit 3 after the server died, or a silent EXE
   death (12.5).
3. Instanced items are counted per screen as templates, not as items (12.2).
4. The game-action witnesses beyond the short state (12.3 table).

## 13. The sweeper (W8)

Ticket 4511, impl-4511 (account B), 2026-11-16 10:16- EST. Design: the Architect's addition sections 4 and 6 (D159).
Code: `tools/hi/sweep.py`; contract v1.10 changes in `tools/hi/trace.py`, `tools/hi/session.py`, `tools/hi/report.py`.
Hermetic tests: `tools/harness/tests/test_hi_sweep.py` (the sweeper, on a fake game) and
`tools/harness/tests/test_hi_sweep_trace.py` (the write-ahead record and the finding kind). Scratch scripts are kept
in `F:/c5-64bit-work/0392/4511/scripts/` (not committed); run folders in `F:/c5-64bit-work/0392/4511/`.

### 13.1 Two harness gaps closed first (contract v1.10) `[C]`

These are 12.7's items 1 and 2.
- **The step that ends the process is in the trace now.** 4510's hover step was missing for two reasons. The step
  record was written only after the step's picture. And the picture's `capture.shot` raised `ActionError` ("not
  running") out of `session.step`, so neither the record nor `session.json` was written.
  - The fix has two parts. `session.step` now writes an `intent` record per action (`trace.intent`), after the
    action's hit is resolved and before its input. A wait or picture that meets a gone game is caught: the step is
    written with exit 1 and `error`.
  - An intent with no step record after it (the CLI itself killed) is warned by `check-trace` (`dangling_intent`).
    `trace.close_dangling(path)` appends the step built from it, marked `ended: process_ended`, with the action `ok`,
    so a cut trace and a replay deliver it again.
  - Readers that want steps skip intents. Tested: `replay` on a trace with intents and a closed step; `cut_trace` with
    intents; coverage's UI measure unchanged.
  - Three older tests indexed the trace by position and now drop intents first.
- **The finding kind `process_ended`** (`report.finding_process_ended`). It records the process ending with no box.
  - Signature: `process_ended-step<N>-<the control path, the plot or the key being delivered>`.
  - It carries the exit code, the mod set (MC #2508) and the stock control's result beside it.
  - `report.process_end_class` turns the stock control into a class: stock `passes` is `patch only`; stock `box` or
    `process_ended` is `stock too`.
  - It reproduces when the replay's process ended again with no box. The caller looks after the replay and sets
    `process_ended`; the stop's step must match when the replay names one.
- **Found on the way: no cut trace could be replayed.** `report.cut_trace` wrote the finding without `ms` and the end
  record without `exit` and `steps`. `replay.replay` runs `check-trace` first and refused every cut trace (exit 1), so
  the reproduction filter of contract 10 could never have run live. 4505's dry run used fake replays and did not meet
  it. Fixed in `cut_trace` (4508 met the same and keeps a temporary repair in its campaign script until this lands).

### 13.2 The sweeper, as built `[C]`

    python -I tools/hi/sweep.py -i NAME --by ADDR run --ticket ID --route product|stock --save SAVE [--launch]
                                [--screens a,b] [--max-controls N] [--grid PX] [--resume] [--out DIR]
    python -I tools/hi/sweep.py compare PRODUCT_DIR STOCK_DIR      # the stock control of every product end or box
    python -I tools/hi/sweep.py report DIR [--calls F]             # W7's coverage report over the sweep's traces

- **The plan** (`DEFAULT_PLAN`, 4511's "Done when") has five screens:
  - the top panel, which needs no opening input;
  - the unit panel, opened by a click on the first unit's plot (`plot-pixel`), waited on with `selected_unit`;
  - the city screen, opened by a click on the first city's plot, waited on with `screen_open CityView`;
  - the tech tree, opened with F6;
  - the game menu, opened with ESC.

  Each screen names its contexts and the part of the client to scan.
- **Per screen:**
  1. A fresh segment: the save is loaded again in game (the load menu's own `Events.PlayerChoseToLoadGame`, set-up
     outside a session, contract 1.4), or the game is launched again when it is gone. Then the scene set-up: auto unit
     cycle off, every unit asleep, nothing selected. Then a new session starts, so every trace replays from "save
     loaded, set up".
  2. The opening input runs as recorded steps.
  3. W7's inventory (`coverage.cmd_inventory`) gives the shown controls.
  4. A grid scan finds where each control is. At every grid point (24 px by default), the harness moves the cursor and
     asks `HasMouseOver` of the screen's shown candidates, deepest first. A control's point is its own grid point
     nearest the middle of all its points, so a click there hits it and not a child. The scan is read-only apart
     from the cursor; it is outside the trace and listed in `sweep.json` (`located`, `not_located`).
  5. Per control, ordered top to bottom with closing names (close, return, exit, cancel, back) last: a hover of 800 ms
     (the tooltip), a right click and a left click. Each is one step, with its picture checked by `not_flat` and
     `not_black`, and its judge state compared with the screen's starting state. The compared fields are screens,
     popup, city screen, mode, turn, unit, city, gold and research.
  6. When an action changed the state, the sweeper restores it. A screen or popup opened on top: ESC, then the state
     is checked again. The screen itself closed: its opening input again. Anything else: a reload of the save.
- **Safety:**
  - Every click is checked by W2's deny list (inside the step) and by the sweeper's own `NEVER` list:
    - exit to Windows, exit to the main menu, retire, restart, quick save, the exit and retire confirmation, the mod
      browser;
    - anything inside the in-game options, save and load menus;
    - any control whose name says it saves, loads or deletes.
  - A hover is never refused.
  - A box stops the sweep with exit 8. The box is never pressed, and the control is recorded with outcome `box`.
    After the owner closes it (COMMON 29), `--resume` goes on.
  - A process that ends with no box is a `process_ended` finding. The game is launched again through the harness
    (`frontend.py prepare` with the mod set and the save, `cli.py launch`, `ensure_in_game`), and the sweep goes on
    from the next control.
  - A file `<out>/STOP` ends the sweep before its next action.

### 13.3 One full sweep of one situation, product route `[L]`

**Set-up (2026-11-16 10:45-12:05 EST, impl-4511, account B).**
- Instance: c5h-b (C:, Goldberg), claimed `--ticket 0392 --address impl-4511`, short.
- Client 1914x1051 (WindowRes 1920x1080, D158) and `SinglePlayerAutoEndTurnEnabled = 0`. Both were written before the
  launch, backed up by SHA-256 (GraphicsSettingsDX11.ini f1d9cf01..., UserSettings.ini 5ffa161d...) and restored.
- Frame cap 30 fps.
- Product route by run_3308's prep (scratch `pp.py`):
  - pins `x64-04b1211b-pfdee656`: proxy 75C40CEC..., server 5B254430..., x64 DLL 29F58E17...;
  - `shell_composite.dll` FA1C83C3..., built by `shell\build.cmd` from an archive of fdee65668 (again not byte-equal to
    the earlier builds of that commit);
  - 3308's composite keys, and the D33 arm with Overlay on.
- Mods `cp,vp,eui` (MC #2508).
- Save `HI4511-TINY_0008`, a copy of c5h-a's `C5H-0167-TINY_0008` (b2ceb663..., Tiny, Polynesia, turn 8; a copy is on
  F:).
- `sweep.py --launch`: `frontend.py prepare --mods cp,vp,eui --load`, `cli.py launch`, `ensure_in_game`. Launch to in
  game took about 2 min.
- Record: `F:/c5-64bit-work/0392/4511/product/` (`sweep.json`, `sweep.md`, `s01`-`s12`, `coverage.md`).

**Result: 5 screens, 60 controls, 180 actions, no finding, no box, the process never ended.**

| Screen | Shown controls | Located by the scan | Scan | Actions (hover, right, left) | Restores |
|---|---|---|---|---|---|
| top panel | 39 | 16 | 144 s (grid 16x8 px) | 48, all ok | 19 ESC (Civilopedia on right click, overviews on left click) |
| unit panel (the warrior) | 38 | 4 | 287 s (24 px) | 12, all ok | 1 ESC, 2 reloads |
| city screen | 96 | 28 | 556 s (32 px) | 84, all ok | 1 ESC, 1 reload |
| tech tree | 4 | 2 | 223 s (48 px) | 6, all ok | 1 reopened (CloseButton) |
| game menu | 25 | 10 | 362 s (32x24 px) | 16 run, 14 refused by the never-list | 2 reopened (Return, Details) |

- 12 sessions, 197 steps, each with its intent record (197 intents). Step exits: 196 times 0, once 5: the city screen's
  first opening by a click (below).
- Hits: control 141, pixel 25, plot 5; keys have none.
- Picture checks on every step picture: `not_flat` and `not_black` 166 of 166 ok (composite dumps).
- Median wall time per step: hover 12.5 s, click 6.4 s (each with resolve, input, a 1 s still-picture wait, the
  picture, the judge state and the open contexts). 10 reloads.
- **Coverage** (W7's report over the 12 traces, the inventories, the lists and the process's call table
  20261116-105124-51880-1):
  - UI 45 of 4,475 covered, 6 partial. 4510's live trace had 1.
  - Game actions 9 of 620.
  - Boundary 452 of 1,615 (308 unused excluded).
- **Located against shown.** Located means a control a click reaches (it reports `HasMouseOver` at some grid point
  and nothing deeper does). Two kinds are not located:
  - controls with no hit area: bars, shadows, boxes, stacks and labels inside buttons;
  - controls smaller than the grid.

  The top panel's 23 not located are its bars, shadows, turn counters, the two info stacks and the panel's bar
  images.

**Found on the way `[L]`:**
- **The Civilopedia is not in the judge's `screens`** (4507 met it in a picture). A right click on any top-panel string
  opens it at that concept's page.
  - On the trial sweep (`try1`, 3 controls) the sweeper therefore went on clicking under the Civilopedia.
  - The sweep now also compares the open contexts (the judge's `contexts` read over the candidate table's 122
    contexts), and closes such a screen with ESC.
  - A hover's tooltip context that goes when the cursor leaves is read again off the control (`transient`) and not
    restored.
- **A click on the city's plot does not open the city screen.** The step's `screen_open CityView` wait reached its
  15 s deadline. The click hit plot [51,14], the city's own plot, and nothing was selected. In the game, the city
  banner opens the screen, and the banner cannot be placed by the judge (13.4). The city screen is opened by the
  set-up before its session (`UI.DoSelectCityAtPlot` on the capital, contract 1.4; the plan's `{"setup": "open_city"}`).
- **The channel was silent for 8 s once, in game, with the process alive and no box.** This was during the unit
  panel's scan, with a plot tooltip up over the map. The request was never picked up. The scan now asks again, up to 3
  times 2 s apart, and a judge that stays silent fails only that screen. Only the read is repeated, never an input.
- The game menu's never-list is wider than needed: `SaveGameButton` and `LoadGameButton` only open their menus, but
  the never-list's save/load pattern refuses them. Kept for this sweep, so both routes are swept alike.

### 13.4 The city banner hover (4507's plot hit) `[L]`

Probe (scratch `banner.py`, cursor moves and judge reads only, 2026-11-16 12:00 EST, product, the set-up above, after
a reload):
- Honolulu sits on plot [51,14] at client (957,527); its banner is drawn about 95 px above it, at client
  847-1067 x 415-449 (picture `F:/c5-64bit-work/0392/4511/diag-banner-crop.png`, not in the feed: a diagnostic).
- At points straight above the plot centre, from 0 to -110 px:
  - `under` named no control at any point;
  - the world witness (`/InGame/PlotHelpManager/TheBox`) reported the mouse at every point;
  - `resolve_hit` recorded a plot at every point: [51,14] down to -88 px, then over the banner's middle [50,15] with
    `at` [105, 19..34].

  4507 recorded [50,15] at [84,55].
- **Why.** The plot help's `TheBox` is a full-screen Button with `ConsumeMouse="0"` (EUI's `PlotHelpManager.xml`,
  read at run time), so its `HasMouseOver` holds wherever no control consumes the mouse. The banner's own Button
  (`CityBannerButton`, an instance of the banner template) gets the mouse-enter, but nothing a read can reach says so.
- **The missing read** (named in `uimap.WORLD_WITNESS_GAPS` and contract 8.1, v1.10): the world-anchored instanced
  control under the cursor. EUI's banners live in the file-local table `local g_cityBanners` of
  `CityBannerManager.lua`, and no path reaches them. Within contract 6.2 (no `debug`), no read-only chunk reaches
  them either. A getter in the banner manager's own Lua would, which is a UI change in the mod, outside the harness.
- **Consequence.** A banner hover or click stays a `plot` hit at an offset. It replays by the plot and the offset
  (contract 8.2), so it lands on the banner only while the camera matches the recording. The sweeper opens the city
  screen by the set-up instead (13.3). Unit flags were not found to have this gap (W2: the witness is false over unit
  flags).

### 13.5 The same sweep on stock, and the comparison `[L]`

**Set-up (2026-11-16 12:07-12:58 EST).** The same instance and settings, with the route switched by
`stockctl.RouteSwitch` (`vpproxy.ini mode=off`, d3deba27... backed up and restored) and a clean stock configuration:
run_3308's D33 `config.ini` arm and the composite keys taken out, as 4505 did after its stock crash (7.6), each file
backed up by SHA-256 and put back by hash. The DLL is the instance's own 32-bit `i0ship-26234371-ia`. `sweep.py
--launch`. Record: `F:/c5-64bit-work/0392/4511/stock/`.

**Result: the same 60 controls located, the same 180 actions, no finding, no box; 178 of 180 actions identical
between the routes** (outcome, restore, changed fields and the control the hit named). The comparison
(`sweep.py compare`, `F:/c5-64bit-work/0392/4511/compare.json`) has no row: no product action ended the process or met
a box, so there is nothing to class.
- The 2 differences are both hovers whose hit was read differently. `InstantYieldsIcon`: a control on product, a plot
  on stock. The game menu's `ExitGameButton`: a pixel on product, `DetailsButton` on stock. Their outcome and the
  state after them are the same. Both come from the judge's read at the moment of the hover, not from the game `[I]`.
- The unit panel showed 35 controls on stock against 38 on product (W7's inventory, with the same 4 located). Which 3
  is not examined.
- **Coverage on stock:** UI 44 of 4,475, 7 partial (the `InstantYieldsIcon` hover was a plot hit); game actions 9 of
  620; boundary not measured (no server on stock).
- **Time:** the stock sweep took 50 minutes against about 63 for product (try1 and the restarts not counted). The
  median hover step took 4.7 s on stock against 12.5 s on product. This is not the route: the first action on each
  control runs W2's hover search for its rectangle (8.5 s median from the step's start to its intent on product). The
  cache (`<run>/hi/rects.json`, keyed by build and client size) was then reused by the stock sweep on the same
  instance (1.2 s).
- 10 sessions, 195 steps, every step exit 0. `not_flat` and `not_black` 166 of 166 ok on PrintWindow pictures.

**Restored (13:00):** stockclean and the route by hash; run_3308's restore (`provision.py verify` rc 0); the settings
by hash; `frontend.py restore` (the three front-end files read "original"); the save copy removed; c5h-b released.
The harness suite at 1e18ffed under quietlock: harness 1352 passed, 0 failed, 25 skipped; instances 110 ran, 0
failed.

### 13.6 Not verified, and open items

- **Not verified live:**
  - The relaunch after a process end, the `process_ended` finding from a live death, and the reproduction filter on a
    live cut trace. No action ended the process on `cp,vp,eui`. 4510's hover death needs the unsupported `cp,vp` set
    (12.5). These paths are covered only by the hermetic tests (`test_hi_sweep.py`, `test_hi_sweep_trace.py`).
  - The box stop and `--resume` after it. Hermetic only.
- **Open items:**
  1. Most shown controls are not located (60 of 202 on these screens). The rest have no hit area of their own, or are
     smaller than the grid.
     - A finer grid costs time: about 0.25 s per point, so a full screen at 32 px is about 9 minutes.
     - A per-screen region known from the first scan would cut later scans.
  2. Instanced items (techs, production list rows, unit actions, notifications) are not swept. They have no path, and
     the inventory counts their templates only (12.2). A sweep of them needs pixel targets from a picture.
  3. The never-list refuses the game menu's `SaveGameButton` and `LoadGameButton`, which only open their menus.
     Narrowing it needs a sweep inside the save and load menus that never presses Save, Load or Delete.
  4. The judge's `screens` list misses the Civilopedia. The sweep compares the open contexts instead, but W2's
     `screens` (contract 6.1) could name it.
  5. The city banner gap (13.4).
  6. 4507's charter (`docs/briefs/hi-tester.md`) still tells runners to record a process end as `wrong_state`. The
     kind `process_ended` now exists (W5's file, not changed here).

## 14. The situation kit (W9)

Epic 0398 (D159), ticket 4600, impl-0398-w9 (account B), 2026-11-16-. Design: `tools/hi/phase/PHASE.md` section 4
(C1 accepted by the MC, queue #2797). Written by 4600 (14.1 the save survey; then the kit as built).

### 14.1 The save survey: which eras and late features the project's saves hold (part a) `[L]`

impl-0398-w9, 2026-11-16 15:00-15:50 EST, workstation. Instance c5h-h (F:, Goldberg). Claimed for 0398 with assert
policy hold; `provision.py align548` and `verify` both rc 0. **Stock route**: the install has no `vpproxy.ini`. DLL: the
pin `i0ship-26234371-ia` (3c2f47da...). Mods `cp,vp,eui` through `frontend.py prepare --mods cp,vp,eui --load`. The
client ran at the instance's size, 2554 x 1411. No box opened on any load.

**What was done.**
- **Copies.** Every candidate was copied to `F:/c5-64bit-work/0398/kit/survey/`, keeping its own name, and checked by
  SHA-256. Each header was read with `tools/d10/savecmp.header`. `survey-headers.json` there lists each copy's source
  path, SHA-256, size and header fields.
- **Live loads.** A loaded save went into the instance's `ModdedSaves/single` as `W9S-<name>`. The driver ran launch,
  `frontend.py ingame` and `dismiss`, then two reads, then `cli.py stop` with the human-turn hook:
  - the judge's `state`;
  - one read-only chunk through `tools/scenario/runner.py lua`.
- **Results.** Per save in `F:/c5-64bit-work/0398/kit/survey/live/<name>/`: `judge-state.json`, `survey-lua.json` and
  `driver.log`. Nothing of the game is committed (D83).
- **Timing.** 155-234 s per save from launch to stop for the standard saves, 251-373 s for the 42-major ones.
- **The chunk.** Scratch file `C:/c5-64bit-work/0398/scripts/survey.lua`, not committed.
  - It passes `judge_lua.scan` with no findings. Two first drafts were refused: one for `DomainTypes`, one for a
    `.Domain` field. Both names begin with the literal `Do` prefix, so air units are counted with
    `GetDomainType() == 1`, which is DOMAIN_AIR in the DLL's `CvEnums.h`.
  - The calls it uses, all getters: `Game.GetGameTurn`, `GetActivePlayer`, `GetActiveTeam`, `GetNumActiveLeagues`
    and `GetNumReligionsFounded`.
  - On each living player: `IsAlive`, `IsMinorCiv`, `IsBarbarian`, `GetCurrentEra`, `GetLateGamePolicyTree`,
    `HasCreatedPantheon`, `GetReligionCreatedByPlayer`, `GetEspionageSpies`, `GetTradeRoutes`, `GetCorporation`,
    `GetNumCities`, `GetGold` and `Units()`, with `GetUnitType` and `GetDomainType` on each unit.
  - On teams: `IsVassalOfSomeone`, `IsHasMet` and `IsAtWar`.
  - On every plot: `Map.GetPlotByIndex(i):GetResourceType()`, compared with `RESOURCE_ARTIFACTS` and
    `RESOURCE_HIDDEN_ARTIFACTS`.
  - `GameInfo` rows for names and `NukeDamageLevel`.
  - All locals, inside `pcall`.

**Counting rules for the table.**
- "Majors" and "CS" are the players alive at the load, not the slots set up.
- "Religions" is `Game.GetNumReligionsFounded`. Religions whose founder is dead count, so the list on living majors
  can be shorter.
- "Spies" and "routes" are summed over the living majors.
- "Arch." is antiquity sites, with the hidden ones after the `+`.
- "Air" and "nukes" are unit counts over every player. A nuke is an air unit whose row has `NukeDamageLevel > 0`.
- "Vassals" is the number of major teams that are a vassal of someone.
- "Corps" is the number of majors that own a corporation.
- "Era" in the save's name column is the active player's era.

The game calls Atomic `ERA_POSTMODERN` and Information `ERA_FUTURE`.

**Mods and builds.** Every header lists Community Patch v151 and Vox Populi v17. EUI is a folder in the game's DLC
directory, so no header names it. 9071-EARTH43 also lists the Hormigas Enhanced Map Pack (43 Civs). The header's DLL
fields carry no build: `dllVersion` reads `1.0.0` and `dllGuid` is the same in every save. So the build column comes
from each save's source ticket.

#### The standard DLL (the phase's own; each loads on the pin with `cp,vp,eui`)

| Save (active civ, era) | Source and SHA-256 | Map | Majors / CS | Built with | Turn | Majors' eras | WC | Ideologies | Religions | Spies | Routes | Arch. | Air | Nukes | Vassals | Corps |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `HI-4508-TINY-T0` (Russia, **Ancient**) | `F:/c5-64bit-work/0392/4508/saves/`; 4504's `HI4504-T0`, a stock client's turn-0 autosave; 68ed61e6... | Tiny, Continental Drift, Emperor | 4 / 8 | stock client, pin at the time `[I]` | 0 | Ancient 4 | no | none | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `HI-4508-TINY-T8` (Polynesia, **Ancient**) | same folder; 4507's `HI4507-TINY_0008`, the same file as `C5H-0301-CAT2_0009`; b2ceb663... | Tiny, Continents, Prince | 4 / 4 | 0301's catalogue `[I]` | 9 (the header says 8) | Ancient 4 | no | none | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `9071-LC21B-T200` = `HI-4508-LARGE-T200` (Netherlands, **Medieval**) | `F:/c5-64bit-work/9071/saves/`; e3da1d17... | Large, Communitu_79a, Prince | 18 / 18 (21 majors set up) | headless server with the x64 DLL of the pin 04b1211b (`x64-04b1211b-pfdee656`), on a stock client's database (metrics-ledger 8.3) | 200 | Medieval 11, Renaissance 7 | yes | none | 8 | 15 | 60 | 0 | 0 | 0 | 1 | 0 |
| `C5H-0168-HUGE_0350` (Egypt, **Modern**) | `F:/c5-64bit-work/0168/3957/saves/`; the same file as `C5H-VP8P-HUGE_0350` / `TB-HUGE_0350`, the D78 reference save written by VP 5.4.8; c499d3c6... | Huge, Continents, Prince | 8 / 20 | VP 5.4.8, 32-bit (new-vp-release-checklist) | 350 | Modern 8 | yes | Autocracy 1, Freedom 2, Order 2 | 5 | 46 | 55 | 34+9 | 0 | 0 | 1 | 5 |
| `Release-5.4.8-REF-HUGE_0350` (Shoshone, **Atomic**) | `F:/c5-64bit-work/0175/reference/`; 85ea3a7e...; every victory off | Huge, Continents, Emperor | 10 / 16 | 0175's `refsave` stage, a headless game from turn 0 to 350 (release-driver.md, run 3) | 350 | **Information 3**, Atomic 5, Modern 2 | yes | Autocracy 5, Order 5 | 7 | 76 | 82 | 14+15 | 144 | 20 | 3 | 6 |
| `c5h-c5h-h-t0430-035002` (Egypt, **Atomic**) | `F:/c5-64bit-work/0322/saves/`; 2f95400d... | Huge, Continents, Prince | 7 / 16 | 0322's human play through the proxy: the x64 DLL of the pin 04b1211b8, turns 350-430 from `C5H-VP8P-HUGE_0350` (proxy-design 26.5); loads on stock (26.8) | 430 | **Information 4**, Atomic 2, Modern 1 | yes | Autocracy 1, Freedom 3, Order 3 | 5 | 57 | 52 | 24+9 | 164 | 41 | 1 | 5 |
| `9071-LC21-T200` (Iroquois, Medieval in the header) | `F:/c5-64bit-work/9071/saves/`; ad5bada8... | Large, Communitu_79a | header only | headless, on the server's descriptor database | 200 | - | - | - | - | - | - | - | - | - | - | - |

`9071-LC21-T200` was **not loaded**. Saves made on the server's descriptor database stop a stock client's load in
the DLL's fatal assert `eLeaderHeadNum > -1` (metrics-ledger, "The server's own database does not fit a client"). It
has no use in the kit.

#### The 43-civ DLL (a fallback, and the kit's own "43-civ" situation)

Loaded on the same instance with the 43 build `i0ship-26234371-m43-ia` (204fbfcc...). It was put in with
`provision.py set-dll --dll F:/c5-64bit-work/0334/4252/layout/i0ship-26234371-m43-ia/CvGameCore_Expansion2.dll` at
15:31 EST. The pin was put back with `set-dll --previous` at 15:46, and the instance's DLL read back as 3c2f47da...
by SHA-256. These games were played headless by the release-43 server: 9073's `segments.sh` for the TSstd-deity
saves, 9071 for EARTH43. They load only with the 43 DLL, so a phase run on the release candidate's standard DLL
cannot use them.

| Save (active civ, era) | Source and SHA-256 | Map | Majors / CS | Turn | Majors' eras | WC | Ideologies | Religions | Spies | Routes | Arch. | Air | Nukes | Vassals | Corps |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `TSstd-deity-T175` (Netherlands, **Renaissance**) | `F:/c5-64bit-work/9071/stress/saves/`; 3551e8fb... | Huge (180 x 112), Tectonic Stress 1.20, Standard, Deity | 42 / 20 | 175 | Renaissance 40, Industrial 1, Medieval 1 | yes | none | 9 | 56 | 169 | 8 | 0 | 0 | 1 | 0 |
| `TSstd-deity-T200` (Netherlands, **Industrial**) | same; 225b2e50... (9073's acceptance save) | same | 42 / 20 | 200 | Industrial 30, Renaissance 12 | yes | none | 9 | 86 | 225 | 8 | 0 | 0 | 2 | 0 |
| `TSstd-deity-T250` (Netherlands, Atomic) | same; d1ed0634... | same | 41 / 20 | 250 | Modern 34, Atomic 5, Industrial 2 | yes | Autocracy 9, Freedom 8, Order 7 | 9 | 167 | 289 | 112+58 | 97 | 1 | 7 | 5 |
| `TSstd-deity-T000`..`T150`, `T225` (header only) | same | same | 42 / 20 set up | 0-225 | active: Ancient T0-T50, Classical T75-T100, Medieval T125-T150, Modern T225 | | | | | | | | | | |
| `TS1.20-T000`..`T400` (header only) | same folder | Huge, Tectonic Stress 1.20, **Marathon**, Prince | 42 / 20 set up | 0-400 | active: Ancient T0-T200, **Classical T225-T400** | | | | | | | | | | |
| `9071-EARTH43-T400` (America, Atomic; header only) | `F:/c5-64bit-work/9071/saves/`; 83a146c6... | Huge, PlayTheWorldExtended (Hormigas pack) | 43 set up, 36 alive (metrics-ledger 8.3) | 400 | not read: it needs the Hormigas pack as well as the 43 DLL | | | | | | | | | | |

The TSstd-deity game crashes stock after T200 (9073's acceptance runs, `F:/c5-64bit-work/9071/stress/accept/`), but
loading it and reading it at T200 was clean here.

#### Eras present and missing (the active player's era)

| Era | Standard DLL | 43-civ only |
|---|---|---|
| Ancient | `HI-4508-TINY-T0`, `HI-4508-TINY-T8` | TS1.20 T0-T200, TSstd-deity T0-T50 |
| Classical | **missing** | TS1.20 T225-T400 (Marathon), TSstd-deity T75-T100 |
| Medieval | `9071-LC21B-T200` | TSstd-deity T125-T150 |
| Renaissance | **missing** | TSstd-deity T175 |
| Industrial | **missing** | TSstd-deity T200 |
| Modern | `C5H-0168-HUGE_0350` | TSstd-deity T225 |
| Atomic | `Release-5.4.8-REF-HUGE_0350`, `c5h-c5h-h-t0430-035002` | TSstd-deity T250, EARTH43 T400 |
| Information | **missing** as the active player's era; 3-4 AI majors are in it in REF-350 and t0430 | none |

**Late features present on the standard DLL.**
- In all three Huge saves: the World Congress, ideologies (all three), religions, spies, trade routes, antiquity sites
  (hidden ones too), vassals and corporations (5-6).
- Air units and nuclear weapons only in REF-350 and t0430.
- In LC21B-T200: the World Congress, religions, spies, routes and one vassal, but no ideology, antiquity site,
  corporation or air unit.
- In the Tiny saves: none of these.

#### The plan for what is missing (standard DLL only; times `[I]` unless marked)

The route is the headless server, which makes the standard saves 9071 and 0175 already use.
- **How.** `server/run_server.py --load SAVE --turns N --save-out NAME`, in segments of 25 turns as 9073's
  `segments.sh` does.
- **The database must be the client's.** Use `--db <instance>/cache/Civ5DebugDatabase.db`, copied after the client
  has activated `cp,vp,eui`. A save made on the descriptor database does not load in a client (the LC21 case above).
- **Then.** Load each result once on the stock pin, read it with the survey chunk, and pick the turn whose active
  player is in the era wanted.
- **Why not client autoplay.** It (`vp-dll-dev`) costs about 22-36 s a turn on Large `[L, metrics-ledger 8.3, stock
  client]`, against 1.5 s a turn headless for LC21B's turns 0-200 `[L, 9071 headless/lc21b-t200]`.

| Missing | Plan | Turns | Estimate |
|---|---|---|---|
| Classical | `9071-LC21B-T000` (the same Large game as the Medieval save, turn 0) headless to T100, a save at T50, T75 and T100 | 100 | 5-10 min server time plus about 10 min of loads and reads; the 0-200 run took 302 s |
| Renaissance, Industrial | `9071-LC21B-T200` headless to T300, saves every 25 turns | 100 | 15-30 min at 5-15 s a turn late in a 21-major Large game, plus loads. Risk: the active Netherlands has 2 cities and may trail the field (7 majors were already Renaissance at T200). If it does, take the turn where it reaches the era, or use the recipe below |
| Information | `Release-5.4.8-REF-HUGE_0350` headless to T400, saves every 25 turns. Every victory is off, so the game does not end; 3 majors are already in Information | 50 | 10-20 min. 0175 averaged 6.3 s a turn over 0-350 `[L]`; late turns are slower. Fallback: `c5h-c5h-h-t0430` (4 majors already Information), but D78 says its game ends at 436 |
| Air and nukes in a smaller game | none needed: REF-350 and t0430 have 144 and 164 air units, with 20 and 41 nuclear weapons | - | - |
| Ideologies on a Large game | the same LC21B run to T300, when majors reach Industrial | (above) | (above) |

**The Lua recipe alternative (part b, PHASE.md 4.2).** A derived save could take the active team's techs up to an
era's first tech with one `SetHasTech` set-up. That gives the era's screens and policy branch at once, but a game
whose cities and units are still of the old era. Use it only where autoplay does not reach an era for the active
player.

**Which era is still uncovered.** Information is not in any project save as the active player's era, standard or
43-civ. Only the REF-350 (or t0430) run above, or a tech recipe, gives it.

**Not verified.**
- The headless times late in each game and the turn at which each era arrives.
- That a REF-350 continuation keeps its "every victory off" through T400. 0175 checked 350 -> 352 only.
- Whether `9071-EARTH43-T400` loads with the 43 DLL plus the pack on c5h-h; it was not tried.
- The DLL the two Tiny saves were made with; neither the header nor the copies' records name it.

*Outcome of this plan (part b, 2026-11-16; 14.3):*
- Classical came from LC21B T000 -> T100.
- Renaissance came from LC21B T200 -> T250.
- Information came from REF-350 -> T375.
- Industrial did not come from LC21B. The game ended at turn 334 with a victory, and the Netherlands were still in the
  Renaissance at turn 325. The era_techs recipe makes Industrial instead.

### 14.2 The kit as built (part b) `[C]`

impl-0398-w9, 2026-11-16 15:52-. Code (all committed, standard library, `python -I`):
- `tools/hi/kit/kit.py`, the command line;
- `tools/hi/kit/recipes.py`, the set-up recipes;
- `tools/hi/kit/kit.json`, the situations;
- `tools/harness/tests/test_hi_kit.py`, the hermetic tests.

The saves are in `F:/c5-64bit-work/0398/kit/saves/`. Each is named, path and SHA-256, in `kit.json`; none is committed
(D83).

- **`kit.json`.**
  - The shape `phase.py` reads (format 1, `situations` with `save`, `path`, `sha256` and `reach`), plus the kit's own
    fields:
    - `era`, `turn`, `civ`: the judge's identity read;
    - `source`, `source_path`, `source_sha256`, `made_with`;
    - `mods` (`cp,vp,eui` everywhere), `dll`, `map`, `majors`;
    - `derived_from` and `recipe` (a list of `{name, params}`), with `results` and `made` for a derived save;
    - `same_save_as`, for a situation that shares another's save;
    - `dll_sha256_required`, which only `43-civ` carries;
    - `notes`.
  - A situation with no save would carry `pending`, and `phase.py` would list it as unresolved. None is left at the
    end.
  - Era saves are named `HI-KIT-<ERA>-<MAP>-T<turn>`. Derived saves are named `HI-KIT-<SITUATION>-<first 8 hex of
    their SHA-256, upper case>`.
- **Recipes (`recipes.py`).** 15 recipes, each a fixed chunk with int parameters (`@NAME@`, checked for type and range;
  an unknown name is refused):
  - `meet_all`, `war_nearest`, `gold`, `faith`;
  - `era_techs`: the techs of the eras before the target, then one tech of the target;
  - `great_people`, `trade_units`, `archaeologists`, `nuke`, `prophet`;
  - `near_defeat`: war with the nearest major, and its units on free land around the active capital;
  - `time_limit`: `Game.SetMaxTurns(elapsed + TURNS)`;
  - `pantheon`, `religion`, `enhance`.

  Unit and belief names resolve in Lua through `GameInfoTypes` / `GameInfo`, and a missing type is skipped. The
  religion ids come from the players' `GetReligionCreatedByPlayer`, not `Game.GetFounder`, which dereferences a null
  religion in the DLL (`CvLuaGame.cpp` `lGetFounder`).
- **`kit.py` commands.** Exit 0 ok, 1 error or failed check, 2 refused, 4 judge unavailable, 8 box.
  - `list`.
  - `verify`: offline, the files, hashes, recipes, `derived_from` and reach names. With `-i NAME` it is live: each
    save is loaded once (situations that share a save share the load), then the identity read is compared with
    `era`/`turn`/`civ`, then every reach entry, then the fingerprint, then the stop.
  - `make SITUATION -i NAME`.
  - `reach SITUATION -i NAME`, on a loaded game.
  - `fingerprint -i NAME --at load|start`.
  - `prove-live`.
  - `order RUN_HI_DIR`.
  - `scan`.

  Loads and stops go through the harness's own commands, the same as `campaign.py`:
  - `frontend.py prepare --mods --load`;
  - `cli.py launch --assert-policy hold`;
  - `frontend.py ingame` / `dismiss`;
  - `cli.py stop` with the human-turn hook.

  The save is copied into the instance's `ModdedSaves/single` under its own name and checked by SHA-256 first.
- **`make`, step by step.**
  1. Refuse while `<run>/hi/session.json` shows an open session (exit 2, nothing loaded).
  2. Refuse a recipe that is not fit, before anything runs (exit 2).
  3. Refuse a base whose `dll_sha256_required` is not the instance's DLL (exit 2).
  4. Load the base save, then take the fingerprint.
  5. Arm the harness's AI-screen answerer (`diplo.ensure_armed`, issue 1116). Recipes that meet players or declare
     war raise first-contact leader screens. These hold `CvGame::update` and the Lua channel; the answerer closes them
     through their own Back function and never accepts anything.
  6. Run the recipe(s); after each one, wait until the channel answers again.
  7. Take the fingerprint and the identity.
  8. Save through the harness's own tested save: `checklist.py`'s `UI.SaveGame` on the main thread, a one-shot handler
     that removes itself. Wait for the file to settle, then hash it.
  9. Copy it to the kit's folder under its final name, checked by SHA-256, and delete the game's file.
  10. Write `<run>/hi/situation.json`: the base, the steps with params and results, the fingerprints before and
      after, the set-up start and end times, the derived save. Update `kit.json`, then stop the game.
- **Reach entries.**
  - `era:<ERA_TYPE>`: the identity read.
  - `state:<check>`: one of 30 read-only checks in one chunk, each under `pcall`. Examples: `met_all`, `at_war`,
    `border_contact`, `league`, `spy`, `ideology_open`, `pantheon_possible`, `has_pantheon`, `has_religion`,
    `prophet`, `trade_unit`, `archaeologist`, `antiquity_site`, `air_unit`, `nuke_unit`, `vassal_any`,
    `corporation`, `great_person`, `enemy_near_capital`.

  They test that the game state behind a screen or action is there for the active player. **They do not open the
  screen**: PHASE.md 4.3's "the screen opens" is left to the charter's first session `[I]`.

### 14.3 The situations `[L]` (c5h-h, stock route, pin `i0ship-26234371-ia` 3c2f47da..., `cp,vp,eui`)

Era saves (plain copies; the active player's era read live):

| Situation | Save | From | Turn | Active civ, era |
|---|---|---|---|---|
| `era-ancient` (= `early`, `stale-selection`) | `HI-KIT-ANCIENT-TINY-T8` (b2ceb663) | 4508's Tiny T8 | 9 | Polynesia, Ancient |
| `era-classical` | `HI-KIT-CLASSICAL-LARGE-T100` | LC21B T000 played headless to T100 by 4600 (`lc-db`, server `x64-04b1211b-pfdee656`, 0 -> 100 in about 2.5 min) | 100 | Netherlands, Classical |
| `era-medieval` (= `mid-game`, `border-contact`, `world-congress`, `events`) | `HI-KIT-MEDIEVAL-LARGE-T200` (e3da1d17, = `HI-4508-LARGE-T200`) | 9071 | 200 | Netherlands, Medieval |
| `era-renaissance` | `HI-KIT-RENAISSANCE-LARGE-T250` (1406934b) | LC21B T200 -> T250 headless | 250 | Netherlands, Renaissance |
| `era-industrial` (= `ideology-ready`) | `HI-KIT-ERA-INDUSTRIAL-B86C97E9`, derived: `era_techs(era=4)` on `era-renaissance` | recipe | 250 | Netherlands, Industrial |
| `era-modern` (= `spies`, `corporations`) | `HI-KIT-MODERN-HUGE-T350` (c499d3c6) | `C5H-0168-HUGE_0350` | 350 | Egypt, Modern |
| `era-atomic` (= `air`, `vassals`) | `HI-KIT-ATOMIC-HUGE-T350` (85ea3a7e) | 0175's REF-350 | 350 | Shoshone, Atomic |
| `era-information` | `HI-KIT-INFORMATION-HUGE-T375` (daeedff1) | REF-350 -> T375 headless on the descriptor `test_db` it was made on (about 20 s a turn) | 375 | Shoshone, Information |
| `near-defeat` | `HI-KIT-NEAR-DEFEAT-LARGE-T325` (c9eaecde) | LC21B T200 -> T325 headless (`lc-db`); Arabia won a cultural victory at T334 in that game (14.6) | 325 | Netherlands, Renaissance |
| `43-civ` | `HI-KIT-43CIV-HUGE-T200` (225b2e50) | 9073's TSstd-deity T200; needs the 43 DLL (`dll_sha256_required` 204fbfcc...) | 200 | Netherlands, Industrial |

Derived saves (`kit.py make`, 2026-11-16 16:16-17:00; each set-up 129-209 s from the start of the load to the save
settled, plus the stop):

| Situation | Base | Recipe(s) and the recipe's own answer | Derived save |
|---|---|---|---|
| `all-met` | era-ancient | `meet_all`: met 7 | `HI-KIT-ALL-MET-429ABBA4` |
| `at-war` | era-ancient | `war_nearest`: war on player 3 at distance 13 | `HI-KIT-AT-WAR-69CBC281` |
| `ai-demand` | era-ancient | `meet_all`: met 7 | `HI-KIT-AI-DEMAND-9101BEC0` |
| `religion-stages` (stage 1) | era-ancient | `faith(300)`: pantheon possible | `HI-KIT-RELIGION-STAGES-7B26B27E` |
| `religion-pantheon` | era-ancient | `pantheon`, `prophet` | `HI-KIT-RELIGION-PANTHEON-6B7FAC52` |
| `religion-founded` | religion-pantheon | `religion` (religion 1), `prophet` | `HI-KIT-RELIGION-FOUNDED-7BC7B746` |
| `religion-enhanced` | religion-founded | `enhance` | `HI-KIT-RELIGION-ENHANCED-178675FA` |
| `capital-threat` (made as `near-defeat`, renamed 18:1x) | era-ancient | `near_defeat(4)`: war on player 3, 4 of its units around the capital | `HI-KIT-CAPITAL-THREAT-13302B55` |
| `near-victory` | era-modern | `time_limit(1)`: max turns 351, elapsed 350, Egypt the score leader (2398) | `HI-KIT-NEAR-VICTORY-C648E6E4` |
| `ai-proposals` | era-medieval | `meet_all` (16), `gold(3000)` | `HI-KIT-AI-PROPOSALS-563F19B1` |
| `trade-routes` | era-medieval | `trade_units`: a caravan (the capital is not coastal) | `HI-KIT-TRADE-ROUTES-0F28F7A4` |
| `great-people` | era-medieval | `great_people`: 9 types | `HI-KIT-GREAT-PEOPLE-E9663E20` |
| `archaeology` | era-modern | `archaeologists(2)` | `HI-KIT-ARCHAEOLOGY-0D678F97` |
| `nuclear` | era-atomic | `nuke(1)` | `HI-KIT-NUCLEAR-5DA39651` |
| `era-industrial` | era-renaissance | `era_techs(4)`: 11 techs, era 4 (Industrial) | `HI-KIT-ERA-INDUSTRIAL-B86C97E9` |

**Not made:** a reformation stage of `religion-stages` (in VP the reformation belief comes from a policy, and no
recipe was written for it). `near-victory` was first left unmade; lead-0398's steer gave the time-limit recipe
and the natural `near-defeat` (14.6).

**In all:** 35 situations on 24 save files. All are made and all were verified live (14.5-14.7).

**`era-industrial` overshot the first time.** Granting every tech of the eras up to Industrial gave a Modern player
(`HI-KIT-ERA-INDUSTRIAL-8C86000B`, removed). A free tech of the next era probably came with them `[I]`. The recipe
now grants the eras before the target plus one tech of the target, and the save was made again.

**The first `make` (all-met, 16:06) and the leader screens `[L]`.**
- `Teams:Meet(team, true)` raised AI first-contact leader screens during the chunk. The channel's answer never came
  (120 s timeout), and `cli.py stop` then waited in its human-turn check.
- I closed the three screens with posted clicks on Goodbye (`cli.py click 0.945 0.953`). The two diagnostic shots
  are in the Visual Checks feed (issue 0398).
- Arming `diplo` before the recipe fixed it: the next `make` of all-met took 145 s with no screen left.

### 14.4 Read-only during play: the three proofs (PHASE.md 4.2)

**1. Static `[C]`.** `recipes.scan_recipe(code, writes)` runs the judge's own tokenizer and scanner
(`judge_lua.scan`, contract 6.2) over the rendered recipe. A finding is accepted only when it is of the form "<name> (a
writing name)" and the name is in the recipe's own `writes` list, which must lie inside `WRITE_NAMES`. `WRITE_NAMES`
is `Meet`, `DeclareWar`, `SetHasTech`, `ChangeGold`, `ChangeFaith`, `InitUnit`, `FoundPantheon`, `FoundReligion`,
`EnhanceReligion` and `SetMaxTurns`.

On top of the judge's scan, a recipe is refused when:
- it names `Events`, `GameEvents`, `LuaEvents`, `Network`, `ContextPtr` or `UIManager` anywhere, or any `Register*`
  name, even as a string;
- it calls a name of `WRITE_NAMES` that is not in its own `writes`. The judge's prefix test does not catch `Meet`,
  `DeclareWar`, `Found*` or `Enhance*`.

The judge's scan already refuses a global or field assignment, code loading and `UI.` setters. `READ_EXCEPTIONS`
names `DomainTypes`, which the literal `Do` prefix takes for a write. A test shows that the exception is the recipes'
alone: the judge still refuses it. `judge_lua.py` is not changed.

`test_hi_kit.py` holds the tests:
- every recipe scans clean and compiles in Lua 5.1 (lupa);
- 19 planted forms are each refused: `Events.*.Add`, `GameEvents.*.Add`, `LuaEvents`, `Register*`, `Network.Send*`,
  `ContextPtr:SetUpdate`, `UIManager:QueuePopup`, a global assignment, a global function, a field of a game table,
  `loadstring`, `setmetatable`, `SetGold` / `ChangeGold` / `Meet` / `DeclareWar` / `FoundPantheon` outside the
  recipe's writes, a `UI.` setter, and the string `"Events"`;
- `make` refuses an unfit recipe before it loads anything.

The kit's own reads (identity, fingerprint, reach) pass `judge_lua.checked` before every send, with no exception.

**2. Order `[C]`, live below.**
- `make` refuses while `<run>/hi/session.json` has an open session: exit 2, nothing loaded (tested).
- `make` writes `<run>/hi/situation.json` with `set_up_started`, `set_up_ended` and the derived save's SHA-256.
- `kit.py order RUN_HI` checks two things:
  - the run's `session.json` `started` is later than `set_up_ended`;
  - the session's trace header names the derived save's SHA-256.

  A session started first, another save or no hash in the header each fail it (exit 1). Missing files are refused
  (exit 2).
- The header's save hash is written by the sweeper (`sweep.py` passes `save={name, sha256}` to `session.start`). A
  plain `hi.py session start` writes `null` there, so `order` fails on such a run until the session's owner fills it,
  as `campaign.py harden` does. `phase.py` (4601) has its own run-start check (`_save_check`).

**3. Live `[C]`, live below.** The fingerprint is one read-only chunk:
- the turn and the active player;
- for each living player: units, cities, gold and era;
- every pair of players at war.

`kit.py fingerprint --at load` and `--at start` write `<run>/hi/kit-fingerprint-<at>.json`. `prove-live` compares
them: equal is ok; otherwise exit 1, naming the lines that differ (tested with one unit more).

**Proofs 2 and 3, live `[L]`** (2026-11-16 18:14-18:20, c5h-h, stock route). The run was on `near-victory`, the
last `make`, so its `situation.json` is the instance's. Scratch driver `C:/c5-64bit-work/0398/scripts/live_proof.py`
(not committed):
1. Load `HI-KIT-NEAR-VICTORY-C648E6E4`: 230 s to in game.
2. `fingerprint --at load`.
3. `session.start` with the save named by SHA-256 in the trace header (as `sweep.py` does): ticket 0398, stock route,
   width 1280. The session's own pictures stay in `F:/c5-64bit-work/0398/kit/proof/near-victory/`.
4. `fingerprint --at start`.
5. `session end`.

`prove-live` was ok: the load and start fingerprints were both b33f5e2d.... `order` was ok: the set-up ended at
18:12:31 and the session started at 18:18:06, from the derived save's SHA-256.

### 14.5 The live verify of every situation `[L]`

`kit.py verify -i c5h-h` over the 32 standard-DLL situations, 2026-11-16 17:00:52-18:07:56 (67 min):
- 22 loads: situations that share a save share the load.
- Load to in game: 121-253 s. Tiny saves took about 2 min, Large 2.5-3 min, Huge 3-4 min.
- **32 of 32 ok.** Every identity read matched `kit.json` (era, turn, civ), and every reach entry read true:
  - `met_all`, `at_war`, `border_contact`, `league`, `spy`, `ideology_open` (the Industrial save);
  - `pantheon_possible`, `has_pantheon`, `has_religion`, `prophet`, `trade_unit`;
  - `archaeologist` with `antiquity_site`, `air_unit`, `nuke_unit`, `vassal_any`, `corporation`;
  - `great_person`, `enemy_near_capital`, ...
- No box opened.
- Records are in `F:/c5-64bit-work/0398/kit/verify/verify-<situation>.json`. `kit.json` carries each situation's
  `verified` field and the turn and civ read.

`near-defeat`, `capital-threat` and `near-victory` came later: see 14.6.

### 14.6 Victory and defeat situations (lead-0398's steer for charter 31, lifecycle-menus)

- **`near-defeat`** is a plain save: `HI-KIT-NEAR-DEFEAT-LARGE-T325` (c9eaecde), LC21B played headless to turn 325.
  - In that headless game the server reported "GAME OVER at turn 334: winner team 16, victory type 3". Team 16 is
    Arabia. Type 3 is the cultural victory if the Victories table keeps the stock order (time 0, space 1,
    domination 2, cultural 3, diplomatic 4) `[I]`; the ID was not read in the game. That is 9 turns after the save.
  - A client game from the save plays its own AI turns, so the winner and the turn can differ `[I]`. The kit only
    checks that a victory is valid and that the Netherlands have cities.
- **`capital-threat`** is the recipe save first made as `near-defeat`: `near_defeat(4)`, war and 4 enemy units around
  the Tiny capital. The file was renamed to `HI-KIT-CAPITAL-THREAT-13302B55`; the bytes and hash are unchanged.
- **`near-victory`** is derived from `era-modern` with `time_limit(1)`: `Game.SetMaxTurns(elapsed + 1)`.
  - The max turns live in the pregame and are saved with the game. `CvGame::testVictory` gives the time victory to the
    highest score once the elapsed turns reach the maximum.
  - The recipe answered "max turns 351 elapsed 350 leader 0 score 2398 active score 2398", so Egypt leads.
  - **Checked live `[L]`** (18:22-18:26, scratch `nv_check.py`):
    1. Load the derived save, arm the AI-screen answerer.
    2. One `turns.py end`. It forced the end past production, city-attack and event blockers.
    3. Read: game state 1 (over), winner team 0 (Egypt, the active team), `VICTORY_TIME`, turn 351.
  - So a session that ends one turn on this save sees Egypt's victory screen. The victory screen itself was not read
    (no picture taken).

**Verified live 18:24-18:44 `[L]`:**
- `capital-threat` ok: `at_war` and `enemy_near_capital` true.
- `near-victory` ok: `score_leader` and `time_victory_next` true.
- `near-defeat` ok on its second run. Its first reach list also asked `cities_several`, which read false: the
  Netherlands have one city at turn 325. The list is now `victory_valid_any`.

### 14.7 The 43-civ situation and the product route `[L]`

- **`43-civ`** (18:42-18:48), c5h-h:
  - `provision.py set-dll --dll` the 43 build `i0ship-26234371-m43-ia` (204fbfcc...);
  - `kit.py verify --situation 43-civ`: in game after 320 s; identity turn 200, the Netherlands, Industrial, 42
    majors, 20 city states, 180 x 112, `MAX_MAJOR_CIVS` 43; ok;
  - `set-dll --previous`: the pin read back as 3c2f47da....

  `kit.py` refuses `43-civ` on an instance whose Community Patch DLL is not 204fbfcc... (exit 2, tested), so a phase
  run on the standard DLL cannot load it by mistake.
- **A derived save on the product route** (18:48-18:53, c5h-h). The first playable `x64-04b1211b-pfdee656` was laid
  as 9.3 and 4602 lay it:
  - `set-dll i0ship-26234371-ia`, a test pack from `pack.py`, `set-proxy`, `vpproxy.ini pack=1`;
  - the proxy 75c40cec..., the x64 DLL 29f58e17..., the server 5b254430...;
  - WindowRes 1920 x 1080 (D158), set before the launch.

  Every changed file was backed up by SHA-256 first. Scratch `C:/c5-64bit-work/0398/w9prod/prep.py` is a copy of
  4602's with this ticket's paths. Then:
  - `HI-KIT-ALL-MET-429ABBA4` loaded with `cp,vp,eui`: in game after 127 s;
  - the judge's `state` was available;
  - the kit's identity read: turn 9, Polynesia, Ancient, 4 + 4 players;
  - the fingerprint (61c12bd1...) **equals the one the stock-route verify read** from the same save;
  - stop, then restore: "restore: OK", every file back by hash, the front end original, `provision verify` rc 0.

  One derived save was tried on the product route, not all of them.
- **The instance at the end.** I removed the 24 `HI-KIT-` save copies that the kit's loads had placed in the
  instance's `ModdedSaves/single`. `frontend.py status` reads original, `provision verify` rc 0, the Community Patch
  DLL is the pin 3c2f47da..., there is no `vpproxy.ini`, and no box is open. c5h-h was released at 18:53.

**Timings (workstation, shared, not timing claims):**
- `make` takes 129-209 s from the start of the load to the save settled, plus 20-30 s for the stop.
- A verify load is 121-320 s (the 43-civ game 320 s).
- Headless continuations on the server `x64-04b1211b-pfdee656`:
  - LC21B 0 -> 100: about 2.5 min;
  - 200 -> 334: about 11 min, with turns slower late in the game;
  - REF 350 -> 400: about 20 s a turn.

**Suites** (2026-11-16 18:54, under `quietlock.py run --who 0398`, `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`):
- every hermetic `test_hi_*.py` (14 files, `test_hi_kit.py` among them with 109 tests) and `test_safety_static.py`:
  **798 passed, 1 skipped**;
- the skip is pre-existing: `test_hi_actions.py:270`, "W2's deny.py has landed; its own tests cover the list".
- after merging `epic-0398-hi-kit` again (19:00, the same files plus the epic's newer `test_hi_*` tests): **809 passed,
  1 skipped** (the same skip).

### 14.8 Not verified, and open items

- **Screens not opened.** No situation's screens were opened by input. The reach checks read the game state behind
  them (14.2); PHASE.md 4.3's "the screen opens" is the first session's.
- **AI-turn situations.** `ai-proposals`, `ai-demand` and `events` only set up the conditions. The AI makes its
  proposals, demands and events during AI turns, and none was waited for `[I]`.
- **`near-defeat`.** The cultural victory at turn 334 is the headless game's. A client game plays its own AI turns
  `[I]`.
- **Overlaps.** `era-industrial` and `ideology-ready` are the tech recipe on the Renaissance save, not a played
  Industrial game. Its cities and units are those of a Renaissance game. `ideology_open` (Industrial era, no
  ideology) is the reach check; it does not show that the ideology choice pops up.
- **Not made.**
  - A reformation stage of `religion-stages`.
  - Coastal trade: the cargo ship was skipped, because the LC21B capital is not on the coast.
  - A save where the active player is a vassal or a master: `vassals` only has 3 vassal teams in the game, and
    `vassal_involved` was not asked.
- **The product route.** Checked for one derived save (all-met). The other 23 save files were checked on the stock route only.
- **Proofs 2 and 3.** Shown live on one situation (`near-victory`). Nothing yet calls them as part of a phase run:
  `sweep.py` and `campaign.py` would take `kit.py fingerprint --at load|start` and `order` around their session
  start. That is W10's (4601/4602) decision.

## 15. The increment acceptance phase: the phase tool (W10a, ticket 4601) `[C]`

Ticket 4601, impl-4601 (account B), 2026-11-16 15:10-15:30 EST. Design: `tools/hi/phase/PHASE.md` sections 2, 3, 5
and 7 (C1, MC #2797). Code: `tools/hi/phase/phase.py` (one CLI, standard library, `tools/hi` modules loaded by file
path under `session.py`'s own keys); the kept set `tools/hi/phase/kept/index.json` (empty); the charters in
`tools/hi/charters/`; hermetic tests `tools/harness/tests/test_hi_phase.py` (70; 74 after lead-0398's review, below). Scratch scripts are in
`F:/c5-64bit-work/0398/4601/scripts/` (not committed).

### 15.1 The catalogue

PHASE.md 3.2's 36 items: 0392's six in `charters.json`'s `charters` list (unchanged apart from
an added `situation`: `mid-game` for city-screen, tech-tree, diplomacy and camera-selection, `early` for save-load and
options; pure-proof is not in the phase), and 30 new files with their entries in a new list, `phase_charters` (items
7-36: `name`, `number`, `group` A-G, `file`, `situation` (a kit name, a list of names, or null for the front end),
`ends_turn`, `turns`, `explorer`, `model_free` (`loads`, `setup`, `window-abuse` or null), `made_saves`). The new
entries are in their own list because `campaign.py load_index` needs a `save` on every entry of `charters` and
`test_campaign.py` asserts exactly seven there; so `campaign.py` is unchanged (lead-0398, 2026-11-16). Every new
charter follows 0392's format (start, goal, about 8 minutes a part, "Using the hour", out of bounds beyond the brief's
5.2, what the report names); those that end turns keep it to 1-3 (war-four-ways: one part per session from a fresh
load). The three model-free items say what the first recording covers and how phase.py replays it. The quick save
and quick load of item 32 are the one save not named `HI-` (contract 9), kept in the instance's own save folder and
removed by the owner after the run (lead-0398 #2814).

### 15.2 The kit as phase.py reads it

The interface fixed by lead-0398, 2026-11-16: `tools/hi/kit/kit.json` =
`{"format": 1, "situations": {NAME: {"save", "path", "sha256", "reach": [...], ...}}}` (a list of `{"name", ...}` is
read too; other fields are ignored). A missing kit is not an error: every situation is listed as unresolved in the
plan, and `campaign/charters.json` names no charter.

### 15.3 The phase folder

`F:/c5-64bit-work/<ticket>/phase/<candidate>/`, never committed: `plan.json`/`plan.md`,
`runbook.json`, `decisions.json`, `findings.json`/`.md`, `removed.json`, `campaign/charters.json` (campaign.py's
index with the kit's saves resolved, absolute paths, by SHA-256, the charter files copied beside it so that
`campaign.py --index` loads it), `coverage/` and `verdict.json`/`verdict.md`.

### 15.4 The commands

- `plan --candidate ID [--rehearsal] --out DIR [--kit F] [--kept DIR] [--charters a,b] [--situations a,b] [--only
  sweep,charter,setup,loads,window-abuse,kept] [--replace]`. The full plan is 94 items: a sweep per distinct save and
  route (corrected 2026-11-16 20:02 by lead-0398 after MC #3061; was "per situation and route": situations that share
  a save file share one sweep, named by the save's own situation and listing the others; with the committed kit, 24
  saves x 2 = 48; without a kit, every situation the charters name is its own, 22 x 2 = 44), 35 explorer
  charters (window-abuse has no explorer), `loads`, `setup:1`..`setup:8`, six `window-abuse:<save|load|ai-turn>:
  <alt-tab|minimise>`, and one `kept:<file>` per active kept trace. A resize is listed as not planned: the game's
  window has no sizing border (10.3). A filtered plan is marked `subset`. A plan is refused (exit 2) over an existing
  one, for an unknown charter, situation or kind, and for a blank candidate; a malformed kit is exit 1.
- `record DIR --item ITEM --run RUN --build ID`: the run's own records decide its kind and status. A sweep
  (`sweep.json`: completed when `ended` and not `stopped`; the route must be the item's; the start save by SHA-256
  from the traces' headers must be the situation's). A session (`trace.jsonl`: completed with an end record). A
  campaign record (`campaign.json`: each session of the item's charter, with its minutes, tokens and the owner's
  finding statuses). A replay (`replay.json`: completed when it started; `passed` when exit 0 and no checkpoint
  failed; for a `kept:` item the trace's name must match). phase.py's own `loads.json`, `options.json` (the
  configuration must match) and `window-abuse.json` (the moment and act must match). The build is required. A build
  that is neither the candidate nor a named successor is recorded with `unnamed_build` (clause 5 fails on it). The
  build hashes are taken from the traces' headers. A stock-route run is recorded with the candidate's label too (route
  `stock`: the stock control of that candidate's phase), as the i2-rc1 stock sweeps were; the four tb1 stock
  window-abuse records first labelled `stock-tb1-3c2f47da` were relabelled so (old label kept in the record; MC #6388,
  #6394, 2026-11-18). Clause 1 counts the model-free-only charters 32 and 33 as run when their items (`loads`,
  `setup:1-8`) are completed (lead-0398, 2026-11-18, MC #6388; PHASE.md 2) [C: test_hi_phase].
- `findings DIR`: the ledger, built from the run book's traces (runner and harness findings), the campaign pipelines
  under each run (`**/pipeline/*/finding.json` with `repro.json`, `stock.json` and `report.json`, joined by the
  runner's signature), the campaign record's statuses and phase.py's own findings. Each row: id
  `<item>|<run folder>|<signature>`, kind, `what` (the failed check), status (`report`, `observation`, `usability`,
  `duplicate` or `open` = not yet through the filter), class, reproduced `k/n`, report folder, bug and closure.
- `close DIR --finding ID` takes one of three forms. `--fixed-by BUILD --bug N --clean-run REPLAY_DIR`: the build
  must be the candidate or a named successor, and the cut trace's replay must be clean. `--reclassify REASON --ref
  MC#N`. `--status observation|duplicate|usability --why TEXT`: refused for a report. `successor DIR --build ID
  --fixes F1,F2 [--items I1,I2] --by WHO` names a successor build mid-phase. `removed DIR --measure M --entry E
  --reason R` names a coverage entry that the candidate removed (clause 4).
- `loads -i NAME --trace T [--n 20] --out DIR` and `options -i NAME --trace T --config N --variants V.json --out
  DIR` replay through `replay.run` on a game the owner launched (no launch, no stop, no Lua write).
  - **Loads:** a box before or after any load gives exit 8 (never pressed). After each load the process is checked
    (gone: a `process_ended` finding, exit 1) and the judge's turn and gold must equal the reading before load 1
    (else a `wrong_state` finding; the loop goes on).
  - **Options:** the variants file is `{"format": 1, "configs": {"N": {"label", "edits": [{"step", "action",
    "centre_px": [dx, dy]}], "expect": {judge fields}} | {"absent": "why"}}}`. An edit moves a front-end click to a
    centre-anchored pixel hit, and the changed trace is checked with check-trace before it runs. An `absent`
    configuration is listed "not in the candidate", with no replay.
- `window-abuse build --trace T --at STEP --moment save|load|ai-turn --act alt-tab|minimise --out DIR` splits the
  trace. Part A runs up to STEP with STEP's wait dropped. Part B starts with a step STEP that has no action and
  carries the dropped wait, followed by STEP's checkpoints and the rest. The window action is written as a
  `realinput.py journey` file: alt-tab to the decoy or minimise, 5 s away, then the harness's focus. `script.json`
  holds the `tb.py --host laptop submit --quiet --headed ... realinput.py job` line and three job commands: replay
  A, the journey, replay B. `window-abuse run DIR --job JOB --bin TESTBED_BIN` sends them through
  `tools/testbed/live.py --host laptop send` and stops at the first that does not answer rc 0 (8: a box finding; 5
  or 6: a `wrong_state` finding). Any host but `laptop` is refused (exit 2). Nothing real-input runs from phase.py.
- `kept add DIR --trace T --charter C --situation S` takes a stripped, check-trace-clean trace (as a journey). A trace
  that keeps a picture is refused (exit 2: strip it first with `trace.py strip`). The trace is copied as
  `<charter>-<n>.jsonl` with its SHA-256, situation and candidate. `kept check` checks every file, its hash and
  check-trace (exit 2 on a problem). `kept retire --file F --reason R` records a repair.
- `verdict DIR [--previous PREV] [--no-coverage]`: PHASE.md 2's five clauses from the records; exit 0 PASS, 6 FAIL.
  - **Clause 1:** every item has a completed run on an allowed build. An item that a successor must re-run needs a
    run on that successor. A charter also counts as run when all of its kept traces replayed. A retired kept trace
    is not required.
  - **Clause 2:** no `report` that is `patch only` and not closed, whose kind is `box`, `hang`, `process_ended`,
    `wrong_state` or `input_ignored`, or `wrong_picture` from a model-free check (`not_flat`, `not_black`,
    `not_frozen`, `no_flicker`, `changed`). An `open` finding (not through the filter) also fails it. Stock too,
    stock only, diverged, observations, usability and a stock-difference `wrong_picture` are listed only.
  - **Clause 3:** the last replay of every active kept trace passed, and `kept check` is clean. Repairs are listed.
  - **Clause 4:** W7's `coverage.cmd_report` over every trace of a completed run, using the candidate table,
    inventories, lists and call tables that the runs left in their `coverage/` folders. Each of the three measures
    must be present, and every uncovered entry must carry a reason. With `--previous`, the previous verdict must be
    a PASS that is neither a rehearsal nor a subset (else exit 2), and every entry covered there must be covered now
    or named in `removed.json`. The first phase sets the baseline.
  - **Clause 5:** every run is on the candidate or a named successor, and one build id carries one set of
    EXE/DLL/server hashes per route. A successor requires: the items it names, the whole kept set, and the sweeps of
    the situations its fixed findings came from.
  - `verdict.md` is headed "REHEARSAL, not an acceptance" for a rehearsal and marks a subset.

### 15.5 The dry run on 0392's records `[C]`

Run 2026-11-16 15:25 EST (scratch `dryrun.py`; output
`F:/c5-64bit-work/0398/4601/dry/`). Setting: a fake kit (`dry/kit.json`: `mid-game` = `HI-4508-LARGE-T200`
e3da1d17..., `early` = `HI-4508-TINY-T8` b2ceb663..., the save 4511's sweeps started from), an empty kept set, and
candidate `x64-04b1211b-pfdee656` as a rehearsal. Recorded: the six charters from
`F:/c5-64bit-work/0392/4508/campaign.json`, and 4511's product and stock sweeps as `sweep:early:product/stock`.
- `dry/full/` (the whole plan, 94 items): 8 runs recorded, all `completed`. The ledger has 17 findings, the same 17 as
  research 9.6: 3 reports stock too, 8 usability, 2 duplicates, 2 observations, 1 observation stock too, 1
  observation diverged. **Verdict FAIL, clause 1 only** (86 items without a run); clauses 2-5 hold. The coverage
  over the 8 runs' traces: UI 45 of 4,475, actions 28 of 620, boundary 452 of 1,615 (308 excluded). Summary: 184.1
  runner minutes, 1,101,196 runner tokens.
- `dry/subset/` (`--charters` the six `--situations early --only sweep,charter`, 8 items): **verdict PASS (a
  rehearsal, a subset)**. The three stock-too reports (tech-tree drag, diplomacy city-state popup, options first
  click) are listed under clause 2, not failing.
- Observed `[I]`: the charter sessions add little to UI coverage (45 against 4511's 44 alone). W7 counts a UI entry
  of a screen that no inventory saw as "its screen was never seen open", and only the sweeps take inventories, so
  screens that only an explorer opens stay uncounted until a sweep or an inventory step covers them.

### 15.6 Not verified

- No live run of `loads`, `options` or `window-abuse`. The live phase is W10b.
- The window-abuse job's `@BIN@` path (the job's `TESTBED_BIN`, given by hand to `run --bin`).
- Whether a posted window action during a save or load lands while the save is still running (the replay takes a
  step picture after the split step, 0.3-1 s).
- The quick-save file's exact name.
- The coverage of a whole phase (time and memory over about 100 runs' traces).

### 15.7 Lead review (lead-0398, 2026-11-16 15:33 EST) `[C]`

- **Clause 2 gap closed.** A reproduced finding (`report`) of a holding kind whose stock control had not run (no
  class: `stock.json` missing or `class: null`) was only listed under clause 2, so a phase could pass with a product
  fault that nobody had classed. `phase.holding_kind` and `phase.unclassified` now fail clause 2 on it ("reproduced,
  but its stock control has not run"); a stock-difference `wrong_picture` without a class still does not fail. Four
  tests added (`test_clause2_a_reproduced_fault_without_its_stock_control_fails` x3,
  `test_clause2_a_reproduced_stock_diff_picture_without_class_does_not_fail`). Under quietlock: `test_hi_phase.py` +
  `test_campaign.py` 128 passed, 0 skipped; `test_safety_static.py` 55 passed.
- **D162 (the owner, 2026-11-16 15:44; added 15:49).** Every plan now has the item `journeys` (the validated tier's
  `hi_journey` stage over `release-basic` and `release-large`, read from `tools/hi/journeys/journeys.json`),
  recorded from the stage's `report.json` (`phase.read_journeys`). It is a run (clause 1) only when both journeys
  ran on both routes, and clause 3 fails unless both passed (`passed on rerun` of a known flake counts). The full plan
  is 95 items. Seven tests (`test_d162_*`); `test_hi_phase.py` + `test_campaign.py` 135 passed under quietlock.
- **The judge's open-screen names (2026-11-16 19:30, MC #3041, from 0325's full-game soak `[L]`).** `judge_lua.SCREENS`
  named `/InGame/ChoosePantheonPopup`, `/InGame/ChooseReligionPopup` and `/InGame/LeagueOverview`; the game's
  contexts are `/InGame/ChoosePantheon`, `/InGame/ChooseReligion` and `/InGame/LeagueOverviewPopup` (the popups'
  Lua files are named ...Popup.lua, their contexts are not). A wrong path is skipped silently (LookUpControl answers
  nil), so those three screens were never reported open: the religion and world-congress charters' screen waits would
  have timed out. Corrected (data only, no new call); `test_the_religion_and_league_screens_carry_the_games_context_names`
  pins them. Harness runner (`run_tests.py -k` judge, safety_static, kit, phase, sweep, coverage): 488 passed, 0
  skipped.

### 15.8 The model-free items live (W10b part 1, ticket 4602) `[L]`

Ticket 4602, impl-4602 (account B), 2026-11-16 15:36- EST. A **rehearsal** (MC #2797 answer 5) of PHASE.md 3.2
items 32, 33 and 36 with `phase.py` (15.4) live. Phase folder `F:/c5-64bit-work/0398/phase/rehearsal-x64-04b1211b/`
(`plan --candidate x64-04b1211b-pfdee656 --rehearsal --only loads,setup,window-abuse`: 15 items). Records under
`F:/c5-64bit-work/0398/4602/`; scratch scripts, logs and the instance backups in
`F:/c5-64bit-work/0398/4602/scripts/` (not committed; the C: scratch folder was removed at the hand-back).

**Setting (items 32 and 33).** c5h-c (C:, Goldberg; claimed by impl-4602, ticket 0398, assert policy hold), the first
playable laid as 4508 did (9.3; scratch `prep.py`, which also keeps the instance's `quick/` and `auto/` save folders
by SHA-256): DLL i0ship-26234371-ia, pack of the layout (x64 29f58e17..., server 5b254430..., proxy 75c40cec...,
purity 6a0d7b6b...), `vpproxy.ini pack=1`; `VP64Options.ini` 9497f85b... with every option commented out (the
shipped defaults: thin client, ServerRenderer 0); WindowRes 1920x1080 (client 1914x1051, D158),
SinglePlayerAutoEndTurnEnabled 0; product route, `cp,vp,eui`, width 1280; picture source PrintWindow (no composite).

**Item 32, lifecycle loads.**
- Recorded 15:45-15:47 (`rec-loads/trace.jsonl`, 6 steps, 2 checkpoints, check-trace clean) on `HI-4508-TINY-T8`
  (b2ceb663..., Tiny, Polynesia, turn 8, gold 18): checkpoint `start` (turn, gold); `ESC`; LOAD GAME (control); the
  save's row (pixel); Load Game (control, `front_end load_screen`, 10.5 s); `wait 15000` + `ENTER` (`in_game`); a
  `stable` step; checkpoint `loaded` (turn, gold, screens). The recording's load: click 15:46:30, load screen 10.5 s
  later, Continue shown by 15:47:07 at the latest.
- `phase.py loads --n 20` 15:48:18-16:07:07: **20 of 20 completed, every replay exit 0, turn 8 and gold 18 after
  each load, no finding, no box.** A replay took 50.6-68.5 s (median about 55 s; the trace's fixed 15 s wait
  included).
- **The quick pair**, recorded 19:17-19:20 on the same save after a fresh launch (`rec-quick/trace.jsonl`, 8 steps):
  checkpoint `start`; `ESC`; QUICK SAVE (control `/InGame/GameMenu/QuickSaveButton`; it closes the game menu and
  writes `ModdedSaves/single/quick/QuickSave.Civ5Save`, 220,077 B here); `ESC`; LOAD GAME; the top row (QuickSave,
  now the newest); Load Game; `wait 15000` + `ENTER`; stable; checkpoint `loaded` (turn 8, gold 18). Hand edit
  (`trace-e1.jsonl`, named in the header): QUICK SAVE's step waits on `screen_closed /InGame/GameMenu` (check-trace's
  v1.9 warning). `phase.py loads --n 20` 19:20:23-19:41:22: **20 of 20 completed, turn 8 and gold 18 after each, no
  finding, no box**; a replay took 55.5-85.5 s (median 60.8 s). The instance's own quick save (Nov 14) was kept by
  SHA-256 before the run and put back after it.
- Order matters: the menu-load trace clicks the top row, so it ran before any quick save (the quick save becomes the
  top row).

**Item 36, window abuse (laptop).** tb1 (the laptop's own EXE), **stock route on tb1's own DLL** 3c2f47da...
(i0ship-26234371-ia; lead #2841), cp,vp,eui, client 3825x2087 (UI 1530x835 at 250 %). Two testbed jobs, quiet and
headed, one at a time, the laptop checked free (queue claims, `tb.py --host laptop status`) before each:
`20261116-155102-b377` (15:51-16:09, recording) and `20261116-161001-f1f8` (16:10-16:21, the six pairs), both rc 0
with the job's restore and `provision.py verify`.
- Three traces recorded on the posted lane behind the decoy (`hi` op of the job, ticket 0392 because realinput.py's job
  claimed as `hi-0392-w6-job` then; changed by the lead at f009d1f9 after MC #2873): **save** (6 steps: ESC, SAVE GAME,
  END + 40 BACKSPACE + type `TBLIVE-HI-PH-WIN-T8`, Save, the overwrite prompt's Yes (split step 5), RETURN TO GAME;
  checkpoint `saved`), **load** (6 steps: ESC, LOAD GAME, the top row, Load Game (split step 4), wait + ENTER, stable;
  checkpoint `loaded`), **AI turn** (the end-turn button (CHOOSE PRODUCTION opens the city screen), Warrior, SPACE,
  SPACE (split step 5: tb1 ends the turn by itself once nothing blocks), stable; checkpoint `turned`, turn 10; hand
  edit in the header: step 5's wait `state.turn>=10`, a stray SPACE after the AI turn removed).
- `window-abuse build` x6 and one job: save/alt-tab, save/minimise, load/alt-tab, load/minimise, ai-turn/alt-tab, a
  reset (the load trace without its start checkpoint, turn 10 back to 9), ai-turn/minimise. **All six completed:
  every command (replay A, journey, replay B) rc 0, every checkpoint ok, no box, no finding** (16:12:40-16:19:15).
  Recorded in the rehearsal folder with build `stock-i0ship-26234371-ia-laptop-tb1` (an unnamed build: clause 5).
- **The six pairs ran, but the window action landed 5-7 s after the split step, so it did not hit the save, the load
  or the AI turn. The rehearsal's six window-abuse items count as run, not as a test of the moment** (lead-0398,
  2026-11-16; marked `moment_tested: false` with a note in each `window-abuse.json` and in the run book; the realinput
  change is the lead's follow-up).
- **15.6's open items settled.** The job's `TESTBED_BIN` is `C:/c5-testbed/jobs/<JOB>/bin` (the runner's
  `job_env`); the pairs ship as `wa-<moment>-<act>` under it (phase.py 95f72dc7). **The window action does not land
  inside a save or a short AI turn:** the journey is a separate job command and its first action came 5-7 s after
  part A's split step for the save and the AI turn and about 14 s after it for the load. Then the save had ended
  (game menu back), the Tiny AI turn had ended (the journey read turn 10); the load screen was still up but whether the
  game was still loading was not read. To act during the operation, the split step and the window action need one
  process (a combined realinput op), not three job commands.
- Facts met: tb1's game drew nothing (flat PrintWindow, black window) until it had been in the foreground once with the
  guard off; one real move fixed it and the posted lane's pictures were good afterwards. The laptop's PrintWindow
  picture holds the back buffer in its top-left 40 % (click shot px = 2.5 x picture px). The Visual Checks entries of
  both jobs (19, `vc-20261116-155918-laptop-a698` .. `-161912-laptop-4a75`) are under issue 0392 and described as
  0398/4602's.

**Item 33, set-up options.**
- Recorded 16:31-16:46 from the main menu of a fresh launch (`rec-setup/trace.jsonl`, 46 steps, 2 checkpoints,
  check-trace clean): SINGLE PLAYER, SET UP GAME, ADVANCED SETUP, **Reset** (the set-up screens remember the last
  game's choices and the two checkboxes are toggles; Reset gives random civilization, Continents, Small, Prince,
  Standard, both off), player 1 = Catherine - Russia, Back, map size Duel, pace Standard, ADVANCED SETUP, wheel -30
  to the list's end, the Quick Combat and Quick Movement clicks on an empty point (both stay off), START GAME
  (`front_end load_screen`), `wait 20000` + ENTER (`in_game`), B (`screen_open CityView`), Warrior, Return, the
  end-turn button (`screen_open TechTree`), Pottery (`screen_closed TechTree`), three turns of SPACE, SPACE and the
  end-turn button (`state.turn>=N`), checkpoint `turn3`, the game menu by its button, SAVE GAME, the name typed
  (`HI-PH-SETUP-1`, read back), Save, LOAD GAME, the top row, Load Game, `wait 20000` + ENTER, checkpoint `loaded`
  (turn 3). A front-end step costs about 30 s (the silent channel); a completed replay took 5.9-6.6 min.
- Variants file `F:/c5-64bit-work/0398/4602/setup-variants.json` (front-end pixel hits, centre-anchored): 2 = the two
  empty-point clicks moved onto the checkboxes; 3/4/5 = Quick/Epic/Marathon; 6 = Duel (the same set-up as 1, whose
  smallest map is Duel); 7 = Huge plus the start wait lengthened to 60 s (phase.py e59e0693: a variant edit
  `{"step", "action", "ms"}` on a wait action); 8 = `absent` (the first playable ships no 43-civ variant).
- Results (each configuration on its own fresh launch, `HI-PH-SETUP-1` removed before it): **completed with turn 3
  after the load and no finding: 1, 2, 3, 4, 5, 6; 8 absent (listed "not in the candidate"); 7 not completed**
  (Huge started and played two turns, then stopped as below). Configuration 2's step pictures show both checkboxes
  on; 3's Advanced Setup shows Quick, Duel.
- **Two kinds of replay stop, 11 of 17 starts, none a product fault** (each closed in the ledger as an observation
  with its cause, read from the stop picture):
  - **First contact on a random map** (4, 5, 6 on Duel and 7 on Huge; 5 stops): a new game's map is random, and an
    AI's first-contact leader screen came up at the end of turn 1 or 2; the judge's channel is silent under a leader
    screen (7.2), so the `state.turn>=N` wait timed out. The game was fine: Goodbye (a short session) closed the
    screen, the turn had advanced. While the screen was up, `cli.py stop`'s human-turn hook refused for 300 s (rc 6;
    no override used). The leader screen's DECLARE WAR sits where the end-turn button is; no click landed on it.
  - **The first main-menu click changed nothing** (6 stops, at step 1, three of them for configuration 7): no hover on
    SINGLE PLAYER either; a 30 s settle after the menu and a move plus 2 s wait before the click (a hand edit,
    `rec-setup/trace-e1.jsonl`) did not prevent it. Four of the six launches came after a game whose leader screen had
    been closed by Goodbye and two after a replay that had itself stopped at step 1; every launch after a completed
    game passed step 1, and two after a step-1 stop did too `[I]`; cause not read.
- So a set-up trace that plays turns on a new random map does not replay reliably: the item needs either a fixed map
  (a map file in the set-up, or a fixed seed if one can be set) or turns that cannot meet an AI, before it can gate.

**The rehearsal's verdict** (19:41, `verdict.json`/`verdict.md`, notes in `verdict-notes.md`): **FAIL, clauses 1, 4,
5 (REHEARSAL, subset)**, 26 runs over 15 items, 11 findings, all observations. Clause 1: setup:7 not completed, and
the six window-abuse items only through their build; clause 4: no coverage tables for these runs (only sweeps leave
them); clause 5: the window-abuse runs are on tb1's stock DLL, not the candidate; clauses 2 and 3 hold. No product
fault was found, so nothing went through 0392's 2-of-2 pipeline or to the MC.

**Open items (4602; the lead plans (c) as a follow-up).**

- (d) Item 33 needs a fixed map (a map file, or a seed if the set-up can take one) or turns that cannot meet an AI,
  before it can gate: 5 of 17 starts met a first-contact leader screen.
- (e) The lost first main-menu click after some fresh launches (6 of 17), cause not read.
- (a) tb1 draws nothing until it has been in the foreground once with the guard off (above).
- (b) realinput.py's job claimed under the fixed 0392 ticket (fixed by the lead at f009d1f9 after MC #2873; this run's
  laptop records keep 0392).
- (c) The window action lands after the save and after a short AI turn (above): the item as built does not test
  "during" for those two moments.

### 15.9 Window abuse in one process (W10b part 2, ticket 4603) `[C]`; live part `[L]` 2026-11-17

Ticket 4603, impl-4603 (account B), 2026-11-16 20:06- EST, branch `task-4603-hi-window-abuse-one-process`. Closes the
offline half of 15.8's open item (c): the window action landed 5-7 s (save, AI turn) and about 14 s (load) after the
split step's input because part A, the journey and part B were three testbed job commands, three processes.

**What was built `[C]`.**
- `tools/hi/realinput.py window-abuse PART_A PART_B WINDOW --at STEP --moment save|load|ai-turn --ends COND --route R
  --by A --out DIR [--delay-ms 300] [--max-gap-ms 1500] [--ends-timeout 300] [--feed]` (function `window_abuse`): one
  process, one `real_lane()`, one decoy. It replays part A with `replay.run` (unchanged) while `actions.run` is wrapped
  for that replay only: the wrapper counts the inputs replay.py delivers (`split_plan`: every recorded action with
  `ok: true` that is not a wait, which is exactly what replay.py's `run_step` sends through `actions.run`; part A ends
  with the split step, so the last of them is the split step's last input). Right after that input returns, inside
  the replay's own step, it waits out `--delay-ms` from the split step's last SendInput and runs the window file's
  steps (journey format: alt-tab to the decoy or the taskbar's minimise, 5 s away, the harness's focus back); then the
  replay goes on (the split step's picture and judge reading, with the game back in front), then a read-only wait for
  the operation's end, then `replay.run` of part B. `_send` appends the clock of every SendInput while this runs
  (`_SEND_TIMES`), so the gap is measured from SendInput to SendInput (a minimise, a window call and not input, is
  timed at its `ShowWindow`).
- **What the game read at that moment.** Right after the window action's first item returned, one read-only reading
  of `--ends` (`ends_read`): `load_ready` = `frontend.probe_in_game` (one Main-state chunk; Continue shown, in game or
  paused = ended, `load_screen_loading` = running, silent = unknown); otherwise one poll of the judge's `Waiter` on the
  condition (true = ended); a busy channel (the request picked up, no answer) or a silent one is "unknown", never an
  error. After the window action, `ends_wait` reads the same until it holds (every 0.5 s, 3 s per reading, 300 s at
  most); the record keeps the readings run-length coded with their time after the first input.
- `<out>/window-abuse.json` (with `a/`, `window/`, `b/`) and one JSON line on stdout (`wa_summary`, under 1.5 kB, so
  the job's answer carries it whole). Exit: 0; 1 an input file or argument is wrong (before any input, no decoy), part
  A delivered its inputs and the window action did not start, or the gap was over `--max-gap-ms` (part B still runs);
  2 refused (fence, someone used the machine); 5/6 a replay stopped or a window expectation failed (part B is then not
  run, the harness's focus brings the game back); 8 a box (nothing more). `journey()`'s step loop is now
  `_journey_step`, shared with the window action (journey's behaviour unchanged).
- `tools/hi/phase/phase.py window-abuse build` writes `script.json` format 2 with one job command for the pair
  (`realinput.py window-abuse`, `timeout` 3000 s, `--feed`, the pair's own `--out wa-<moment>-<act>`), and new options
  `--ends`, `--delay-ms`, `--max-gap-ms`. The end read by default: `load_ready` for a load (the dropped wait,
  `front_end load_screen`, holds as soon as the load screen is up, long before the load ends), else the split step's
  dropped judge wait (4602's traces: save `screen_closed /InGame/GameMenu/SaveMenu`, AI turn `state.turn>=10`). Refused
  (exit 2): a split step without an input or with a wait action after its last input, a split step whose wait is no
  judge condition and no `--ends`, an `--ends` that one reading cannot decide (`stable`, `turn_advanced`) or that the
  judge cannot parse, a delay not below the limit.
- `window-abuse run` refuses a three-command script from before 4603 (exit 2, "rebuild it") and a pair whose
  part-a/part-b/window file is missing (exit 2); passes `--timeout 3300` to `live.py send`; reads the one-line answer
  (`wa_summary_of`) and records `gap_ms`, `moment_tested`, `moment_evidence` and `moment_why` (`wa_moment`).
  **`moment_tested` is true only when** the window action ran, its gap is within the limit, and a reading after its
  first input says the operation had not ended (`read`: the operation cannot un-end, so it was running at the first
  input), or the first readings were unanswered (busy or silent) and a later one said ended (`inferred` `[I]`: a busy
  channel during a save, a silent one during a load, taken as the game busy with the operation). A reading that says
  ended first means the action came after the operation. A run whose moment was not tested is recorded
  **incomplete** (exit 1: it does not count for clause 1); a box or a stopped replay stays a finding as before.
  `record` carries `gap_ms`, `moment_tested`, `moment_why` in the run book's summary.

**Tests `[C]`** (hermetic: fake user32 events, fake clock, fake replay that delivers each recorded input through
`actions.run`, fake decoy, focus, window facts and readings; nothing real is sent). `test_hi_realinput.py` +25: the
event order (part A, the alt-tab, part B), the gap equal to the delay (300 ms; 0 ms with `--delay-ms 0`; a minimise
timed at its window call), each input error exit 1 with nothing sent and no decoy (a missing part, `--ends` missing or
undecidable, a delay not below the limit, part A not ending at the split step, a split step without input, an unknown
moment), a failed window expectation exit 6 without part B, a box in the window action exit 8, a gap over the limit
(2,300 ms) exit 1 after part B, part A stopping before the split exit 6 with no window input, part B's stop as the
exit, someone at the machine exit 2, the CLI's one line (rc 0 and 6), the fence on the workstation for the three new
entry points and the new CLI command, `split_plan`, `check_ends`, `ends_wait`, `ends_read`'s unknowns.
`test_hi_phase.py`: the window-abuse block rewritten and extended (10 tests to 33): one command, the default ends, the refusals,
the old script and a missing part refused, `moment_tested` for read, inferred, ended, an error reading, no reading, a
gap over the limit, a box, a stopped replay, no answer line, and `record` of a tested and an untested run.

**Quiet submit length (2026-11-17 03:58 EST, lead-0398, ticket 0481 / MC #3655) `[C]`.** `tb.py submit --quiet` now
refuses a job without `--expect-min N` (its planned length in minutes). `window-abuse build` writes `--expect-min`
into `script.json`'s submit line (default 45: 4602's six pairs took 7 min of one job, 4603's job had a 2700 s timeout;
option `--expect-min`, 0 or less refused); test_hi_phase.py pins both. Scripts built before this date carry a submit
line without it: rebuild them, or add the option by hand.

**The live part (item 4, 2026-11-17).** Replaces the line "Not verified (the live part, item 4, waits for a laptop window, D163)" (lead-0398, 2026-11-17 05:02 EST): the load and the AI turn are now tested live, the save is not.

Item 4, impl-4603 (account B), 2026-11-17 04:33-04:56 EST, in the laptop window of MC #3262 (WMC grant).

**Setting.** One headed quiet testbed job on the laptop, `20261116-232704-5dbc` (submitted 2026-11-16 23:27, queued
behind the M job 20261116-223507-e8da, started 04:33:32, ended 04:55:45, rc 0; `realinput.py job --ticket 4603 --load
HI-PH-WIN-T8 --idle 1200 --max 2100`, tb.py `--timeout 2700`; HEAD 51779fe9 of task-4603-hi-window-abuse-live, which
carries items 1-3 at a21cae96). tb1, the laptop's own EXE, **stock route on tb1's own DLL** (3c2f47da...,
i0ship-26234371-ia), cp,vp,eui, client 3825x2087; save HI-PH-WIN-T8 (b2ceb663...). The six pairs built by
`phase.py window-abuse build` (format 2, one job command each; delay 300 ms, limit 1500 ms) from 4602's three laptop
traces (copied to `F:/c5-64bit-work/0398/4603/wa-traces`, hashes equal to 4602's): save split step 5 (ends `screen_closed
/InGame/GameMenu/SaveMenu`), load split step 4 (ends `load_ready`), AI turn split step 5 (ends `state.turn>=10`); 4602's
reset trace (the load trace without its start checkpoint) between the two AI-turn pairs. Records:
`F:/c5-64bit-work/0398/4603/wa/wa-<moment>-<act>/window-abuse.json` (the host's own records and pictures in
`F:/c5-64bit-work/0398/4603/laptop-5dbc/out/live/wa-*`); run book entries in
`F:/c5-64bit-work/0398/phase/rehearsal-x64-04b1211b/runbook.json`, build `stock-i0ship-26234371-ia-laptop-tb1`
(unnamed: clause 5, as 4602). Scripts in `F:/c5-64bit-work/0398/4603/scripts/` (`run_wa2.sh`: the pairs, a read-only
`live.py frontend JOB ingame` box check after each, `phase.py record`).

**Results `[L]`** (attempt 2, 04:43-04:51; every command rc 0, part A, the window action and part B exit 0, every
checkpoint ok, no box after any pair, no finding):

| pair | gap_ms | first input | first reading after it | operation ended at | moment_tested |
|---|---|---|---|---|---|
| save / alt-tab | 328 | SendInput (ALT+TAB) | 234 ms: SaveMenu closed (ended) | before 234 ms | **false** |
| save / minimise | 296 | window call (ShowWindow) | 1,250 ms: closed (ended) | before 1,250 ms | **false** |
| load / alt-tab | 656 | SendInput | 3,204 ms: Lua channel silent | 20,235 ms (`load_screen_ready`) | **true, inferred** |
| load / minimise | 312 | window call | 8,875 ms: silent | 21,719 ms | **true, inferred** |
| AI turn / alt-tab | 359 | SendInput | 578 ms: turn 9, not ended (read) | by 12,625 ms (turn 10) | **true, read** |
| AI turn / minimise | 297 | window call | 1,328 ms: turn 9 (read) | 23,547 ms (still 9 at 12,984) | **true, read** |

- **The one-process op closes 15.8 (c) for the load and the AI turn:** the window action now comes 0.3-0.66 s after
  the split step's input (was 5-14 s with three job commands), and the record shows the load (channel silent, the load
  screen ready 20-22 s later) and the Tiny AI turn (turn still 9 at 0.6 and 1.3 s, turn 10 later) still running when the
  game was alt-tabbed away or minimised. The game came back, part B's checkpoints held (`loaded`, `turned` turn 10).
- **The save moment is still not tested `[L]`.** The Tiny save's end condition (`screen_closed
  /InGame/GameMenu/SaveMenu`) already held at the first reading, 234 ms after an alt-tab's first input (about 0.56 s
  after the overwrite prompt's Yes). A second try of both save pairs with `--delay-ms 0` (after a reset, 04:52-04:55,
  pairs built as `wa-save-<act>-d0` on the shipped part files, own out folders): minimise gap 0 ms, read ended at 1,516
  ms; alt-tab gap **359 ms even at delay 0** (the alt-tab's first SendInput comes about 0.33-0.36 s after the split
  step whatever the delay `[I]`: overhead in the window action before its key, not read), read ended at 234 ms. So
  either a Tiny save ends within about 0.3-0.5 s on the laptop, or the SaveMenu closes when the save starts rather than
  when it ends `[I]`, which one is not read. To test "during a save" needs a save that lasts (a larger map's save) or
  an end condition that marks the save's end; the minimise at 0 ms is the closest the lane gets.
- **tb1 drew nothing again at the job's start `[L]`** (15.8 (a) confirmed on a second job): attempt 1 (04:36-04:38)
  had all six pairs refused at part A's session start ("no usable picture: flat frame (std 0.6)", 0 inputs sent; the
  reset replay was also refused: `realinput.py replay` needs `--ticket 4603` when the trace header names 0392). Focus
  alone did not fix it (`realinput focus` answered "already"; decoy show + focus + decoy hide: still mean 0.01, std
  0.52). **One real move fixed it:** `live.py send JOB '{"op": "realinput", "args": ["act", "{\"a\": \"move\", \"x\":
  1900, \"y\": 1000}"]}'` (the act needs client `x`/`y`; `fx`/`fy` is refused), then PrintWindow mean 23.9, std 62.8.
  A window-abuse (or any real-input) job on tb1 should start with that move.
- **Readings.** During a load the Lua channel was silent (as 15.9 expected), during a Tiny AI turn the judge read turn
  9 in 141-421 ms, during and after a save the judge read the menu closed in 78-156 ms. No reading was "busy".
- **Stop.** `live.py stop` 04:55:11; the job's `cli.py stop` rc 0, `frontend.py restore` rc 0, the job's own save copy
  removed, `provision.py verify` rc 0 (`restore_ok: true`); the job ended `done` rc 0 at 04:55:45 (quiet summary:
  alone, undisturbed false: the owner's session interactive, as 4602).
- **Visual Checks.** 18 entries under issue 4603 pulled from the laptop (`visualcheck.py pull --host laptop`,
  vc-...-laptop, 04:43-04:54): the replay checkpoints of parts A and B and the reset (start, saved, loaded, turned),
  each described by the replay as "DIAGNOSTIC (HI posted lane): replay checkpoint <name> (trace, step)", PASS; the
  label says "posted lane" although these are real-lane replays (an observation for realinput.py's feed text).
  Picture checked: `wa-ai-turn-alt-tab/b/checkpoint_0002_turned.png` shows the game at turn 10 with the interface, in
  the top-left 40 % of the PrintWindow frame (15.8). `visualcheck.py list --issue 4603 --result undescribed` lists
  none. Four PrintWindow driving-aid shots (the flat-picture check, no `what`) are not recorded.
- **Run book.** The first attempt's five entries (04:36-04:37, incomplete, no gap) carry a note and the path of their
  moved record (`wa/attempt1-0436/`); the latest entry per item is attempt 2 (load and AI turn: completed,
  moment_tested true) or the delay-0 run (save: incomplete, moment_tested false). Not a candidate run (clause 5).
- **Privacy.** The kept traces and records name the host only as `laptop`; no machine name found.

**Checks** (2026-11-16 20:24-20:27 EST, under quietlock): `python -I perf/quietlock/quietlock.py run --who 0398 --what ... -- python -I tools/harness/run_tests.py -- -k "test_hi or safety_static or campaign"`: harness 862 passed, 0 failed, 2 skipped (both older and unrelated: `test_hi_actions.py:270` W2's deny list covered elsewhere, `test_hi_actions_live.py:111` live opt-in); instances 110 ran, 0 failed.

### 15.10 Item 33 on a fixed set-up, and the lost first click (W10b part 3, ticket 4604) `[L]`

Ticket 4604, impl-4604 (account B), 2026-11-16 20:06- EST. Closes 15.8's open items (d) and (e). Setting as 15.8
(the first playable `x64-04b1211b-pfdee656` laid on c5h-c by a copy of 4602's `prep.py`: x64 DLL 29f58e17..., server
5b254430..., proxy 75c40cec..., purity 6a0d7b6b..., `pack=1`, i0ship-26234371-ia as the 32-bit DLL; product route,
`cp,vp,eui`, width 1280, client 1914x1051, PrintWindow; claimed by impl-4604, ticket 0398, assert policy hold). Records
under `F:/c5-64bit-work/0398/4604/` (`runs/`, `setup/`, `rec-setup/`, `bak/`); scratch scripts in
`C:/c5-64bit-work/0398/4604/` (not committed; copied to `F:/c5-64bit-work/0398/4604/scripts/` at the hand-back).

**(d) The fixed set-up: the map seeds, a test fixture.**
- The front end offers no seed: the set-up screens (`AdvancedSetup`, `GameSetupScreen`) set none, and the only seed
  call in the game's Lua is the read `Network.GetMapRandSeed()` in `InGame/DebugMenu.lua` [C]. The pregame reads
  `config.ini` `[CONFIG]` `MapRandSeed` and `SyncRandSeed` in `CvPreGame::loadFromIni` when the DLL loads (0 = the
  clock); the proxy forwards that call with a remote parser (`proxy/src/proxy_hooks_human.cpp`,
  `ProxyHook_ICvPreGame2_loadFromIni`) [C], and civ5-headless found on 2026-10-25 that the keys pin a menu-started
  game with the stock DLL (`research/sources/civ5-headless/history/host-ledger.md`).
- **The product route honours them** [L]: with 1001/2002 two fresh launches of 4602's configuration-1 trace (x01
  20:18, x02 20:31) showed the same turn-0 map and start, and both met the same first contact, Pedro II of Brazil's
  leader screen, in step 28's turn-1 wait. So the AI's turn repeats too, and seed 1001 is unusable.
- **Seeds 3/3** met no AI in the three turns on Duel (configuration 1, 20:43, a scouting copy with 60 s turn waits) and
  on Huge (configuration 7, 21:13, 120 s waits), each at the first try.
- **The stock route honours them too** [L]: configuration 1 on 3/3 with `vpproxy.ini` `mode=off` (the proxy loads
  i0ship-26234371-ia; the ini backed up fbe61a65... and restored by hash) completed (21:31-21:40), and its turn-0 map
  and start equal the product recording's (Visual Checks vc-20261116-214212).
- The fixture: before each configuration's launch the owner sets both keys to the configuration's `seeds` in the
  variants file (a field phase.py ignores; `[3, 3]` for 1-7), `config.ini` backed up by SHA-256 by the prep and restored
  by hash at the end. A player never sets them; the variants file's `fixture` text says so.
- **Recorded again:** configuration 1 on 3/3, 21:00-21:12, step for step as 4602's
  (`F:/c5-64bit-work/0398/4604/rec-setup/trace.jsonl`, 46 steps, 2 checkpoints `turn3` and `loaded` with turn and
  screens, check-trace clean, 698 s), by a script that replays 4602's actions and waits as live steps
  (`rerecord.py`). Variants file `F:/c5-64bit-work/0398/4604/setup-variants.json`: 4602's edits on the same step
  numbers, plus `seeds` per configuration.
- **Results** (`full.sh`/`run_all.py`: per configuration the game stopped with the human-turn hook, `HI-PH-SETUP-1`
  removed, the seeds set, a fresh launch to the ready menu, `phase.py options ... --ticket 4604`, `phase.py record` into
  `F:/c5-64bit-work/0398/phase/rehearsal-x64-04b1211b/`; runs in `F:/c5-64bit-work/0398/4604/setup/config-N-rK/`):
  configuration 8 `absent` (recorded 21:41); round 1 21:41-22:55 and round 2 22:55-23:59: **configurations 1-6
  completed 2 of 2** (each with turn 3 after the load, no finding, no box; 5.9-7.7 min a replay). **Configuration 7
  (Huge)**: round 1 stopped at step 45, a trace timing: after Load Game the Huge save was still loading when the
  trace's `wait 20000` + ENTER came, so ENTER was lost, the load screen stayed on Continue Your Journey and `in_game`
  reached its 300 s deadline (Visual Checks vc-20261116-225731). The variant now also lengthens step 45's wait to 60 s
  (`{"step": 45, "action": 0, "ms": 60000}`, 22:58); with it round 2 (23:47-23:59, 8.2 min) and round 3 (23:59-00:09)
  completed: **configuration 7 completed 2 of 2 on the corrected variant**. So item 33's eight configurations hold on
  the fixed set-up: 1-7 two completed runs each, 8 `absent`. No first contact in any of the 15 configuration runs on
  3/3, nor in the scouting and stock runs (18 starts). No product finding: the only stops were 4602's trace on seed 1001
  (a first contact, x01/x02), the two deliberate clicks inside the activation, and configuration 7's round-1 load wait.
- The instance was restored by hash (`prep.py restore`: every user `.ini` incl. `config.ini` with the seeds back to 0,
  the quick and auto save folders, the DLL, the proxy layout removed, the front end original; `provision.py verify
  c5h-c` rc 0, 2026-11-17 00:10) and released 00:11.

**(e) The lost first main-menu click: the front end's mod activation.**
- With `frontend.py prepare --mods`, the patched `MainMenu.lua` enables and activates the mod set from the main menu
  (`Modding.ActivateEnabledMods()`, which rebuilds the game database, then `SystemUpdateUI(RestoreUI, "MainMenu")`).
  Lua.log (stamps are seconds since boot, checked against the file time) shows per launch: `Initializing Lua`; 40-53 s
  later `vp-dll-dev: activating 3 mods`; 34-56 s later `vp-dll-dev: requested mods are already active` (the next menu
  pass), i.e. 76-109 s after Lua starts (21 launches 20:18-23:59) [L].
- **The menu picture reads `main_menu` before and through the activation**: in 7 launches the judge's first
  `main_menu` (polls 15 s apart, so an upper bound) came between 15 s before and 1 s after `activating`, i.e. 35-56 s
  before the activation's end, and every poll during the activation read `main_menu` too. 4602's launcher returned at the first `main_menu` after 60 s
  (15 s polls), so its replay's first click came at about 70-110 s after the launch, inside or near the end of the
  activation [I: 4602's own Lua.logs are overwritten, the timing is from its prep.log].
- **Reproduced on purpose, 2 of 2** (`runs/during-1-2124`, `during-2-2127`): the replay started as soon as Lua.log
  showed `activating` without `already active`; step 1's click on SINGLE PLAYER came 26.5 and 25.1 s before `already
  active`, nothing opened, no hover, and the wait `front_end single_player` reached its 30 s deadline reading
  `main_menu`: 4602's symptom exactly. At that moment `IsHungAppWindow` was true for the game's `WinClass_FXS` window
  (its thread does not pump while the database is rebuilt); the window was visible, enabled, not iconic, not the
  foreground, and `GetGUIThreadInfo` named no active or focus window, as at every passing click.
- **Every click after the activation landed: 20 of 20** launches whose first click came 7-33 s after `already active`
  (x01 +7.1 s and x02 +10.9 s on 4602's own 60 s rule, the recording +18.9 s, the scouting, stock and 14 configuration
  runs +16 to +33 s); 0 of 2 inside it. Per-run table: scratch `summary.py`. x02 came right after a game closed by Goodbye, so a Goodbye is not the cause; 4602's 4-of-4 lost clicks after
  Goodbye are not explained by it (timing variance, `[I]`).
- **The fix (the owner's launcher, not the trace):** wait until the current launch's Lua.log has `requested mods are
  already active` after `activating` and the judge reads `main_menu`, then 5 s. In the scratch `prep.py ready`. A
  harness fix belongs in `tools/harness/frontend.py` (a `menu-ready` read) or in the phase's run book; proposed to
  lead-0398. **Made 2026-11-17** (third lead-0398, MC #3326): `frontend.menu_ready`, below.

**The harness read `frontend.menu_ready` `[C]`** (2026-11-17 00:31 EST, third lead-0398, granted by the MC as #3326
on 4604's proposal #3324; `tools/harness/frontend.py`, test `tools/harness/tests/test_frontend_menu_ready.py`).
- `frontend.menu_ready(inst, timeout=240, poll=2, settle_s=5, since=None, menu_fn=None)`, CLI
  `python -I tools/harness/frontend.py -i <inst> menu-ready [--timeout S] [--settle S]` (exit 0 ready, 1 not within
  the timeout, 2 refused). Ready = the current launch's Lua.log has, after its last activation line (`activating`,
  `enabled`, `disabled`), `requested mods are already active` (or `none of the requested mods are installed`; the
  markers are `turns.FE_ACTIVATING` / `FE_ACTIVE`); when no mods were asked (no `prepare` state, or an empty
  `modsToEnable`) a current Lua.log is enough; then `menu_fn()` true when the caller gives one (e.g. the HI judge's
  `main_menu`; frontend.py does not import tools/hi); then `settle_s` (5 s, 4604's rule).
- **Which launch:** `since` = the launch's process creation time (default the instance state's `create_time`, which
  launch.py records; unknown is refused, exit 2). Lua.log counts only when its mtime is at or after `since`; the game
  truncates Lua.log when its Lua starts (about 28 s after the process, 1114), so before that the file is the previous
  launch's and none of its lines is read or quoted. The same rule as `turns.frontend_state`.
- **Fails loud** (`FrontendError`, D118's spirit): on the timeout with the reason, the last menu reading and the last 12
  lines of this launch's Lua.log ("none of this launch" when the file is older); at once when this launch's Lua.log
  shows a load or a game (`turns.FE_GAME` / `FE_GAME_CONTEXTS`: MainMenu re-executes during a load and prints
  `already active` again, so the read is for a fresh launch's menu only, not the menu after a played game).
- Tests (hermetic: a temporary Lua.log whose lines and mtime a fake clock writes): 14, incl. an older launch's
  `already active` never read before the truncation, the timeout's message with exactly the last lines, a second
  activation, no mods asked, the menu reading, a game refused at once, the CLI's three exits. Checks on lead3-0398
  (2026-11-17 00:30 EST): `test_frontend_*.py` 63 passed (49 older + 14 new); `test_safety_static.py` 55 passed;
  under quietlock `run_tests.py -k "test_frontend or safety_static or test_turns or stop_main_menu"` harness 207
  passed, instances 110 ran, 0 failed.
- **Not verified:** a live launch (`[L]` pending: the first phase's item 33 uses it, PHASE.md 8 step 5).

**Open items.**
- Done 2026-11-17: the `menu-ready` read (above) and the phase's run book for item 33 (PHASE.md 8 step 5 names the seed
  fixture and the read). `phase.py options` itself needs no change: it already ignores the `seeds` and `fixture`
  fields. (Was: "proposed to lead-0398, not made".)
- Seeds 3/3 are proven for the first playable's map scripts and VP version only; a candidate that changes the map
  generation, the AI's opening or the default set-up (Reset's choices) may meet an AI again: the owner re-scouts with
  the scouting copy (turn waits of 60-120 s) and changes `seeds` in the variants file.
- Whether `SyncRandSeed` matters at all for VP (civ5-headless found VP ignores the sync seed for its ensembles) was not
  tested; both are pinned.

### 15.11 The kit's proofs in `phase.py record` (W10b part 4, ticket 4605) `[C]`

Ticket 4605, impl-4603 (account B), 2026-11-16 20:29-20:40 EST, branch `task-4603-hi-window-abuse-one-process`.
Closes 14.8's "Nothing yet calls them as part of a phase run" for `phase.py record` (proofs 2 and 3 of PHASE.md 4.2;
proof 1, the static scan, is `test_hi_kit.py`'s).

**Where the proofs are read (settled, PHASE.md 4.2).** `kit.py make`, `kit.py fingerprint` and `hi.py session start`
all write into the instance's own `<instance>/run/hi/`, which the next session or `make` overwrites, and no copy of a
derived save's `situation.json` is kept beside the save (the 24 saves in `F:/c5-64bit-work/0398/kit/saves/` have none,
checked 2026-11-16 20:30). So a run carries its own copies in `<RUN>/hi/` (RUN = `record --run`: a session's folder; a
sweep's folder, for its first segment's session; each session's folder of a campaign record): `situation.json`
(written by `kit.py make`), `session.json` (the instance's as the session started), `kit-fingerprint-load.json` and
`kit-fingerprint-start.json` (`kit.py fingerprint --at load|start`). The phase's instance preparation for a run from a
derived save: load it, `fingerprint --at load`, start the session, `fingerprint --at start`, copy the four files.

**What `record` does `[C]`.** For a sweep and a charter session (and each session of a campaign record) whose start
save, matched by the trace header's SHA-256 among the item's situations, is a derived save (a `recipe` in `kit.json`;
the plan's resolved saves now carry `derived`, and a plan from before 4605 reads it from its kit), `record` imports
`tools/hi/kit/kit.py` by its file path and applies its own `check_order` (the session started after `set_up_ended`,
from the derived save's SHA-256 in the trace header) and `prove_live` (the two fingerprints equal) to `<RUN>/hi/`, and
also checks that `situation.json`'s `derived.sha256` is the situation's save. A run that fails one, or lacks a file,
is recorded **incomplete** with the reason ("the kit's proofs (PHASE.md 4.2): ..."; a missing file is named with the
command that writes it), so clause 1 fails on it; the run-book entry keeps `kit_proofs` (`ok`, `derived`,
`problems`, `order`, `live`). A **plain era save** needs neither proof: no recipe ran, so there is no set-up to order,
and its start state is proven by its SHA-256 in the trace header (`_save_check`, unchanged); its fingerprints are
checked only when the owner wrote both (a difference means something changed the game between the load and the
session's start, so it fails too).

**Not covered `[C]`.** The replay kinds (kept, loads, setup, window-abuse) are not checked: they start from the
owner's load without a set-up record. A sweep's later segments (each after the sweeper's own reload of the same save)
carry no fingerprints of their own. Both would need `kit.py fingerprint` calls in `sweep.py` and at the replay's start,
and `kit.py make` could keep each `situation.json` beside its derived save (`<save>.situation.json`); none of these
files is in 4605's touches (for lead-0398 to plan).

**Tests `[C]`** (`test_hi_phase.py` +13, a fake kit with a derived `mid-game` and fake `<RUN>/hi/` files): both proofs
passing for a session and a sweep (CLI `record` rc 0, `completed`); each failing (a session started before the set-up
ended, another save's hash in the header, a fingerprint one unit apart) recorded `incomplete` with its reason and
clause 1 failing in the verdict (exit 6); a set-up record of another save; each of the four files missing, named with
its command; no `hi/` at all; a plain save with no proofs (`completed`), with unequal fingerprints (`incomplete`) and
with equal ones (`completed`); a plan from before 4605; and a spy showing that `kit.py`'s own `check_order` and
`prove_live` run.

**Checks** (2026-11-16 20:35-20:37 EST, under quietlock): `python -I perf/quietlock/quietlock.py run --who 0398 --what ... -- python -I tools/harness/run_tests.py -- -k "test_hi or safety_static or campaign"` (includes `test_hi_phase.py`, `test_hi_kit.py`, `test_safety_static.py`): harness 875 passed, 0 failed, 2 skipped (both older and unrelated: `test_hi_actions.py:270`, `test_hi_actions_live.py:111` live opt-in); instances 110 ran, 0 failed.

### 15.12 The kit's proofs in real phase runs (W10b part 5, ticket 4606) `[C]`, records `[L]` on F:

Ticket 4606, impl-4603 (account B), 2026-11-16 20:45- EST, branch `task-4606-hi-kit-proofs-in-runs`. Closes 15.11's
"Not covered" for sweeps and charter sessions: since 4605 every sweep and charter session from a derived save failed
clause 1 for want of `<RUN>/hi/` files that nothing wrote. MC #3125 granted the `sweep.py` touch (and its test); the
fingerprints stay read-only through `judge_lua.checked`, as `kit.py` takes them.

**1. Set-up records beside the derived saves.**
- `kit.py make` now also writes the set-up record beside the derived save as `<save>.situation.json`
  (`kit.record_path`), the same JSON as the instance's `run/hi/situation.json`, and puts `record: {"path",
  "sha256"}` into the situation's `kit.json` entry. `kit.py verify` (offline) checks it: a derived made save without
  `record` in `kit.json`, a missing file or another hash is a problem.
- `kit.py records [--situation S] [--dry-run]` writes the missing records for saves made before 4606:
  `rebuilt_record` from `kit.json` (`derived_from` and the base's save and hash, the recipe steps with params and
  `results`, `made` as `set_up_ended`, the identity, the derived save), marked `"rebuilt_from": "kit.json"` and
  `set_up_started: null`. It never replaces a file that is there (a file with another hash than `kit.json`'s is
  reported, exit 1).
- **Done on F: `[L]`** (2026-11-16 20:54 EST): `near-victory`'s original record still stood in c5h-h's
  `run/hi/situation.json` (the last `make` there, 14.4; set-up ended 18:12:31, derived c648e6e4...): copied unchanged
  beside its save (SHA-256 5683007d...), so it is **not** rebuilt. The other 14 were rebuilt by `python -I
  tools/hi/kit/kit.py records` (output `F:/c5-64bit-work/0398/4606/records.json`). All 15 hashes are in `kit.json`.
  `python -I tools/hi/kit/kit.py verify` afterwards: 35 of 35 situations ok, every save's SHA-256 unchanged
  (`F:/c5-64bit-work/0398/4606/verify-offline.json`). No save was changed, moved or made again.

**2. Fingerprints by the tools that start the sessions.**
- `kit.py`: `kit_save_by_sha(sha)` (the kit situation of a save by its hash; a situation with `same_save_as` loses
  to the one it names) and `write_proofs(dest_hi, kit_save, fp_load, fp_start, session_file, ...)`, which writes both
  fingerprints, copies `session.json` and, for a derived save, the set-up record from beside the save after checking
  it against `kit.json`'s hash. A problem is returned, never raised.
- `sweep.py`: `LiveOps.kit_save` and `LiveOps.fingerprint` (`kit.Live(...).fingerprint()`, the FINGERPRINT chunk
  through `judge_lua.checked` before it is sent; a writing chunk is refused with exit 2 and nothing is sent, tested).
  The sweeper looks the save up once by its SHA-256. For a kit save it takes the load fingerprint in `fresh()` after
  the launch, load or reload and the box check, before the scene set-up, outside any session. Right after each
  `session start` it takes the start fingerprint and writes the four files into that session's `s<NN>/hi/`. A
  session started after a set-up opening (no load of its own) is compared with its segment's load, so it proves that
  the game is still the save's. The session's row in `sweep.json` keeps `proofs` (`problems`, `same`, `derived`). A
  fingerprint that fails is a problem in that row, and the sweep goes on. The scene set-up (`SETUP_CHUNK`: units
  asleep, nothing selected, auto unit cycle off) changes none of the fingerprint's fields (turn, units, cities, gold,
  era, wars) `[C]`, so load and start must be equal. Ops without `kit_save` (every older fake) take no proofs.
- `campaign.py proofs CHARTER -i NAME --out OUT --at load|start [--wait 900]`, because a charter's session is started
  by its runner and not by `campaign.py`. With `--at load`, right after the owner loaded the charter's save, it
  refuses with exit 2 while a session is open and writes `OUT/hi/kit-fingerprint-load.json`. With `--at start`, run
  in the background before the prompt goes to the runner, it polls the instance's `session.json` every 0.5 s until a
  session writing to `OUT` is open, then takes the start fingerprint at once and calls `kit.write_proofs`. Its exits:
  1 on a problem; 2 without the load fingerprint; 1 when no session started within `--wait`. A save outside the kit
  needs nothing (exit 0).
- `phase.py record`: a sweep's proofs are now read from every session's own `hi/` (the trace's folder), problems
  prefixed with the session folder (`s02: ...`); a run-book entry keeps `kit_proofs.sessions`. A rebuilt set-up record
  counts and is marked (`kit_proofs.rebuilt`). (4605's reading of one `hi/` in the sweep folder is replaced.)

**3. An absent sweep (MC #3091).** `phase.py absent PHASE_DIR --item sweep:43-civ:product --reason TEXT --ref
MC#3091` records a completed run-book entry on the candidate (`kind: absent`, why "not in the candidate: TEXT
(MC#3091)", no traces), which clause 1 lists under its notes as setup:8's is. Only a product sweep can be absent; a
stock sweep, any other item, a blank reason, a reference not of the form `MC#N` and an unknown item are refused
(exit 2, nothing recorded). The ledger and the coverage skip an absent entry (it has no folder).

**Tests `[C]`** (hermetic; under quietlock below): `test_hi_kit.py` +11 (the record beside the save from `make`,
`records` rebuilt / dry run / kept / a changed file exit 1 and untouched, `write_record` never replaces, the
rebuild's refusals, the offline check of the record, the committed kit has a record for every made derived save,
`kit_save_by_sha`, `write_proofs` derived and plain, its problems recorded); `test_hi_sweep.py` +6 (every session's
four files with load before set-up outside the session and start inside it, the set-up opening's session against the
segment's load, a failing fingerprint recorded and the sweep going on, a save outside the kit, `LiveOps.fingerprint`
is kit's checked chunk and a writing one is refused before sending, `LiveOps.kit_save` on the committed kit);
`test_campaign.py` +5 (load then start, outside the kit, a session open at load, start without the load or without a
session in time, the CLI's exit 1 on a problem and on a bad `--at`); `test_hi_phase.py` +9 (a sweep needs the proofs
of every session, a rebuilt record counts, an absent product sweep passes clause 1 with its note, five refusals, the
ledger skips it).

**Not verified.** No live sweep or runner session with the hooks yet (the first is the phase's own); whether the
fingerprint read costs the sweep noticeable time per session (one chunk, about a second on the Lua channel `[I]`).
The replay kinds (kept, loads, setup, window-abuse) still take no proofs (as 15.11).

**Checks** (2026-11-16 21:00-21:05 EST, under quietlock): `python -I perf/quietlock/quietlock.py run --who 0398 --what ... -- python -I tools/harness/run_tests.py -- -k "test_hi or safety_static or campaign"` (includes `test_hi_kit.py`, `test_hi_sweep.py`, `test_hi_phase.py`, `test_safety_static.py`): harness 901 passed, 0 failed, 2 skipped (both older and unrelated: `test_hi_actions.py:270`, `test_hi_actions_live.py:111` live opt-in); instances 110 ran, 0 failed. `python -I -m pytest tools/hi/charters/test_campaign.py -q -p no:cacheprovider` under quietlock: 59 passed.

### 15.13 One rehearsal sweep of a derived save with the kit's proofs live (W10b part 6, ticket 4607) `[L]`

Ticket 4607, impl-4603 (account B), 2026-11-16 21:08- EST, branch `task-4607-hi-rehearsal-sweep-proofs`.

**Setting.** c5h-a (C:, claimed 21:12 by impl-4603, ticket 0398, assert policy hold). The first playable
`x64-04b1211b-pfdee656` laid as 14.7 lays it (scratch `C:/c5-64bit-work/0398/4607/prep.py`, a copy of
`C:/c5-64bit-work/0398/w9prod/prep.py` 3ad91287... with this ticket's paths and a `stock` mode): `set-dll
i0ship-26234371-ia` (the pin, 3c2f47da...; already the instance's), a test pack from `pack.py`, `set-proxy`,
`vpproxy.ini pack=1`; laid hashes x64 DLL 29f58e17..., server 5b254430..., proxy 75c40cec..., purity 6a0d7b6b...
(as 14.7); WindowRes 1920x1080 (D158), SinglePlayerAutoEndTurnEnabled 0; `HI-KIT-ALL-MET-429ABBA4` copied in by
SHA-256. Every changed file backed up by SHA-256 first. Mods `cp,vp,eui`, product route.

**Commands.**
- `python -I tools/hi/sweep.py -i c5h-a --by impl-4603 run --ticket 0398 --route product --save
  HI-KIT-ALL-MET-429ABBA4 --out F:/c5-64bit-work/0398/4607/sweep-product --mods cp,vp,eui --launch` (21:15-).
- The stock control after `prep.py restore` and `prep.py prep c5h-a stock` (the pin's DLL, no `vpproxy.ini`): the
  same with `--route stock --out F:/c5-64bit-work/0398/4607/sweep-stock`; then `sweep.py compare`.
- `phase.py plan --candidate x64-04b1211b-pfdee656 --rehearsal --only sweep --situations all-met --out
  F:/c5-64bit-work/0398/phase/rehearsal-4607-all-met` (2 items, both `derived: true`), then `phase.py record` of
  each sweep and `phase.py verdict --no-coverage`.

**Expected.** Every session's `s<NN>/hi/` with the four files, `proofs.same` true (the scene set-up changes no
fingerprint field), the set-up record the rebuilt one (`rebuilt_from: kit.json`, 15.12); both sweeps `completed`
with `kit_proofs.ok`.

**The product sweep `[L]`** (21:14:50-22:07:09, **52.3 min**, inside 0392's 50-65): all five default screens
`done` (top panel 16 of 39 shown controls located, unit panel 4 of 38, city screen 28 of 96, tech tree 2 of 4, game
menu 10 of 25, 7 + 7 of its clicks barred by the never-list), 10 sessions, 1 launch, 7 reloads, **0 findings**, exit 0.
Launch: in game with the save loaded by 21:17:38 (the launch began 21:15:17; the guard's self-test ok, 17/17 hooks).
- **The proofs, live:** every session's `s<NN>/hi/` holds the four files; `proofs.problems` empty and `same` true in
  all 10; the load and start fingerprints are all **61c12bd1...**, the value 14.7 read from this save on both routes.
  The set-up record is the rebuilt one (`rebuilt_from: kit.json`, set-up ended 16:18:42).
- Sessions 6 and 8 began after a set-up opening (the city screen opened by `UI.DoSelectCityAtPlot`, no load of their
  own): their load fingerprint is their segment's (21:33:19 for s05 and s06, 21:54:29 for s07 and s08), and it
  equals the start one, so the game was still the save's after the earlier session's restores `[L]`.
- **Fingerprint time:** this run kept only second-resolution times. The load fingerprint is written 1-2 s before the
  session's start record (load fingerprint, scene set-up, `session start` with its picture), and the start
  fingerprint within the same second as the session's start record (all 10) `[L]`. `sweep.py` now keeps each read's
  `took_ms` (this ticket; the stock sweep below has them).
- **`phase.py record`** of `sweep:all-met:product` on build `x64-04b1211b-pfdee656`: **completed**,
  `kit_proofs.ok` true, `derived` true, `order.ok` and `live.ok` true for s01-s10, `rebuilt: kit.json`.
- Stopped with `cli.py stop --human-turn-hook` at 22:07:54 (no autosave written during the run: the newest autosave
  was 21:00:27, from before the claim), restored by hash at 22:08:20 (`restore: OK`, front end original,
  `provision.py verify` rc 0).

**The stock control `[L]`** (`prep.py prep c5h-a stock`: the pin DLL 3c2f47da..., no `vpproxy.ini`, the same
settings and save; 22:08:53-23:01:15, 52.4 min of wall clock with a stop of about 4 min inside): all five screens
`done` (top panel 16 of 39, unit panel 4 of 35, city screen 27 of 96, tech tree 2 of 4, game menu 10 of 25), 10
sessions, 8 reloads, **0 findings**, exit 0. `sweep.py compare`: no rows (no finding on either route).
- **A stop, and a sweep.py fix `[L]`/`[C]`.** At 22:27:23, after the fourth reload, `session start` found only
  flat grey PrintWindow frames (std 0, 2 retakes) and refused (exit 3); the game was in game, no box. The
  `SessionError` was not caught by `Sweeper.run()`, whose `finally` then wrote `ended` with no `stopped`: **phase.py
  would have recorded the stopped sweep as completed.** Fixed in `sweep.py` (3691da63): any other exception records
  `stopped` (and is raised as before); a later resume that runs to the end clears it; and a start without a usable
  picture is tried once more after 5 s (`start_retries` in the record). The sweep was resumed with `--resume` at
  22:31 with the fixed sweeper and ran to its end (no retry was needed then). One refused start in 11 session starts on
  stock (4 before it, 6 after the resume); on the product route none of 10 `[L]`. Research 4.7 found PrintWindow good on stock in 20 of 20
  in-game shots, so a flat start right after a reload is rare there `[I]`.
- **The proofs:** the 10 sessions' `hi/` hold the four files, `same` true, all fingerprints 61c12bd1... (the same as
  the product route: the game is the save's on both routes). Sessions 6 and 8 again took their segment's load.
- **Fingerprint time** (`took_ms`, new): load 31-141 ms (median 125), start 46-141 ms (median 70), 10 each. The start
  fingerprint is written in the same second as the session's start record in 8 of 10, the next second in 2 `[L]`. So
  the proofs cost about 0.2 s per session, about 2 s of a 52-minute sweep.
- `phase.py record` of `sweep:all-met:stock` on the candidate's id: **completed**, `kit_proofs.ok`, 10 sessions with
  `order.ok` and `live.ok`. (A first record under a stock build id `stock-i0ship-26234371-ia` was marked
  `unnamed_build`, as 4602's laptop runs were: in a phase the stock sweeps are runs of the candidate on the stock
  route, so I removed that entry from this rehearsal's run book and recorded again on `x64-04b1211b-pfdee656`; clause 5
  then holds with the stock route's own hash set: the EXE 04cfdc71..., no x64 DLL or server.)
- Stopped 23:02:00, restored by hash 23:02:14 (`restore: OK`, front end original, `provision.py verify` rc 0); no
  `HI-KIT-` save left in the instance; c5h-a released 23:04.

**The verdict** (`phase.py verdict F:/c5-64bit-work/0398/phase/rehearsal-4607-all-met`): **PASS (REHEARSAL,
subset)**, clauses 1-5 hold; clause 1 has both sweeps completed with the kit's proofs, clause 5 one build id with one
hash set per route; clause 4 sets the baseline (UI 45 of 4,475 covered over the 20 sessions). With `--no-coverage`
clause 4 fails by design ("coverage not measured").

**Times.** Product 52.3 min, stock 52.4 min (with the stop), each inside 0392's 50-65 minutes. Launch to in game
2.3 min on both routes (product 21:15:17-21:17:38, stock 22:08:58-22:11:06).

**Found on the way.** `sweep.py compare --out DIR` failed when DIR did not exist (`[Errno 2]`); it now makes the
folder (tested). No product fault; nothing for 0392's 2-of-2 pipeline.

**Pictures.** The sweeps' step pictures stay in their session folders (diagnostic, not fed). Two are in the Visual
Checks feed under issue 4607 (DIAGNOSTIC, D112): the product s01 start and the stock s05 start after the resume.

**Checks** (2026-11-16 23:04-23:08 EST, under quietlock, after the sweep.py changes): `python -I
perf/quietlock/quietlock.py run --who 0398 --what ... -- python -I tools/harness/run_tests.py -- -k "test_hi or
safety_static or campaign"`: harness 903 passed, 0 failed, 2 skipped (both older and unrelated:
`test_hi_actions.py:270`, `test_hi_actions_live.py:111` live opt-in); instances 110 ran, 0 failed; `python -I -m
pytest tools/hi/charters/test_campaign.py -q -p no:cacheprovider`: 59 passed. New or extended tests in
`test_hi_sweep.py`: `took_ms` kept with each fingerprint, a start without a picture tried once more, a start that
cannot be made stops the sweep without `ended` and a completed resume clears the stop, `compare --out` into a new
folder.

Records (F:, not committed): `F:/c5-64bit-work/0398/4607/` (sweep-product, sweep-stock, compare, logs, scripts) and
the phase folder `F:/c5-64bit-work/0398/phase/rehearsal-4607-all-met/`.

### 15.14 Stock defects of VP 5.4.8 the phase will meet (MC #3693, 2026-11-17 04:27 EST) `[C]` from the sources named

Told to lead-0398 by the MC from ticket 0462 (its `upstream-lua-fixes.md` 3.3 and 4.4); recorded here so that a phase
run classifies them before filing anything against the patch.
- **A load from the game menu while a game runs can crash in UI teardown on both routes** (upstream #13463, VP's own
  Lua; fixed for VP's next release). A journey or explorer that loads from the in-game menu (charters `save-load`,
  `lifecycle-menus`, item 32 `lifecycle-loads`) can meet it: such a crash is classified as this stock defect, with the
  stock replay as usual (0392's rule), before any report against the patch. 4602's rehearsal of item 32 met it 0 of 20
  times (15.8).
- **Maps wider than 181 plots hit `Events.ClearHexHighlights` in mod Lua** (upstream #13539, aa7a29b38; mod Lua only,
  `research/03-dll-engine-boundary/upstream-fix-batch-1.md` section 3). Phase maps stay at most 181 wide. The kit's
  widest is the 43-civ situation's Huge Tectonic Stress 180x112 (`kit.json`); the others are Huge and Tiny Continents
  and Large Communitu_79a; item 33's largest configuration is a Huge map from the set-up screen. A charter or
  configuration that picks a wider map is out of bounds.

### 15.15 The first full phase on i2-rc1: plan and the 48 sweeps (ticket 4608) `[L]`

Ticket 4608, impl-4608 (account B), 2026-11-18 01:50- EST, branch `task-4608-hi-phase-sweeps`.

**The candidate.** `i2-rc1` (ticket 0509, MC #4805; take post #4801): installer
`F:/c5-64bit-work/0509/installer/VP64-Patch-i2-rc1-for-VP5.4.8.exe` (8,759,842 B, SHA-256 6ed1dad6..., i2, std only),
layout `x64-4f4daac7-p7bf0d57`, pack manifest 3fa8269f.... Build id in the run book: `i2-rc1` on both routes.

**The plan `[L]`** (01:53): `phase.py plan --candidate i2-rc1 --out F:/c5-64bit-work/0398/phase/i2-rc1/`: 99 items (48
sweeps, 35 charters, 8 set-up configurations, 6 window-abuse pairs, `loads`, `journeys`), kit sha256 f68f6bf2..., the
campaign index `<phase>/campaign/charters.json` with 34 charters (`setup-options` skipped: front end). The same 99 as the
dry plan of 8.1. `sweep:43-civ:product` recorded absent at 02:02 (`phase.py absent ... --ref MC#3091`).

**The routes `[L]`.**
- **Product** (MC #4594): the instance first given the player's state (`provision.py set-dll` VP 5.4.8's own
  `CvGameCore_Expansion2.dll` 262aee88..., from `F:/c5-64bit-work/0509/take/inputs/player-x86/`), then the installer
  run silent on a hidden desktop in test mode (`research/06-tooling-and-workflows/evidence/2600/run_installer.py ...
  --action install --procroot <instance>`, `/DIR` and `/GAMEDIR` the instance's). Every installed file checked by
  SHA-256 against the installer's staged payload (`F:/c5-64bit-work/0509/installer/payload/std/`) and against the take's
  posted hashes. Read at the lay and again at the release: the manifest's `shell_config`, `VP64Options.ini`
  `ServerRenderer`, the helper's side files, `vpproxy.ini`, the record. Removal by the installer's own `--action
  remove`, then every user .ini, the quick and auto save folders and the DLL by hash, My Games compared file by file
  with its listing from before the install (`treehash.py`), `provision.py verify` rc 0.
- **Stock**: the default path, `set-dll` the pin's i0ship-39f17f75-ia (7de39f71...), no `vpproxy.ini`. The 43-civ stock
  sweep on the kit's 43 DLL i0ship-26234371-m43-ia (204fbfcc..., batch 2: no batch-3 43 build exists).
- Both: WindowRes 1920x1080 (D158), SinglePlayerAutoEndTurnEnabled 0, the import wrapper reinstalled (MC #3612), the
  kit save copied in by SHA-256 before each sweep. Scratch scripts (not committed): `C:/c5-64bit-work/0398/4608/lay.py`,
  `runq.py`.

**Commands.** Per sweep: `python -I tools/hi/sweep.py -i NAME --by impl-4608 run --ticket 0398 --route R --save SAVE
--out F:/c5-64bit-work/0398/phase/i2-rc1/sweeps/R/SITUATION --mods cp,vp,eui --launch`; then `cli.py stop
--human-turn-hook tools.harness.turns:stop_readiness`; `phase.py record <phase> --item sweep:SITUATION:R --run <out>
--build i2-rc1`; with both routes done `sweep.py compare <product> <stock> --out <phase>/sweeps/compare/SITUATION`.

**The installed state on c5h-g `[L]`** (02:13, F:, mods548): installer exit 0, `helper on exit code 0`; My Games gained
exactly: the proxy 8d809d01... in place of VP's DLL, `CvGameCore_Expansion2_x86.dll` 262aee88..., `shell_composite.dll`
8053f8c7..., `shell_composite.ini` (= the payload's), `shell_composite.sha256`, `vp64helper.exe`, `vpproxy.ini`
(f80d7590..., `mode=on pack=1`), the notices, `VP64\` (x64 DLL 87b07172..., server 54e873a2..., manifest 3fa8269f...,
purity, the three libraries), the record and backups in `My Games/VP64/install/`, `vp64-config-side.ini` and
`vp64-options-side.ini`; the helper changed `config.ini` (27e8332b -> 64151835) and `VP64Options.ini` (9497f85b ->
446ae0e1). Manifest `shell_config` **i2**, `ServerRenderer = 1`.

**Harness setting: AutoUnitCycle (MC-B #5033, 0530's finding; MC #5043).** The instances differed in UserSettings.ini
`AutoUnitCycle` (c5h-g 1, c5h-h 0, as they were before the lay). From 03:05:30 lay.py's per-sweep save step sets
`AutoUnitCycle = 1` before each sweep's launch (between sweeps only) and records the lay value in the instance's plan;
the lay's UserSettings.ini backup (c5h-h 5ffa161d...) restores it by hash at the release. Only
`sweep:era-ancient:stock` ran with 0; its pair is identical action by action (below), so it was not rerun.

**The result `[L]`** (2026-11-18 02:11-12:53 EST, workstation, 4 instances at most: c5h-g and c5h-e product,
c5h-h and c5h-i stock; mods `cp,vp,eui`, 1920x1080): **all 48 sweeps recorded**: 47 completed and
`sweep:43-civ:product` absent (MC #3091). Every product sweep's trace headers carry the take's x64 DLL 87b07172... and
server 54e873a2... (EXE 04cfdc71...); every stock sweep's carry no x64 DLL or server; one build id `i2-rc1`, one hash set
per route. Every sweep's kit proofs `ok` (`phase.py record`, derived saves included); 0 proof problems.
- **Product route: 0 findings, 0 boxes, 0 process ends, 0 actions with a non-ok outcome** over 23 sweeps (1 launch
  each, 7-10 in-process reloads each).
- **Stock route:** 1 finding, the fatal box of `era-atomic` (below; stock route, not a patch-only fault), and 7 actions
  with step exit 3, all a `source_glitch` PrintWindow picture on a left or right click that opens a popup or overview
  (FaithString, MenuButton, SciencePerTurn, GoldPerTurn, CultureString, NoAutoSpecialistCheckbox, ResearchFocusButton):
  a capture fault of the harness on stock (13.5 saw stock PrintWindow good in 20 of 20 in-game shots; these are
  shots right after a popup opens) `[I]`.
- **The pairs** (`sweep.py compare`: no rows in any of the 23, no finding on the product route to class).
  Action by action (`C:/c5-64bit-work/0398/4608/pairdiff.py`: outcome, hit, changed fields and restore per control and
  mark): **4,116 identical, 45 differ, 3 on one route only**. Every difference is harness-side: (a) the 7 stock
  `source_glitch` steps; (b) hovers whose hit the judge read differently at the moment of the hover (as 13.5); (d) a
  one-point control missed by one scan; (e) near-defeat's 16 actions behind a modal the judge cannot see. None is a
  product fault.
- **Time:** 47 timed sweeps, 37.3 instance-hours, mean 47.7 min (product 39.0-56.1 min, stock 38.3-62.7 min; the 43-civ
  Huge 180x112 stock sweep 62.7 min; era-atomic stock 61.2 min with the crash and a relaunch). Wall clock 10 h 42 min
  (02:11-12:53) on 2, then 3 (03:33), then 4 (04:27) instances. Plan 3.1's 50-65 min per sweep was about 10 % high.
- **Installed state through the run:** on c5h-g (8 sweeps) and c5h-e (11 sweeps) the manifest's `shell_config` i2,
  `ServerRenderer = 1`, the helper's side files, `vpproxy.ini` f80d7590..., `VP64Options.ini` 446ae0e1... and
  `config.ini` (c5h-g 64151835..., c5h-e 6a45b634...) were byte for byte the lay's at the end of the last sweep
  (`lay.py state`, 12:20) `[L]`.

| Sweep | Product (min, sessions, findings, launches, instance) | Stock | Actions same / differ / one route | Located p/s (top, unit, city, tech, menu) |
|---|---|---|---|---|
| 43-civ | absent (MC#3091) | 62.7 min, 13 sess, 0 find, 1 on c5h-h | - | - |
| ai-demand | 46.1 min, 10 sess, 0 find, 1 on c5h-g | 43.1 min, 10 sess, 0 find, 1 on c5h-h | 179 / 1 / 0 | 16/16, 4/4, 28/28, 2/2, 10/10 |
| ai-proposals | 48.5 min, 13 sess, 0 find, 1 on c5h-e | 45.7 min, 13 sess, 0 find, 1 on c5h-i | 177 / 3 / 0 | 16/16, -, 32/32, 2/2, 10/10 |
| all-met | 44.3 min, 10 sess, 0 find, 1 on c5h-g | 42.0 min, 10 sess, 0 find, 1 on c5h-h | 180 / 0 / 0 | 16/16, 4/4, 28/28, 2/2, 10/10 |
| archaeology | 45.2 min, 13 sess, 0 find, 1 on c5h-g | 41.5 min, 13 sess, 0 find, 1 on c5h-h | 171 / 3 / 0 | 16/16, -, 30/30, 2/2, 10/10 |
| at-war | 42.6 min, 10 sess, 0 find, 1 on c5h-g | 41.5 min, 10 sess, 0 find, 1 on c5h-h | 180 / 0 / 0 | 16/16, 4/4, 28/28, 2/2, 10/10 |
| capital-threat | 48.0 min, 10 sess, 0 find, 1 on c5h-g | 45.0 min, 10 sess, 0 find, 1 on c5h-h | 180 / 0 / 0 | 16/16, 4/4, 28/28, 2/2, 10/10 |
| era-ancient | 56.1 min, 10 sess, 0 find, 1 on c5h-g | 53.7 min, 10 sess, 0 find, 1 on c5h-h | 180 / 0 / 0 | 16/16, 4/4, 28/28, 2/2, 10/10 |
| era-atomic | 55.4 min, 13 sess, 0 find, 1 on c5h-g | 61.2 min, 14 sess, 1 find, 2 on c5h-h | 174 / 3 / 0 | 16/16, -, 31/31, 2/2, 10/10 |
| era-classical | 54.8 min, 15 sess, 0 find, 1 on c5h-e | 49.0 min, 15 sess, 0 find, 1 on c5h-i | 200 / 1 / 0 | 17/17, 4/4, 34/34, 2/2, 10/10 |
| era-industrial | 43.2 min, 9 sess, 0 find, 1 on c5h-e | 40.8 min, 9 sess, 0 find, 1 on c5h-i | 188 / 1 / 0 | 16/16, 4/4, 31/31, 2/2, 10/10 |
| era-information | 54.2 min, 13 sess, 0 find, 1 on c5h-g | 52.1 min, 13 sess, 0 find, 1 on c5h-h | 171 / 3 / 0 | 16/16, -, 30/30, 2/2, 10/10 |
| era-medieval | 52.2 min, 13 sess, 0 find, 1 on c5h-e | 51.7 min, 13 sess, 0 find, 1 on c5h-i | 179 / 1 / 0 | 16/16, -, 32/32, 2/2, 10/10 |
| era-modern | 55.5 min, 13 sess, 0 find, 1 on c5h-g | 53.9 min, 13 sess, 0 find, 1 on c5h-h | 173 / 1 / 0 | 16/16, -, 30/30, 2/2, 10/10 |
| era-renaissance | 49.6 min, 9 sess, 0 find, 1 on c5h-e | 46.7 min, 9 sess, 0 find, 1 on c5h-i | 185 / 4 / 0 | 16/16, 4/4, 31/31, 2/2, 10/10 |
| great-people | 51.3 min, 13 sess, 0 find, 1 on c5h-e | 45.5 min, 13 sess, 0 find, 1 on c5h-i | 179 / 1 / 0 | 16/16, -, 32/32, 2/2, 10/10 |
| near-defeat | 50.2 min, 9 sess, 0 find, 1 on c5h-e | 45.3 min, 11 sess, 0 find, 1 on c5h-i | 173 / 16 / 0 | 16/16, 4/4, 31/31, 2/2, 10/10 |
| near-victory | 45.3 min, 13 sess, 0 find, 1 on c5h-g | 41.4 min, 13 sess, 0 find, 1 on c5h-h | 173 / 1 / 0 | 16/16, -, 30/30, 2/2, 10/10 |
| nuclear | 54.5 min, 13 sess, 0 find, 1 on c5h-g | 50.0 min, 13 sess, 0 find, 1 on c5h-h | 176 / 1 / 0 | 16/16, -, 31/31, 2/2, 10/10 |
| religion-enhanced | 41.3 min, 10 sess, 0 find, 1 on c5h-e | 40.2 min, 10 sess, 0 find, 1 on c5h-i | 181 / 2 / 0 | 16/16, 4/4, 29/29, 2/2, 10/10 |
| religion-founded | 39.0 min, 10 sess, 0 find, 1 on c5h-e | 38.3 min, 10 sess, 0 find, 1 on c5h-i | 183 / 0 / 0 | 16/16, 4/4, 29/29, 2/2, 10/10 |
| religion-pantheon | 41.2 min, 10 sess, 0 find, 1 on c5h-e | 39.0 min, 10 sess, 0 find, 1 on c5h-i | 180 / 0 / 0 | 16/16, 4/4, 28/28, 2/2, 10/10 |
| religion-stages | 49.2 min, 10 sess, 0 find, 1 on c5h-g | 46.0 min, 10 sess, 0 find, 1 on c5h-h | 176 / 1 / 3 | 16/16, 4/4, 27/28, 2/2, 10/10 |
| trade-routes | 49.0 min, 13 sess, 0 find, 1 on c5h-e | 47.5 min, 13 sess, 0 find, 1 on c5h-i | 178 / 2 / 0 | 16/16, -, 32/32, 2/2, 10/10 |

(a) `TopPanel/FaithString` left click (opens the religion overview, closed by Esc on both): stock step exit 3 because
its PrintWindow picture was a `source_glitch` (no usable picture, so `not_flat`/`not_black` could not be read);
product ok. Same changed fields and restore on both. A harness capture fault on the stock route, not a finding.
(b) `CityView/PQbox` hover: both ok, nothing changed; the hit was read as `PQbox` on product and as the neighbouring
`AutomateProduction` on stock: the judge's read at the moment of the hover, as research 13.5's two hover differences.
(c) Two hovers read differently (game menu `DetailsButton`: `ModsButton` on product; `TopPanel/MenuButton`: a plot on
product), as (b); and `TopPanel/MenuButton` left click (opens the game menu): stock exit 3, `source_glitch` picture, as
(a). All 7 stock exit-3 steps of the run are clicks that open a popup or overview; product had none.
(d) `CityView/CultureFocusButton` (a one-point control in the grid scan: 1 point at [1648,240] on both routes in
era-ancient) was located on stock and not on product, so its 3 actions ran on stock only. The scan reads the
judge's `HasMouseOver` at each grid point; a one-point hit is missed when the read and the cursor disagree once. A
harness scan limit (research 13.6 item 1), not the game's.
(e) **near-defeat: 16 differing actions from one modal the judge cannot see** `[L]`. In product session 7 (city screen
of Amsterdam), step 87's left click at (1648,912) on the right-hand building list opened the city screen's
sell-building confirmation ("Garden: Are you sure you want to sell this building?", Yes/No). The judge's state did not
change (`screens` still only `/InGame/CityView`, `popup` unchanged), so the sweep saw no change, did not restore, and
steps 88-102 (ProductionPortraitButton, ProdBox, AutomateProduction, PQbox, ReturnToMapButton: 15 actions + the
unit-panel hover) all hit the modal (`pixel`, nothing changed; ReturnToMap did not close the screen). On stock the
same click hit another row: the building list stood at another scroll position (the scan put `RightScrollBar` at
y 560 on product and y 528 on stock, and steps 75-76 clicked it there). Picture: Visual Checks
`vc-20261118-101213-workstation-c486` (DIAGNOSTIC, step 101, with the stock step 104 as an approximate reference).
The game did what the click asked; **harness gap**: the city screen's own Yes/No confirmation is not visible to the
judge (not a context of its own), so the sweeper cannot restore it. Not a product finding. Open for the harness: name
that box in the judge's reads, or have the sweeper's never-list cover the building list's rows.

**A stock-route crash in the farm-field layout (2026-11-18 08:24:55, c5h-h)** `[L]`, addresses `[C]`, cause `[I]`.
- `sweep:era-atomic:stock`, save `HI-KIT-ATOMIC-HUGE-T350` (85ea3a7e..., Huge, turn 350), the stock route: pin
  `i0ship-39f17f75-ia` (7de39f71...), no `vpproxy.ini`, no proxy, no compositor; the EXE `04cfdc71...` (the
  instances'). One process since the sweep's launch at 07:4x; the crash came in its **9th in-process reload** of the
  save (sweep.py's RELOAD_CHUNK, after session 12 ended 08:24:12), 43 s into that load, i.e. in the load gap.
- VP's handler box "Game Crash": 0xC0000005, read of 0x00007377, `Location (in file)` +0x0037BAE6, live
  `0x00e3c6e6+0x0037c6e6`: RVA 0x37C6E6, **VA 0x77C6E6**, inside **0x77C1C0** (the weld walk of the farm-field layout
  graph, research 02 `crashes-huge-load.md` 2): 0xC bytes after run5nd's 0x77C6DA, 0x27 before 0435's 0x77C70D.
  Stack return addresses (RVAs): 0x6FACF, 0x201AFE, 0x3490B3, 0x466919, 0x23ABD4, 0x385BA7, ... = VAs in 0x46F8B0 (one
  of 0x77C1C0's callers), the field layout 0x6016F0, the farm builder 0x748FC0, the listener walk 0x8668E0 and the
  dispatcher 0x63ABA0: **the same chain as the three product-path crashes** of `crashes-huge-load.md` 1, 7 and 8 `[C, by
  address; no dump opened]`.
- Memory markers: Sub2G free 44,104 KB (largest 7,872 KB), total free 1,074,760 KB (largest 978,060 KB). The handler
  appended its "32-bit address space nearly exhausted" advice; as in `marathon-stress-t2000.md` 4.1 the read is of a
  near-null value with a GB free, so not an allocation failure `[I]`.
- **Cause** `[I]`: the EXE's own farm-field layout fault of `crashes-huge-load.md` 8.3 (the double free of an endpoint,
  root-cause candidate of ticket 0515), here on the stock route. To confirm: open the minidump at 0x77C6E6 and read
  which field of the node record it read, and walk the endpoint free list in the dump (0515's `layout_peek.py` method).
  **As far as found, the first stock-route crash in 0x77C1C0** (0515's stock arm: 0 in 24 loads, section 8.5); with
  it the stock count is 1 in at least 25 huge loads plus this sweep's 8 reloads. So the fault is the EXE's, seen on
  both routes; the in-process reload of a Huge late save is one way to meet it.
- Closed by the claim's owner: `cli.py -i c5h-h dismiss --button OK --fatal --requested-by ...` (08:25:55), `stop
  --after-fatal` (08:26:24); crash collection `F:/c5-64bit-work/0398/4608/crash-era-atomic-stock/` (minidump
  `CvMiniDump_20261118_082455_...dmp` 6da3af5c..., crashes.log, queueguard.log, instruments.log, the dismiss record).
  Posted #5465. Not a patch-only finding (stock route); the sweep resumed (`--resume`) at 08:30. The MC recorded this
  occurrence in research 02 `crashes-huge-load.md` 8.5 (main 05dc2a16e, MC #5472); that section is the reference. The
  minidump folder is kept as it is for a D185 dump read (lead #5473).
- On the product route the same save (`era-atomic`, c5h-g, 07:59-08:54) ran its 8 in-process reloads with no crash;
  one clean run is no evidence of a rate (`crashes-huge-load.md` 8.5).

**Restores and claims `[L]`.** c5h-i (12:44) and c5h-h (11:49 after its stock list, then relaid `stock43`; again
12:53): `lay.py NAME restore` OK (every user .ini, the quick and auto save folders by SHA-256, the kit saves removed,
`set-dll` back to the instance's 3c2f47da..., the front end original, `provision.py verify` rc 0), released. c5h-e and
c5h-g stay laid as installer-installed i2-rc1 product instances for the runners (lead #5663, #5671): transferred to
lead-0398 at 4608's hand-back, idle, with the restore line of the issue log (2026-11-18 11:20).

**Found on the way (harness, not the game).**
- `phase.py`'s run book was an unlocked read-modify-write while several owners record into one phase folder; fixed
  (cb91befac: a lock file beside `runbook.json`, re-read inside it; 2721d1d5c: Windows `PermissionError` on a
  delete-pending lock, a replace over an open file and a read during a replace retried), with hermetic tests.
- The judge does not see the city screen's own Yes/No confirmation (sell a building), so a sweep click that opens it
  leaves the rest of the session behind a modal (note (e)).
- The instances differ in UserSettings.ini `AutoUnitCycle` as provisioned (c5h-h 0; c5h-e, c5h-g, c5h-i 1): set to 1
  per sweep here (MC #5043); the real fix is the MC's.
- A sweep's in-process reloads of a Huge late save (8-10 per sweep) are a load pattern that met the EXE's farm-field
  fault on stock once in 23 stock sweeps.

**Not verified.**
- The modpack form of the installer (NOT VERIFIED, MC #4594; mods form only here).
- `sweep:43-civ:product`: not in the candidate (i2, std only). `sweep:43-civ:stock` ran on the batch-2 43 DLL
  (204fbfcc..., `i0ship-26234371-m43-ia`); no batch-3 43 build exists, so this stock control is a comparison arm on
  older pins (COMMON 18). It does not block the verdict (MC #5043).
- The stock-route crash's cause `[I]` (dump not read; MC/D185).
- Coverage: not measured here; clause 4's totals are read over all the phase's traces by `phase.py verdict`. The sweeps
  ran the default five screens only (the kit's `reach` lists are state checks, not screens a key opens).

### 15.16 The first full phase on i2-rc1: journeys, loads, set-up, runners (ticket 4609) `[L]`

Ticket 4609, impl-4609 (account B), 2026-11-18 01:52- EST. The candidate is i2-rc1: ticket 0509's take, installer
`F:/c5-64bit-work/0509/installer/VP64-Patch-i2-rc1-for-VP5.4.8.exe` (SHA-256 6ed1dad6..., checked), pack manifest
3fa8269f..., proxy 8d809d01..., x64 DLL 87b07172..., server 54e873a2..., compositor `shell_composite.dll` 8053f8c7....
Phase folder `F:/c5-64bit-work/0398/phase/i2-rc1/` (4608's plan). Mod set `cp,vp,eui`.

**take_release on a kept take whose run folder is gone.** 0509's run folder `C:/c5-64bit-work/0509/run` was removed
after the merge; the state in the kept copy `F:/c5-64bit-work/0509/take/state.json` still names it (84 paths), and its
`config.fork` names `C:/c5-64bit-work/0509/fork`, which is also gone. `take_release.py --only hi_journey` needs:
- the build, pack, purity and testdata outputs. The installed route reads only the build's `x64_hashes`; the default
  path also reads `x64_layout`, `i0ia_dll` and the candidate descriptor;
- a `--fork` that is a git clone outside the user's clones. hi_journey reads no fork, so an empty `git init` folder
  passes the driver's check.

Done by copying `testdata`, `layout`, `pack`, `inputs`, `purity`, `deps`, the descriptor and the -ia DLL into a work
folder on C:, with the run paths in `state.json` rewritten (scratch `mkwork.py`, `setfork.py`; nothing written under
0509's folders) `[L]`.

**The installer on c5h-d and c5h-a** (silent, test mode, `/DIR` and `/GAMEDIR` of the instance, the player's state
made first with `set-dll --dll <the tag's DLL 262aee88...>`): exit 0 at 02:06:50 (c5h-d) and 03:20:22 (c5h-a). The
My Games tree differs by exactly the 28 rows of the i2 payload: the proxy, `CvGameCore_Expansion2_x86.dll`,
`shell_composite.dll`/`.ini`/`.sha256`, `VP64\` (seven files), the helper, the notices, `vpproxy.ini`, the record and
backups, `vp64-config-side.ini`, `vp64-options-side.ini`, `config.ini` and `VP64Options.ini` changed by `vp64helper on`.
State read after the install: manifest `shell_config` `i2`; `vp64helper status` "config.ini on", "VP64Options.ini on:
ServerRenderer = 1", "mode: on" (exit 3, "failed: (not set)", not a failure of the install); config.ini
`[DisableSystems]` with OverlayDebug, Decal, Forest, FOW, FluidFOW, YieldIcons, DistanceFog, ColorKeyEdit = 1, the
rest 0. In both games the proxy log shows `shell: [Renderer] ServerRenderer = 1: composite ... sha256 8053f8c7...
matches`, `render=1`, `composite ... loaded`, and the view mask with Terrain cleared `[L]`.

**D162 (PHASE.md 8 step 2, MC #4590): both journeys passed on both routes** `[L]`.

| Journey | Product: installer-installed i2, compositor on (c5h-d) | Stock: default path of the same build (c5h-a) |
|---|---|---|
| release-basic | exit 0, 63 steps, 6/6 checkpoints, 641.5 s (02:12-02:23) | exit 0, 63 steps, 6/6, 559 s (02:26-02:35) |
| release-large | exit 0, 21 steps, 5/5, 479.1 s (02:26-02:34) | exit 0, 21 steps, 5/5, 465 s (rerun 03:09-03:17, AutoUnitCycle = 1) |

- The installed state (shell_config, helper status, ServerRenderer, config.ini f01c3839..., VP64Options.ini
  446ae0e1..., the binaries' hashes) was read before and after the run: unchanged.
- The joined record is `F:/c5-64bit-work/0398/phase/i2-rc1/runs/journeys/report.json`: the stage reports by SHA-256,
  the configuration line and the AutoUnitCycle of each instance. `phase.py record --item journeys` gave completed and
  passed. Script: scratch `merge_journeys.py`.
- **Harness finding (MC #5043): AutoUnitCycle.** UserSettings.ini `AutoUnitCycle` is 0 on c5h-a (1 on c5h-d).
  release-large's step 11 kills the warrior and needs the auto-cycle to centre the camera, so both routes on c5h-a
  stopped at step 12 with exit 6 (02:38-02:54). Cause per 0530's #5033. The rerun with AutoUnitCycle = 1 (backed up
  5ffa161d... and restored by hash) passed.
- **Harness flake:** the default-path *product* replay of release-large (thin client, PrintWindow) in the rerun gave
  exit 3, "no usable picture: top band 37 levels from the reference (printwindow, 2 retakes)", at checkpoint
  `attacked`. This is the known product-route PrintWindow flake (CONTRACT 4.2). It is not the D162 product route,
  whose picture source is the composite.
- A take with `for_art` in its state stops (exit 10) whenever a run plays only one route. The verdict lines say
  "for the owner: NO ... stock not played" although the journeys passed. Expected; the joined record is the D162 record.

**Item 32, loads, on the installed i2 instance (c5h-d), compositor on, product route** `[L]`.
- Trace recorded 02:43-02:48 on `HI-KIT-MEDIEVAL-LARGE-T200` (the plan's situation, e3da1d17..., Large, turn 200,
  gold 38): checkpoint `start`; ESC; LOAD GAME; the top row; Load Game (wait `front_end load_screen`, 15.8 s); wait
  60 s; ENTER (`in_game`); stable; checkpoint `loaded`. File `F:/c5-64bit-work/0398/4609/rec-loads/trace.jsonl`,
  check-trace clean (one screen-wait warning, as 4602's). The Load Game step's own picture failed with exit 3 (no
  picture during the load); the replays do not stop on it.
- `phase.py loads --n 20`, 02:48:45-03:28:24: **20 of 20 completed, turn 200 and gold 38 after each load, no finding,
  no box**, about 2 min a load. Recorded (`runs/loads20`, completed).
- The quick pair was recorded 03:29-03:32 (`rec-quick/trace.jsonl`, 9 steps: QUICK SAVE with `screen_closed
  /InGame/GameMenu`, then the QuickSave row; check-trace clean, no warning). `phase.py loads --n 20`
  03:32:56-04:13:26: **20 of 20 completed, turn 200 and gold 38 after each, no finding, no box**, about 2 min a pair.
  Recorded (`runs/quick20`, completed).
- **Observation, for the renderer teams (not yet compared with stock):** on the product route with the compositor on,
  the load screen of an in-game load is translucent over the world picture in the composite's frame. The leader
  picture and the right-hand panel show the terrain, cities and units behind them
  (`F:/c5-64bit-work/0398/4609/rec-loads/step_0005.png`). 4602's thin-client load screen on PrintWindow is opaque
  (`F:/c5-64bit-work/0398/4602/rec-loads/step_0004.png`). Whether the stock EXE shows the world through it too, and
  whether this is the composite capture or what reaches the screen, is not read yet.

**Item 33.** `setup:8` recorded absent (`phase.py options --config 8`): i2-rc1 ships the standard variant only
(#4801), MC #3091. 4604's trace and variants file are copied to `F:/c5-64bit-work/0398/4609/setup/` (seeds 3/3).
Configurations 1-7 are not run yet.
**Update (impl-4609g, 2026-11-18 16:06) `[L]`:** `scripts-g/run_setup.sh` (product route, installer-installed i2-rc1,
seeds 3/3, `menu-ready` before the first click; config.ini copied to `backup/<name>/config.ini.orig` and restored by
hash at 16:06). **setup:1 completed** on c5h-f (15:17-15:27, recorded rc 0, `setup/config-1-r1`). The others are
recorded incomplete or not run, each a harness fault, not a product finding:
- setup:2 (c5h-f, 15:36) and setup:5 (c5h-d, 15:40): `phase.py options` "the replay could not run (exit 3)", 0 steps
  done, the replay folder empty; hi.py's exit 3 is "no usable picture" (CONTRACT section 12). setup:1 on the same
  instance two runs earlier got its pictures.
- setup:6 (c5h-d, 15:49): replay exit 5 at 0 steps.
- setup:3 (c5h-f) and setup:7 (c5h-d): `run_setup.sh` stopped at "FAILED: stop": `cli.py stop --human-turn-hook
  tools.harness.turns:stop_readiness` returned non-zero right after the previous configuration's replay (the game
  at the front end). A later stop of the same game worked ("Lua channel silent ... front end idle", stopped).
- setup:4 (c5h-f, 15:43): incomplete, "the process ended": my own manual `cli.py stop` on c5h-f at 15:47 terminated
  it while the loop was in it (time lost, owner error).
- Next: read why the replay of `rec-setup-4604.jsonl` gives exit 3 / exit 5 at step 0 (run `hi.py replay` of the
  changed trace by hand on one instance after `menu-ready`, and read its stderr), then re-run 2-7 one at a time.

**Update (impl-4609h, 2026-11-18 16:20-16:56): why setup:2-7 failed at step 0 `[L]`.** Two causes, both harness, not
product:
- **The front end's mod activation outlasted menu-ready, and the script did not see it.** Each launch after a played
  game activates the mods again (Lua.log "MainMenu: vp-dll-dev: activating 3 mods - this rebuilds the game database").
  Under today's load that took longer than `menu-ready --timeout 300`. On c5h-f: launched 16:36, Lua initialised only
  at 16:38:15, still "activating" at 16:42, so menu-ready printed "NOT READY: ... mod activation in progress" after
  302 s. `run_setup.sh` ran it as `menu-ready ... | tail -1 || fail`: the pipe's status is tail's, so the NOT READY
  went unseen and `phase.py options` started the replay during the activation. That gives hi.py's exit 3 ("no
  picture at session start", session.py `SessionError(... e.exit_code)`, replay.run's `stop.why`) or a first click
  that is lost (exit 5 at step 1, "front_end single_player" deadline). `phase.py options` recorded only "could not run
  (exit 3)", because it dropped replay.run's `stop` (fixed in 6ca90d14d: the record's `replay.stop` and the `why` now
  name the reason; loads the same; test in `test_hi_phase.py`).
- **The window size.** 4609g's 16:06 restore had put GraphicsSettingsDX11.ini back to the installed 2560x1440
  (hash f1d9cf017a31). The setup trace is recorded at a 1914x1051 client. A hand replay of `config-2.jsonl` on
  c5h-f at a 2554x1411 client (16:25-16:32, `F:/c5-64bit-work/0398/4609/setup/hand-2-h1/`) clicked steps 1-6
  right (front-end controls placed by the centre), then missed step 7's Back of Advanced Setup. That panel is anchored
  to the bottom (Back at centre offset (-367, +677) at 1411 high, recorded (-366, +495) at 1051), so the screen
  stayed on Advanced Setup and the judge wait timed out (exit 5). A setup replay must run at the recorded size
  (D158: chosen at launch).
- The fixed script is `scripts-h/run_setup2.sh`. It runs `prep_inst.py set` (1920x1080, auto end turn 0, restore
  by hash) before each launch, checks menu-ready's own exit with a 900 s timeout, and keeps the launch and stop
  output, with one stop retry after 30 s. 4609g's two "FAILED: stop" kept no output, so their cause is not read.
- **F: was saturated during these runs** (16:56: 100 MB written with fsync in 43 s, 2.3 MB/s; C: 0.09 s; MC-B 16:58:
  0 % idle at 0.3-1.6 MB/s, many small writers, not one job). The replays of setup:2 and setup:5 on c5h-f and c5h-d
  (instances on F:) ran 45-118 s a step, against 13 s for setup:1 at 15:20. One stable wait's single picture took about
  15 s. Later setup runs write their replay pictures to C: (`C:/c5-64bit-work/0398/4609h/`, lead #5985), and the
  records are moved to F: afterwards.
- **A third cause on c5h-f: AutoUnitCycle = 0 (17:18) `[L]`.** setup:2-h1 (c5h-f, 1914x1051, 16:43-17:17) passed
  steps 1-27 and stopped at step 28 (exit 5): End Turn's judge wait `state.turn>=1` reached its 240 s deadline with
  "A UNIT NEEDS ORDERS" shown (`F:/c5-64bit-work/0398/4609/setup/config-2-h1/replay/stop_0028.png`). Steps 26-27 are
  two SPACE presses that skip the two units in turn. With AutoUnitCycle 0 the second SPACE does not reach the
  warrior. c5h-f's UserSettings.ini has AutoUnitCycle = 0 (as installed; MC #5043: 0 on a, b, f, h), c5h-d has 1, and
  the recording instance c5h-c has 1. A harness setting, not a product finding (8.1). `scripts-h/run_setup3.sh` sets
  AutoUnitCycle 1 after `prep_inst set` (UserSettings.ini restored by hash by `prep_inst restore`).
- **Harness gap, for 4610 (lead #6045) `[L]`: the deny-list read's 1 s bound.** setup:6-h1 (c5h-d, product, 1914x1051,
  AutoUnitCycle 1, 2026-11-18 17:14-17:35) is recorded incomplete: "the replay could not run (exit 2: step 32 action 0
  refused: deny list: deny list could not be read (channel busy (request picked up, no answer within 1.0 s): lua: no
  answer within 1 s ... state InGame ...))". Record: `F:/c5-64bit-work/0398/4609/setup/config-6-h1/options.json`.
  F: wrote at 1.3-2.4 MB/s around then (17:13 and 17:20 measurements; MC-B 16:58: 0 % idle). The Lua channel had
  picked the request up but did not answer within the 1 s bound, so a busy, not silent, channel refused the action.
  For 4610: let the deny-list read wait longer (or retry) when the channel answers "busy", rather than refusing the
  step.
**#5917, 4611's camera-selection notes (impl-4609h, 2026-11-18) `[L]`.** Cut of the runner's steps 1-28
(`scripts-h/mkcut.py`, `F:/c5-64bit-work/0398/phase/i2-rc1/runs/camera-selection/pipe-h/cut.jsonl`) replayed on
c5h-a stock (-ia DLL b71a9f31, default path, 1914x1051, cp,vp,eui) from HI-KIT-MEDIEVAL-LARGE-T200, 16:24-16:34, 28
steps, exit 0 (`pipe-h/stock-1/`).
- (a) Step 7, held DOWN 3 s at the south map edge: on stock the clouds run on to the bottom edge, with no seams and no
  black band (VC vc-20261118-180830-workstation-9ece). The product run's picture has two straight brightness seams
  (y about 365 and 390) and a hard cloud-to-black line at y 440 with black below (VC
  vc-20261118-180827-workstation-0450). The camera stops within about 50 px of the product's. So far this is a
  product-only picture difference for the renderer teams; it does not fail the phase (lead #5955). Product 2 of 2:
  see the update below.
- (b) Step 28, the hover that took over 120 s on the product: on stock, step 28 took about 37 s from step 27's record
  to its own (printwindow). Other stock steps took 4-76 s (27: 76 s, 23: 64 s), all on a loaded workstation. In the
  product runner's trace, step 28's record comes 10.7 s after step 27's and step 29's comes 171.8 s later, with the
  runner's model time in between. The step's shot fell back to printwindow after "composite dump timeout: no complete
  back-buffer dump within 5.0 s" (retakes 2). Product timing 2 of 2: see the update below.

**Owner closes at the end of impl-4609h's run (stock too) `[L]`, impl-4609h's checks, written up by impl-4609i.**
- air-units wrong_state-step58: a cut of steps 1-58 replayed on c5h-a stock (-ia b71a9f31, default path, 1914x1051,
  cp,vp,eui) gave the same judge at steps 56-58 as the product run; replay exit 0, 58 steps, 18:15. Closed as
  observation (stock too), VC vc-20261118-182418-workstation-5c98.
- nuclear wrong_picture-step70: a cut of steps 1-70 replayed on c5h-a stock (same set-up, HI-KIT-NUCLEAR-5DA39651,
  save SHA-256 5da39651...), 18:23-18:32, exit 0, 70 steps, stop clean. The judge at steps 68-70 matches the product
  run (unit 20728 UNIT_ANTI_TANK_GUN at 78,36, INTERFACEMODE_SELECTION), so the picture the runner flagged comes with
  the same state on stock. Closed as observation (stock too), VC vc-20261118-183321-workstation-c96b. Output moved to
  `F:/c5-64bit-work/0398/4609/stockchk/nuclear-70/` (impl-4609i, 2026-11-18 18:49, 68 files, SHA-256 list 2aa6d1d9f6c2502a).
That makes 18 owner closes; the only open findings are setup:2, 4, 5, 6 and 7 (each closes after its configuration's
completed run).
**#5917 product-1 on c5h-d (impl-4609h's chain_d, 18:08-18:26): the load never started; evidence read by impl-4609i
(lead #6148).** `vr3.sh c5h-d F:/c5-instances 1 product` (fresh game from HI-KIT-MEDIEVAL-LARGE-T200, SHA-256
e3da1d17...; `frontend.py prepare --mods cp,vp,eui --load`) launched pid 69580 at 18:08:41 (start-up watch ok in
82.6 s); for the 40 probes of its load loop (to about 18:25) `frontend.py ingame` answered "NOT IN GAME [silent] Lua
channel silent ... state Main", so no load-screen dismiss was tried. What is known `[C]`, from the files left:
- No box: `run/dialogwatch.log` for pid 69580 ends "process ended ... exit_code 15, crash false, boxes_seen 0" at
  18:26:46 (15 is the harness terminate of the next script's stop).
- The 64-bit server never started for this game: `Logs/VP64/run/` has no run folder between 20261118-174956-64984-1
  (the game before) and 20261118-183317-31904-1 (setup:5-h2 after it). The server never started for that load, so the save was
  never loaded by the DLL; the game stayed at the front end.
- The stop at 18:26 read the front end as idle: "Lua channel silent (front end, load screen, leader screen or
  frozen); front end idle (activation none, no journal held open, cache quiet 847.01 s)", so nothing had been written
  to the cache since about 18:12:40. The guard log ends with the start movie windows at 18:10:03; audio first seen
  18:10:04.
- Not known: whether the front end stopped in the mod activation or before the load request (the game's Lua.log was
  overwritten by the 18:26 launch; F: was saturated at the time, 15.16 above). The product route's Lua channel is
  answered by the x64 DLL in the server (installer-notes 14.5), so a silent channel in Main is what any front-end wait
  on the product looks like; it does not by itself say the game hung.
Class: not determined `[I]` (a load request lost or not yet made, under the F: load, is the likelier reading; no hang
evidence). The re-run uses `scripts-i/vr4.sh`, which logs every probe with its time and, if the load fails, keeps
Lua.log, Database.log, modding.log, APP.log, the `VP64/run` listing, the status and a shot in
`F:/c5-64bit-work/0398/4609/evidence/loadfail-<instance>-<time>/` before anything else touches the instance. A
repeat is reported to the lead at once as a hang candidate.
**setup:5 step 19 (Marathon): the city screen does not open after founding on the product; it does on stock
(impl-4609i, 2026-11-18 18:43-19:00) `[L]`.** Candidate for clause 2, with the lead (#6181, #6186).
- Product, 2 of 2 on c5h-d (installer-installed i2-rc1, mods form, compositor on, 1914x1051, AutoUnitCycle 1, seeds
  3/3, `phase.py options --config 5`): h1 (16:42-17:10, impl-4609h) and h2 (18:37-18:52, impl-4609h's chain) both
  stop at step 19 with exit 5: key B founds Moscow (seen in the picture), and the wait `screen_open CityView` (20 s)
  reaches its deadline (h2: 22.4 s, 14 polls). The judge then reads city_screen false, screens [], end-turn block
  ENDTURN_BLOCKING_PRODUCTION, and the selection has moved to the warrior (unit 1002 at 27,15). Records
  `F:/c5-64bit-work/0398/4609/setup/config-5-h1/` and `config-5-h2/` (VCs d062 and 73bf, impl-4609h's).
- Stock control on c5h-a (-ia DLL b71a9f31, default path, same trace, variants and seeds, `--route stock`), 18:43-18:53,
  `scripts-i/run_setup_stock.sh`: step 19 ok in 93 ms (city_screen true, screens ["/InGame/CityView"], block
  ENDTURN_BLOCKING_UNITS, city Moscow 1015); the whole replay completed, exit 0. Its step_0011 shows Duel, Prince,
  Marathon. Out `F:/c5-64bit-work/0398/4609/setup/stock-5-s1/` (not a phase record), VC
  vc-20261118-190039-workstation-9b4f.
- The judges at steps 17 and 18 are the same on both builds (settler 1001 at 28,14, block UNITS, no screens)
  (`scripts-i/jdiff.py`, `F:/c5-64bit-work/0398/4609/evidence/setup5-step19/judge-17-19-stock-vs-product-h2.txt`).
- Lua.log (copies in the same folder): no Lua error after the founding on either build. Stock logs "CityView:
  GameInfo.UnitClasses[-1] is undefined" at the founding (the city view opening); the product logs nothing after the
  in-game UI load.
- Instance options compared (c5h-a against c5h-d): UserSettings.ini and GraphicsSettingsDX11.ini identical
  (AutoUnitCycle 1 on both). config.ini differs only by the installer's i2 [DisableSystems] (Decal, Forest, FOW,
  FluidFOW, YieldIcons, DistanceFog = 1 on the product: product configuration), the seeds (3/3 on both during the
  runs) and the front end's remembered Last* values (the trace's Reset sets the set-up).
- Configurations 1, 2 and 6 (Standard pace, the same Duel set-up) pass step 19 on the product; only Marathon fails.
  Quick (3) and Epic (4) have not yet reached step 19 on the product. Not yet known: whether it follows the instance
  (setup:5 on c5h-b next) and why the pace matters.
- **Third product run, on c5h-b: step 19 passes (impl-4609i, 19:03-) `[L]`.** c5h-b (C: instance, installer-installed
  i2-rc1 with the same hashes as c5h-d: proxy 8d809d01, x64 DLL 87b07172, server 54e873a2; 1914x1051,
  AutoUnitCycle 1, seeds 3/3, original variants): key B founds Moscow and CityView opens in 421 ms (city_screen true);
  step 20 the same. So the 2-of-2 failure on c5h-d does not follow the build: it follows c5h-d (an F: instance, F:
  saturated) or the load's timing. Reported #6208, #6210 (thread 0569). The MC holds the classification: ticket 0569
  (MC #6199/#6200, p8, its own implementer), the ledger's two setup:5 entries name bug 0569 and stay open (MC #6209: no
  setup:5 product replay on c5h-d except by the MC's exception). `phase.py close` has no mode that names a bug without
  closing, so `scripts-i/bugonly.py` wrote `bug` and `bug_ref` into the phase's decisions.json for those two ids
  (backup `F:/c5-64bit-work/0398/4609/backup/decisions-before-bug0569.json`); a gap for 4610 `[C]`.
**Setup re-runs, step 17 lengthened (lead-0398's approval, answer to #6198) `[L]`.** setup:3-h2 and 4-h2 (c5h-f)
stopped at step 17 (ENTER lost before the load screen was ready, the lead's reading `[I]`). Re-runs of configurations
2-6 from 19:10 use `F:/c5-64bit-work/0398/4609/setup/setup-variants-i.json`: `setup-variants.json` with configuration
7's step-17 edit `{"step": 17, "action": 0, "ms": 60000}` added to 2-6 (`scripts-i/mkvar.py`); only a wait changes.
The runner is `scripts-i/run_setup5.sh` (run_setup3.sh with that file). setup:6-i1 (c5h-b, 18:51-19:01, completed)
and setup:5-i1 ran with the original file.
**Setup item 33 re-runs on c5h-b (impl-4609i, 2026-11-18 18:51-19:38) `[L]`.** c5h-b (C: instance) laid 18:45-18:48 with
the i2-rc1 installer (`scripts-i/lay_b.sh`; same installed hashes as c5h-d), then per configuration
`scripts-i/run_setup3.sh` / `run_setup5.sh` (prep_inst set 1920x1080, AutoUnitCycle 1, seeds 3/3, menu-ready with its
own exit, `phase.py options --route product`), output on C: and moved to `F:/c5-64bit-work/0398/4609/setup/` with
`move_rec.sh` (recorded again from F:). All on the deny.py of epic-0398-hi-kit 42de7d462 (the 1 s bound).
| Config | Run | Variants | Time | Result |
|---|---|---|---|---|
| 6 (Duel, Standard) | config-6-i1 | setup-variants.json | 18:51-19:01 | completed, rc 0 |
| 5 (Marathon) | config-5-i1 | setup-variants.json | 19:03-19:13 | completed, rc 0 (step 19 CityView in 421 ms) |
| 3 (Quick) | config-3-i2 | setup-variants-i.json (step 17 60 s) | 19:14-19:25 | completed, rc 0 |
| 4 (Epic) | config-4-i2 | setup-variants-i.json (step 17 60 s) | 19:28-19:38 | completed, rc 0 |
On F: in the same hour, c5h-d's setup:7-i1 (Huge, 18:59-19:27, original variants) was recorded incomplete: "the
replay could not run (exit 2: step 19 wait refused: the judge cannot read 'screen_open CityView' now: channel silent
(CONTRACT 6.4: refused, not waited on))", a harness fault (`F:/c5-64bit-work/0398/4609/setup/config-7-i1`). A C:
instance ran a configuration in about 10 min against 25-35 min on F: under the saturation. Closed as observation, each
naming its completed run: setup:6 r1, setup:3 h2, setup:4 r1 and h2 (harness faults). Left: setup:2 and 7.
After the merge of epic-0398-hi-kit 6cbe354b4 (deny.py's busy read up to 5 s, MC #6070/#6282) on c5h-b, same set-up:
| Config | Run | Variants | Time | Result |
|---|---|---|---|---|
| 2 (Quick Combat/Movement on) | config-2-i3 | setup-variants-i.json | 19:47-19:57 | completed, rc 0 |
| 7 (Huge, Standard) | config-7-i3 | setup-variants-i.json (7's own step 17 and 45 waits) | 19:57-20:09 | completed, rc 0 |
**Every configuration 1-7 now has a completed run on i2-rc1** (setup:8 absent, MC #3091). Closed as observation
(harness faults, naming the completed run): setup:2 h1 (AutoUnitCycle 0 on c5h-f), setup:7 h1. deny.py per run: all
runs before 19:47 had the 1 s bound; setup:2-i3, setup:7-i3 and the c5h-b camera repeats the 5 s bound.
**#5917 product 2 of 2 on c5h-f (impl-4609i, 2026-11-18 19:15-19:52) `[L]`; ticket 0572 (MC #6308).** `scripts-i/vr4.sh`
(fresh game from HI-KIT-MEDIEVAL-LARGE-T200, cut 1-28 `pipe-h/cut.jsonl` sha 4336eeab4341, product route), c5h-f (F:,
installer-installed i2-rc1, compositor on, 1914x1051, AutoUnitCycle 1), deny.py with the 1 s bound (before
6cbe354b4). Out `F:/c5-64bit-work/0398/4609/cam-f-i/product-1/`, `product-2/`.
- product-1: launched 19:15; the front end's Lua.log began only at 19:24:14 ("activating 3 mods"), the server's run
  folder at 19:25:05, in game 19:30 (probe 30). Stopped at step 9, exit 2 (deny-list read, channel busy, 1 s bound: a
  harness fault). Its step 7 shows the seams.
- product-2: in game 19:39 (13 probes), the whole cut replayed, 28 steps, exit 0, 19:39-19:51.
- (a) Both step-7 pictures show the runner's two straight brightness seams across the clouds (about y 365-380 and
  390-405 of 703) and the hard cloud-to-black line (about y 440-458) with black below (VCs
  vc-20261118-193725-workstation-d3ac, vc-20261118-195324-workstation-90f6). Stock on c5h-a (C:, -ia b71a9f31,
  default path, impl-4609h 16:24-16:34) shows continuous clouds (VC 9ece). Reproduced 2 of 2: a product-only picture
  difference, listed for the renderer teams as ticket 0572, not a clause-2 failure.
- (b) Step 28 took 27.2 s from step 27's record (stock about 37 s) and step 27 64.5 s (stock 76 s): no game stall; the
  runner's 171.8 s was its own model time.
- The earlier product-1 load (impl-4609h, 18:08-18:26, c5h-d) now reads as the same slow F: front end: there the
  front end had not started the load after 18 min `[I]`.
- MC #6282: the C: repeats run on c5h-b after setup:2 and 7.
- **C: repeats on c5h-b (MC #6282), 20:13-20:24, deny.py 5 s `[L]`.** product-1 (20:13-20:17) and product-2
  (20:20-20:24), each the whole cut, 28 steps, exit 0; loads took about 3 min on C: against 15-24 min on F:. Step 7
  shows the same seams and black band in both (VCs vc-20261118-201907-workstation-6694,
  vc-20261118-202500-workstation-86d0; mean pixel difference between the two 5.6/255, to stock 52.3/255). 4 of 4
  product replays (2 on F:, 2 on C:) against stock's continuous clouds. Step 28 on C:: 3.3 s after step 27 (27: 8.9
  s). Out `F:/c5-64bit-work/0398/4609/camb-i/` (#6360, #6361 on 0572).

**The phase verdict, first draft (impl-4609i, 2026-11-18 20:25:47) `[C]`.** `python -I tools/hi/phase/phase.py verdict
F:/c5-64bit-work/0398/phase/i2-rc1` (no `--previous`): FAIL, clauses 1, 2, 5, exit 6; runs 129 over 99 items; findings
48 (usability 22, observation 24, open 2). Copies `F:/c5-64bit-work/0398/4609/verdict-draft-4609i-2026-11-18.md/.json`;
text and summary to the lead only (#6364), not to the MC.
- Clause 1: charter:unit-actions and charter:production-and-purchase had no runner; charter:lifecycle-loads and
  charter:setup-options are the model-free charters carried out by items `loads` and `setup:1-8` (both complete), but the
  plan lists them as charters with no recorded run.
- Clause 2: the two setup:5 entries on c5h-d (bug 0569, open). 0572 (the cloud seams) is a picture difference, not a
  clause-2 failure; it is not in the ledger as an open finding.
- Clause 3 holds with an empty kept set; clause 4 is the baseline: UI 59 covered (135 partial, 42 excluded, 4239 open)
  of 4475, game actions 47 of 620, boundary calls 0 of 1615 (not measured).
- Clause 5: the four window-abuse pairs that ran on tb1 are recorded with the build label stock-tb1-3c2f47da, so the
  verdict does not count them as on i2-rc1.
- Gaps for 4610 `[C]`: verdict.json's summary reads minutes 0 and runner_tokens 0 (the records carry neither; the
  runner hours and tokens are only in this issue: at least 12.7 h and 3,868,714 tokens, runners 4 and 8 not written);
  `phase.py close` cannot name a bug without closing (bugonly.py above).
**Kept traces (PHASE.md 5; lead #5955 cap 3-5, stop at 18:30) `[L]`.** The kept set
(`tools/hi/phase/kept/index.json`) was empty on epic-0398-hi-kit, so there was no active set to replay. Candidates
were made with `scripts-h/keep_cut.py` (the runner's header with save and charter, every intent and step, owner
checkpoints after each judged screen change and at the end: turn and screens). They are under
`C:/c5-64bit-work/0398/4609h/kept/` (moved to `F:/c5-64bit-work/0398/4609/kept/` at the hand-back), checked for the
machine name (none), check-trace clean apart from pictures. Stock evidence on c5h-a:
- combat (runner 20, HI-KIT-AT-WAR-69CBC281): the full cut passed keep-step7 and keep-step10 and the AI turn (turn
  10), then stopped at step 21 (exit 6, "plot [55,17] not under the cursor", the step whose judge was unavailable in
  the run). Candidate `combat20/cut.jsonl` (steps 1-20, end checkpoint turn 10, screens []); stock's step 20 judge
  matched it.
- lifecycle-menus (runner 22, HI-KIT-NEAR-VICTORY-C648E6E4): passed keep-step9, then stopped at step 32 (exit 6,
  EndTurnButton not found after 6 tries). An AI-turn leader scene (Bismarck) was left open and was answered by real
  clicks afterwards. Candidate `lifecycle31/cut.jsonl` (steps 1-31, end turn 350, screens []), which stock matched at
  step 31.
- vp-events (runner 18, HI-KIT-MEDIEVAL-LARGE-T200): passed 8 checkpoints (steps 20-37), then stopped at step 42
  (exit 5, `state.turn>=203` deadline: a leader scene during the AI turn kept the channel silent, harness gap 5/6).
  The scene (Ramkhamhaeng, Bismarck) was answered by real clicks and the turn reached 203.
- bad-moments: cut made, not replayed.
- **Not offered: time (first phase).** No candidate got its 2 product replays, because the two installed product
  instances (c5h-d, c5h-f) ran setup:2-7 and #5917 throughout, under the F: saturation. So nothing was added to the
  kept set. Leader scenes in AI turns (gap 5/6) stop most turn-ending cuts, so a kept trace should end before its
  first AI turn or have the scene answered by a recorded click.
**Explorer runners (PHASE.md 8 step 4).** Runner 1, trade-deal-answer, Sonnet, c5h-a (installed i2, AutoUnitCycle 1,
1920x1080), started 03:24. Kit proofs: load ok (fingerprint f4c3e788...), start ok and "same" (session
c5h-a-20261118-032446). Scratch `runner_up.sh` lays the save, launches, closes the load screen and takes both proofs.
- Runner 1 ended 04:02: 64 steps, 185,493 tokens, 119 tool uses, no box, game left at turn 203.
- Finding, usability (step 51): VP's AI proposal popups (Research Agreement, Open Borders) have only REFUSE and
  ACCEPT, no modify, so charter part 3 is unreachable. Probably stock; not yet through the stock check.
- **Tool gap found live `[C]`:** `phase.py record` gives the first live runner session INCOMPLETE, "proof 2 (order):
  the session's trace header names no save hash". `hi.py session start` writes `save: {name: null, sha256: null}`
  (only `campaign.py harden` fills it, in the owner's copy), and `kit.check_order` requires the header's hash. So no
  raw runner session on a derived save could pass proof 2, although both fingerprints were ok and equal.
- Decision (lead #5104, option (a)): phase.py takes the hash from the run's `hi/kit-fingerprint-load.json` when the
  header has none. The result is marked `save_hash_source: fingerprint-load` and must equal the charter's derived
  save. Replaces "Not built yet." (impl-4609b, 2026-11-18 05:27).
- **As built `[C]`** (impl-4609b, commit dc153958, the lead's correction #5146).
  - kit-fingerprint-load.json carries no save hash. Its `sha256` is the SHA-256 of the fingerprint text (turn, each
    player's units/cities/gold/era, war pairs), so it can never equal a save's hash.
  - `phase.kit_proofs` therefore compares that fingerprint with the one the kit's own verify read at the derived
    save's load: `<kit.json saves_dir>/../verify/verify-<situation>.json`, or kit.json `verify_dir` when set. Its
    `save` must be the charter's save.
  - On a match, the run-book entry and its kit_proofs carry `save_hash_source: fingerprint-load`, and
    `save_from_fingerprint` holds the fingerprint, the verify file and `save_hash` (the derived SHA-256). Otherwise
    proof 2 fails: "the session's trace header names no save hash, and <why>".
  - The trace is not touched. A plan whose kit path is gone falls back to the tree's kit.json.
  - 7 hermetic tests in test_hi_phase.py: the match, a header hash that needs no fallback, a mismatch, another save,
    no verify record, no load fingerprint.
- Runner 1 re-recorded 2026-11-18 05:27: **completed**. The load fingerprint f4c3e788 equals kit verify
  `verify-ai-proposals.json` (2026-11-16, i0ship-26234371-ia DLL), so the state fingerprint at a load is the same on
  the stock DLL and on i2-rc1's x64 DLL `[L]`.
- Runner 2 save-load (c5h-a, plain Tiny save HI-KIT-ANCIENT-TINY-T8, 04:44-05:16, 42 steps, 128,007 tokens):
  completed, no finding, no box `[L]`. Three loads (game menu, quick save, front end) each came back at the saved turn
  and gold. The load screen again showed the leader picture blended with the map.
- Runner 3 war-four-ways (c5h-d, plain MEDIEVAL-LARGE-T200, 04:46-05:18, 30 steps, 121,356 tokens): completed, part 1
  only `[L]`. One finding, wrong_state at step 5: a row of the right-edge leader list opened another leader's scene.
  Row 2 opened Shoshone, the player's own civ; row 3 (Polynesia in the tooltip) opened Arabia (row 2). The scene
  looks one row behind, and the tooltip shows the clicked row. **Corrected 2026-11-18 08:13 (impl-4609d): not a fault,
  the same on stock; the tooltip in the after-picture is not the clicked row's. See "Findings 1a-1c" below.** (Replaced:
  "a row opened another leader's scene", which was the runner's reading.) The pipeline cannot harden this finding (impl-4609c,
  2026-11-18, `[C]` by reading `campaign.harden` and `report.reproduced`); see the two harness gaps below. It goes
  through the owner's visual repro instead (lead #5256): the cut of steps 1-5 replayed twice on a fresh product
  instance and once on the stock route, with step 5 judged by eye against the clicked row's tooltip and recorded in
  Visual Checks. 2 of 2 is needed to count. Cut: `F:/c5-64bit-work/0398/phase/i2-rc1/runs/war-four-ways/visual-repro/cut.jsonl`
  (harden with a `not_black` checkpoint only to obtain a validated cut, not a pipeline verdict).
- **Harness gap 1: no judge field names the diplomacy partner** (impl-4609c, 2026-11-18, `[C]` from
  `tools/hi/judge_lua.py`). The state read gives turn, player, block, gold, research, unit*, city*, mode, city_screen,
  popup, screens, ui_w/h and mouse_x/y. No field says whose leader scene is open. So a wrong-leader finding has no
  `--expect`, and the picture checks (`not_flat`, `not_black`, `not_frozen`, `no_flicker`) cannot tell two leaders
  apart. `report.reproduced` counts only a failed checkpoint, so any checkpoint added would pass on every replay and
  record "not reproduced". Adding a field is outside 0398's touches and is the MC's to grant.
  - Product 2 of 2 by eye `[L]`, 2026-11-18, c5h-a, i2-rc1 installer-installed, compositor on, fresh game each:
    run 1 06:34-06:36 (vc-20261118-063902-workstation-cf8b), run 2 06:48-06:50
    (vc-20261118-065132-workstation-9d05). Both show the same picture at step 5: the tooltip of the clicked row
    names Polynesia (Kamehameha), while the scene and speaker are Harun al-Rashid of Arabia.
  - The stock replay by `stockctl.RouteSwitch` (vpproxy.ini mode=off) on the installed c5h-a reached no game. The
    load screen stayed up, the Lua channel was silent (state Main), and nothing was pressed. This is the known gap of
    installer-notes 14.5 and MC #3333: an installed instance's stock route has no Lua channel, and stock runs on the
    default stage-laid path (14.14). It needed the MC's Lua-free stop (#5330, rc 0 at 07:10). vpproxy.ini went back
    by hash, 11d2cf60 -> f80d7590. Do not use RouteSwitch on an installed layout; `campaign.py stock` does exactly that.
  - `campaign.py repro` and `stock` cannot take a finding whose cut ends in a leader scene `[L]`. Their `down()`
    runs `cli.py stop` with the human-turn hook, which waits out the 300 s front-end timeout while the judge is
    silent, and leaves the game running (exit 6). The owner's runs pressed Esc before each stop (`scripts-c/vr.sh`).
  - Stock control on the default stage-laid path `[L]` (c5h-a, `set-dll` with the pin 39f17f75's -ia DLL
    b71a9f31, no proxy; 07:22-07:23). Step 1's click on leader-list row 2 opened no scene, where product had opened
    Shoshone's. So the step-4 ESC opened the game menu, and step 5's click landed on it
    (vc-20261118-072639-workstation-3f6f, vc-20261118-072640-workstation-77f3). Stock shows no wrong-leader scene,
    but it parted from product at step 1, so it neither confirms nor rules out the fault `[I]`. Handed to the lead for
    review (#5359).
- **Harness gap 2: the judge channel is silent in leader scenes** `[L]`. war-four-ways steps 3, 5 and 6, all in a
  diplomacy scene on product i2-rc1 (c5h-d, 2026-11-18 04:46-05:18), read `lua: no answer within 8 s`. A judge field
  for gap 1 would also need an answer while a scene is open. **Stock too** (impl-4609d, 2026-11-18 08:07, c5h-a, the
  pin's -ia DLL on the default path): `judge` `available` false in Arabia's and Polynesia's scenes on both routes.
- **Findings 1a-1c, owner controls (impl-4609d, 2026-11-18 08:04-08:34) `[L]`.** Product: installer-installed i2-rc1,
  compositor on (c5h-f; c5h-d for 1c run 2). Stock: c5h-a on the default stage-laid path, `provision.py set-dll c5h-a
  --dll <the pin 39f17f75's -ia DLL b71a9f31>`, no proxy. Both 1920x1080 windows, client 1914x1051, mods cp,vp,eui,
  `hi.py` sessions at width 1280, the same points on both routes, a fresh game per run. No box anywhere.
  - **1a, runner 3's leader rows: not a fault, identical on stock.** New short trace from the map on
    HI-KIT-MEDIEVAL-LARGE-T200 (e3da1d17), turn 200 (`runs/war-four-ways/leader-rows/{product,stock}-1`). The right-edge
    leader list sits at the same place on both routes (shot px x 1250): row 1 y 67 The Shoshone (Pocatello, GUARDED,
    an AI civ, not the player's), row 2 y 107 Arabia, row 3 y 147 Polynesia, about 40 px apart. Hover row 2: Arabia's
    tooltip; click: Arabia's scene; ESC: the map (judge `screens []`). Row 3: Polynesia's tooltip, Polynesia's scene,
    ESC to the map. The same on both routes. While a scene is open the list stays drawn at the right edge, and the
    tooltip in the after-picture belongs to the row now under the resting cursor (Polynesia's in Arabia's scene,
    England's in Polynesia's), not to the clicked row. Runner 3's y 70 is row 1 (Shoshone, right); its y 100 lies on
    row 2's upper edge (Arabia, right). Visual Checks (product, stock as reference): vc-20261118-081058-workstation-a04c,
    -081100-12a1, -081102-384c, -081103-8dcf, -081105-e1e6, -081108-ee79, -081109-e519, -081111-9422. The 07:22 stock
    control's step-1 miss (no scene) is not explained; the 08:04 stock run opened scenes at the same points `[I]`.
  - **1b, runner 9's spy sent to the neighbour row: not a fault, identical on stock.** HI-KIT-MODERN-HUGE-T350
    (c499d3c6), turn 350 (`runs/espionage/spy-rows/{product,stock}-1`, 08:19-08:23): Espionage Overview (top-bar
    1144,35), Move on Ahmed (428,241), Their Cities wheel -5 at (785,440), hover (785,477) = "Shubat-Enlil is
    controlled by Assyria", click: Ahmed travels to Shubat-Enlil, 3 turns. Both lists are rebuilt on Move (unavailable
    rows greyed first) and again after the click, so Nineveh's row and tooltip then sit under the resting cursor.
    Runner 9's own pictures agree: at its step-21 click (785,477) step 20's list showed Aksum's row there; Assur sat
    there only after the rebuild. Visual Checks: vc-20261118-082251-workstation-56e9, -082252-899e, -082253-3243,
    -082255-b7e2, -082257-0d3a.
  - **1a and 1b are one cause, a reading error, not a row-to-player mapping fault:** the runner judged the clicked row
    by the after-picture, where a rebuilt (espionage) or still-drawn (leader list in a scene) list puts a neighbour row
    under the cursor. The mapping is the same on both routes: it is the UI Lua's (EUI's leader list, the espionage
    overview's instanced rows), which runs in the EXE on both routes `[I]`. Kit/brief gap: a runner should name the
    clicked row from the before-picture's tooltip (hover first, then click). **Rule for the runner template (lead
    #5452, accepted 1a and 1b as not faults):** every runner brief says: judge a clicked row or target by the tooltip
    in the BEFORE-picture (hover, picture, then click), never by the after-picture, since lists are rebuilt or redrawn
    shifted. Sent to the running runners 12 and 13 at 09:15 (their briefs had started without it), and in every brief after; `tools/hi/charters` unchanged (the lead's follow-up).
  - **1c, runner 10's combat preview beside Social Policies: stock too.** `campaign.py --index <phase
    charters.json> harden runs/social-policies --finding 1 --charter social-policies --checks not_black --out
    runs/social-policies/visual-repro` gave the cut (steps 1-23, checkpoint `owner-f1-step23`). `scripts-d/vr2.sh`
    replays (fresh game, `hi.py replay`, Esc, stop): product c5h-f 08:28-08:30 and c5h-d 08:29-08:33, stock c5h-a
    08:29-08:33, each exit 0, 23 steps, stop rc 0. Step 23 (hover the Artistry icon 883,548 with the popup open)
    shows the combat preview (MINOR DEFEAT) and unit panel beside the popup on product 2 of 2 and on stock; step 22
    (hover empty world 1000,300) shows none on either. Cause `[I]`: the popup lets the world's plot mouse-over
    through, and the enemy-unit panel answers the plot under the icon. Visual Checks: vc-20261118-083534-workstation-d7aa,
    -083535-8982, -083538-ea39.
- **Harness gaps 3 and 4 (impl-4609d, 2026-11-18, `[C]` by reading `tools/hi/checks.py` and `campaign.py`).**
  3: no picture check witnesses an added panel: `checks.CHECKS` is not_flat, not_black, not_frozen, no_flicker,
  changed, and a `--changed` finding counts as reproduced when the region did *not* change (input_ignored). So a
  wrong_picture of the "something extra is drawn" kind is judged by eye. 4: `campaign.py stock` always goes through
  `stockctl.RouteSwitch`, which refuses an instance without `vpproxy.ini` (the stage-laid default path) and is barred
  on installed layouts (14.5), so the pipeline has no stock step this phase can use; stock ran as `hi.py replay --route
  stock` of the same cut on c5h-a.
- **Kit gaps of the phase's runners** (lead #5290). A charter run whose parts the kit could not reach still counts as
  run (PHASE.md 2, clause 1); the parts are clause-4 'open'. Not repaired in this phase; the lead opens the follow-up.

  | Charter (runner) | Situation (save) | Parts unreachable | Why | Set-up that would reach them |
  |---|---|---|---|---|
  | peace-and-relations (4) | HI-KIT-AT-WAR-69CBC281 | 2, 4, 5 (take peace, friendship, embassies/deal) | only one met major (Portugal) | at war with one major while at peace with two or more others, embassies possible |
  | ai-demands (5) | HI-KIT-AI-DEMAND-9101BEC0 | 1-5 (demand popup, give, refuse, reaction, keyboard answer) | no AI demand came in the 3 allowed turns (turns 9-12) | an AI's demand due on the next AI turn (verified by the kit's own check) |
  | world-congress (6) | HI-KIT-MEDIEVAL-LARGE-T200 | 2-5 (proposals, votes, special session, session popups) | session 9 turns away; units awaiting orders block the end of turn | session within 1-3 turns, a delegate to spare, no unit awaiting orders or promotion |
  | city-states (7) | HI-KIT-MEDIEVAL-LARGE-T200 | 2 (gold and improvement gift), 3 (pledge), 4 (tribute, worker demand) | two city states met, both Angry; 38 gold; the military too weak for a pledge | 200+ gold, a city state at neutral or better, a strong military for tribute |
  | ideologies (8) | HI-KIT-ERA-INDUSTRIAL-B86C97E9 ("ideology-ready", turn 250) | 1-3, tenets of 5 | ideologies locked: 8 policies left (18 needed), culture 2452/4267 | one policy short of the unlock with culture for it within a turn, or the choice popup due |
  | religion (11) | HI-KIT-RELIGION-STAGES-7B26B27E (no-pantheon stage, turn 9) | 3 founding, 4 enhancing/reformation, 5 missionary/inquisitor/prophet | the save is the first stage only: no prophet, no religious unit | the later stages' saves (a prophet ready, a founded religion with faith for units) loaded in the same charter run |
  | trade-routes (12) | HI-KIT-TRADE-ROUTES-0F28F7A4 (turn 200) | by sea, plunder, the third route | no cargo ship (the kit promises one; only a Galleass and a Trireme), route slots 3/3, no enemy route found | a cargo ship ready in a coastal city, a free route slot, an enemy trade route within reach of a military unit |
  | archaeology (13) | HI-KIT-ARCHAEOLOGY-0D678F97 (turn 350) | the artifact choice popup, an artifact in the Culture Overview | the dig needs 4-5 turns and the turn is blocked by production choices, promotions and about 60 idle units (one turn took the hour) | an archaeologist one turn from a finished dig, no unit or city awaiting orders |
  | air-units (14) | HI-KIT-ATOMIC-HUGE-T350 (turn 350) | carrier movement, a plane on a carrier, rebase onto a carrier, an interception reported | no carrier in the save (judge snapshot has none); the end of turn is blocked by a promotion and many units awaiting orders | a carrier with one plane aboard next to an enemy within range, no unit awaiting orders or promotion |
  | vp-vassals (16) | HI-KIT-ATOMIC-HUGE-T350 | 1 (a strong AI offers vassalage), 2 (a weak AI accepts a vassal deal), 4 (tax change), 5 (independence) | we are Rome's vassal in this save: no vassal can be taken and no offer comes; no tax or independence control was found | a save where we are strong beside a weak AI at war with us, and one where we are a vassal 10 or more turns in |
  | vp-corporations (17) | HI-KIT-MODERN-HUGE-T350 | 1 (found a corporation), 4 (build an office) | the save's corporation (Giorgio Armeier, HQ Memphis, turn 293) is already founded; no HQ or office is offered in any build list; ending a turn took about 35 steps | a civ one step from founding (the founding building available), no unit awaiting orders |
  | vp-events (18) | HI-KIT-MEDIEVAL-LARGE-T200 | 1-3 (a player event, a city event, their choices) | no event came due in turns 200-203 | a save with a VP player or city event due on the next turn |
  | great-people (19) | HI-KIT-GREAT-PEOPLE-E9663E20 (turn 200) | the merchant's trade mission, the diplomat's mission, the admiral's actions, a golden age started by an artist | no city state within one turn of a merchant or the diplomat; no Great Admiral in the save; the one artist used for points | a merchant and a diplomat one move from a city state's land, a Great Admiral by a coast, two artists |
  | lifecycle-menus (22) | HI-KIT-NEAR-VICTORY-C648E6E4 (turn 350) | part 3/4: a defeat reached by play | the kit has no near-defeat save (the load list holds none) | a save one AI turn from the player's defeat (last city under siege) |
  | awkward-input-and-stale-selections (24) | HI-KIT-ANCIENT-TINY-T8 (turn 8) | the weak city captured while its screen is open; a unit killed by the enemy with its hotkeys pressed after | no enemy army in sight within the 3 allowed turns | an enemy army one turn from a weak own city, an own unit in reach of an enemy attack |
  | combat (20) | HI-KIT-AT-WAR-69CBC281 (turn 9) | 1-4 (melee, ranged and city strike, capture popup, unit lost); 5 only partly (one turn ended, no counterattack) | at war with Portugal but no Portuguese city or unit is visible or located (judge enemy_cities empty, x,y -1,-1); own units only a Warrior and a Pathfinder; the charter allows one turn | an enemy city and units in sight and within one turn's reach of several own melee and ranged units (or the map revealed) |
  | nuclear (15) | HI-KIT-NUCLEAR-5DA39651 | part 2 cancel the confirmation (none came), the enemy city itself (Nippur 7 plots from the bomb, range 6) | the kit's "enemy city in range" does not hold; about 230 F presses and three promotions to end one turn | the bomb within range 6 of an enemy city, no unit awaiting orders or promotion |
- **Window abuse, first window (MC #5176) `[L]`: three of the four pairs ran.** Results read by impl-4609c,
  2026-11-18 06:10.
  - Laptop job 20261118-050752-91f4, stock route on tb1's own DLL (lead, 05:01), with the four load and AI-turn pairs
    built from 4602's traces, unchanged by hash from 4603's copies. Ran 05:57-06:04, earlier than planned, rc 0.
    tb1 was restored by hash: `restore_ok` true, `provision_verify_rc` 0 (`out/live/session.json`, fetched to
    `F:/c5-64bit-work/0398/4609/wa/fetch-91f4`).
  - Recorded in the run book with build `stock-tb1-3c2f47da` (`unnamed_build`, not the candidate):
    - `window-abuse:load:alt-tab`: completed, gap 656 ms, moment tested (inferred). Finding
      `wrong_state-window-abuse-load-alt-tab-cmd1`: part B's checkpoint `loaded` read turn 8, expected 9.
    - `window-abuse:load:minimise`: completed, gap 313 ms, moment tested (inferred). Same finding: turn 8, expected 9.
    - `window-abuse:ai-turn:alt-tab`: completed, gap 359 ms, moment tested (read), no finding.
    - `window-abuse:ai-turn:minimise`: **not run**. The reset trace between the two AI-turn pairs stopped with rc 6
      at the same checkpoint `loaded` (turn 8, expected 9), so the driver ran no further.
  - All three `loaded` failures, and the reset's, are the same mismatch on tb1's stock DLL. The window action
    therefore did not cause them. The checkpoint's turn expectation (9, from 4602's recording) does not match the save
    as tb1 loads it (turn 8). This is a harness and trace finding, not a product one `[I]`. To confirm: read the
    loaded save's turn in 4602's recording against tb1's copy of the save.
  - **Read (impl-4609d, 2026-11-18 08:30, from job 91f4's fetched traces and pictures) `[L]`:** the game starts from
    `HI-PH-WIN-T8` and reads turn 9 at the `start` checkpoint, both on 2026-11-16 (4602) and in job 91f4 (start ok,
    turn 9). The load trace's step 3 clicks the Load list's **top row** (sorted by last modified). In job 91f4 that row
    was `TBLIVE-HI-PH-WIN-T8`, its panel "8 Turns, 2026-11-18 5:08 AM" (`out/live/wa-load-alt-tab/a/step_0003.png`),
    and the load gave turn 8. In 4602's and 4603's runs the save pairs ran first: the save trace types
    `TBLIVE-HI-PH-WIN-T8` and overwrites it at turn 9 (research 15.x, "save (6 steps ...)"), so the top row loaded at
    turn 9. Job 91f4 ran no save pair, so the top row was an older `TBLIVE-HI-PH-WIN-T8` at turn 8 `[I]` for where that
    file came from. **The trace is not at fault; no edit.** The load pairs and the reset depend on a save step having
    run first in the same job: a job without the save pairs runs one save step (the save trace's part A) first.
    Where the turn-8 file comes from `[C]` (`tools/testbed/jobs/live_session.py`, `SAVE_PREFIX = "TBLIVE-"`): the job
    copies its `--load` save into the instance as `TBLIVE-<name>` and loads that, so the top row is the job's own copy
    of HI-PH-WIN-T8, and 4602's save trace types exactly that name and overwrites it. Why the same file reads turn 9
    at the job's start load and turn 8 at the in-game load is not read `[I]`.
- **Item 36, the install job for the second window (impl-4609d, 2026-11-18) `[L]` on the workstation.**
  - `tools/hi/phase/jobs/install_job.py PLAN.json [--bin DIR] [--out DIR] [--dry-run]` (lead #5272, #5273): checks the
    installer and the player DLL by SHA-256 and refuses an instance folder under a forbidden write root (Steam, the owner's
    folders) or naming `steamapps`; backs up by SHA-256 the four inis, every ModUserData file (EUI's options db) and
    the Community Patch DLL; runs the plan's `before_install` commands; lays VP's own DLL (`set-dll --dll`, 262aee88)
    and runs the installer silently in test mode with this instance's /DIR and /GAMEDIR only (`run_installer.py`);
    sets AutoUnitCycle; runs `after_install`; then always (unless a game of the instance still runs: exit 6, nothing
    removed) the installer's removal, files back by hash, the DLL back by `set-dll`, `provision.py verify` as the last
    line. Tests: `tools/harness/tests/test_hi_install_job.py` (8). The tb1 plan:
    `tools/hi/phase/jobs/plan-4609-window2.json` (stock session: ai-turn:minimise; product session on HI-PH-WIN-T8: the
    T8 save step, load and AI-turn pairs, the reset; product session on HI-KIT-MODERN-HUGE-T350: the save pairs;
    `ready.json` removed between sessions; live_session's own exit 7 accepted on the installed layout).
  - **Trial on c5h-a, 2026-11-18 08:48-09:05 EST: exit 0.** `install_job.py C:/c5-64bit-work/0398/4609/trial/plan-trial.json
    --out F:/c5-64bit-work/0398/4609/trial/run1`; the inner command (`scripts-d/trial_inner.sh`) launched
    HI-KIT-MODERN-HUGE-T350 on the installed layout (product, composite), held it while the Huge save trace was
    recorded, then Esc, stop rc 0, frontend restore rc 0. set-dll player rc 0 (4 s), installer install rc 0 (9 s),
    installer remove rc 0 (5 s), all 8 backed-up files equal by hash, DLL back to 3c2f47da, `provision.py verify` rc 0.
    No box (dialogwatch marker rc 0 on c5h-a, c5h-d, c5h-f at 09:06). Note: `realinput.py job` refuses on the
    workstation (fence, D63), so the trial's inner command was a harness launch, not the job's real-input session.
  - **The Huge save trace** `F:/c5-64bit-work/0398/4609/rec-save-huge2/trace.jsonl` (product route, c5h-a installed
    i2-rc1, client 2554x1411, 09:03-09:04, check-trace ok, no warning): checkpoint `start` turn 350; ESC; SAVE GAME;
    END + 40 BACKSPACE + type `TBLIVE-HI-KIT-MODERN-HUGE-T350` (the name live_session gives the loaded save on tb1, so
    the overwrite prompt comes on both machines; a copy by that name was laid on c5h-a for the recording and removed
    after); Save; Yes (`screen_closed /InGame/GameMenu/SaveMenu` 1078 ms); RETURN TO GAME; checkpoint `saved`.
    **Size check:** the file's mtime came 2.2 s after the Yes click (09:03:54.9 -> 09:03:57.1, the click time
    from the trace's ms, +-1 s); an earlier recording on the same save gave 1.5 s. So the workstation is at the 2 s
    line; the laptop's own timing decides (the pair's record gives it). The largest standard kit save is
    HI-KIT-INFORMATION-HUGE-T375 (2.63 MB against 2.23 MB). A first recording (`rec-save-huge`) is void: its RETURN
    click missed (point read wrongly), step 6 failed.
  - Pairs built offline into `F:/c5-64bit-work/0398/4609/wa2/` (`scripts-d/wa2_build.sh`, --expect-min 70): product
    save, load and AI-turn pairs (alt-tab, minimise), stock ai-turn:minimise, the reset and the T8 save step.
  - The one real move the driver makes at the start (research 15.8 (a)) was refused: "the game is not the
    foreground window (foreground: explorer.exe pid 5080): no real input" (rc 2). The pairs after it delivered their
    input.
  - No tb1 job op runs an installer today (live_session's ops: lua, shot, cdb, cli, frontend, turns, burst, wait,
    stop; realinput job adds realinput, hi, decoy). live_session's restore also ends in `provision.py verify`, which
    fails while an installer layout is present. So the product route on the laptop needs a new job wrapper `[C]`.
  - The save pairs need a recording on the lane and a `window-abuse build` between recording and replay, which an
    unattended `--script` job cannot do.
- **Item 36, second window: laptop job `20261118-095459-f29c` (impl-4609e, 2026-11-18 09:55-09:58 EST) `[L]`: no
  product pair ran; tb1 restored.** `install_job.py` with `plan-4609-window2.json` (commit 7d4646767), quiet headed,
  `--expect-min 70 --timeout 5400`, MC #5546 window, one submit. Fetched to `F:/c5-64bit-work/0398/4609/wa2e/fetch-f29c`.
  - Session 1 (stock, tb1's own DLL 3c2f47da, HI-PH-WIN-T8): in game after 113.6 s (pid 3716). The one real move was
    refused, "the game is not the foreground window (foreground: explorer.exe pid 5080)" (as in job 91f4).
    `window-abuse:ai-turn:minimise` (stock) **incomplete**, gap_ms None, moment_tested false: part A stopped before
    its first input, "no usable picture: flat frame (std 0.6) (printwindow, 2 retakes)". So 15.8 (a)'s flat
    PrintWindow comes back when the move is refused; `realinput.py focus` (no key sent) before the move is the fix to
    try. The driver now sends it before the move and before every step (`scripts-e/run_wa_e.sh`); untested live.
  - Install: set-dll player rc 0, installer install rc 0 (5 s), AutoUnitCycle 1.
  - Sessions 2 and 3 (product): **launch refused by the memory gate**, "free commit 5.9 GB is below the floor of
    6 GB" (`out/install-job/after_install-2.log` and `-4.log`, line 97). Session 1 had started at 6.2 GB. Each
    session ended with exit 7 within 6 s, and the plan's `ok_rc` [0, 7] (meant for live_session's end verify on an
    installer layout) let the job go on. So save-t8, the two load pairs, both product AI-turn pairs, the reset and both
    save pairs did **not run**. Lessons `[C]`: a plan that accepts 7 must also check `launch_rc` 0 (session.json);
    the laptop's free commit is only just above the 6 GB floor before a game, so a product launch right after a stock
    game needs a wait for free commit (or fewer other processes on the laptop).
  - Restore (job exit 0): installer remove rc 0; config.ini, GraphicsSettingsDX11.ini, UserSettings.ini and the
    ModUserData files equal by hash; DLL back to 3c2f47da (set-dll rc 0); `provision.py verify tb1` rc 0, the last
    line. GPU driver untouched (no driver step in the job).
  - **Harness slip found before the submit `[C]`:** `phase.py window-abuse build` names a pair's bin folder
    `wa-<moment>-<act>` (`phase.py` near line 1606), so a stock and a product pair of the same moment and act ship
    to the same `bin/` folder and write the same job out folder. The wa2e copy of the stock pair was renamed
    `wa-s-ai-turn-minimise`. Kit/harness gap for 4610: the bin name should carry the route.
  - **The fixes for the next window (lead #5588, commit b3ebfcfe5) `[C]`, untested live.** `install_job.py`:
    before every game session (`realinput.py job` or `live_session.py`), it waits until the previous session's game
    pid has ended and the instance no longer runs, then until free commit >= the game floor (`C5_MIN_FREE_COMMIT_GB`,
    else 6 GB) + `commit_margin_gb` 0.5. Each wait is at most `memory_wait_s` 600 s, polled every 10 s, and the
    numbers go into the step's `wait`. When the memory never comes, the session is recorded `not_run: memory ...`,
    no later command runs, and the job goes on to the restore. After a session, its own fresh `out/live/session.json`
    must show `launch_rc` 0, or the session fails even when its exit is in `ok_rc`. `realinput.py job --start-focus
    X,Y` runs `realinput focus` and one real move to client X,Y before the first command; the plan passes 1900,1000
    to all three sessions. 11 hermetic tests in `tools/harness/tests/test_hi_install_job.py` (fake gate, fake
    launcher, fake process table). Next window: `--expect-min 80 --timeout 7200`; pairs staged in
    `F:/c5-64bit-work/0398/4609/wa2f` (`scripts-e/wa2f_stage.py`, `files-f.txt`).
- **Item 36, third window: laptop job `20261118-102347-8ebd` (impl-4609e, 2026-11-18 about 10:33-10:54 EST, MC
  #5603) `[L]`: five of the seven pairs completed with the moment tested, no finding, no box; tb1 restored.** Source
  b93ca63f5, `--expect-min 80 --timeout 7200`, one submit (#5608), result #5618. Records in
  `F:/c5-64bit-work/0398/4609/wa2f/<pair>/window-abuse.json`, job files in `wa2f/fetch-20261118-102347-8ebd`.
  - Waits (b3ebfcfe5, live for the first time): session 1 waited 230 s for free commit (need 6.5 GB, then 7.41 GB);
    sessions 2 and 3 found 7.31 and 7.39 GB at once; the previous game was gone each time within 0.2 s; launch_rc 0
    in all three. The start focus answered "already" and the move rc 0: no flat frame this time.
  - Stock, tb1's own DLL: `window-abuse:ai-turn:minimise` **completed**, gap 297 ms, moment tested (read).
  - Product, installer-installed i2-rc1, compositor on, HI-PH-WIN-T8: save-t8 replay rc 0, then
    `load:alt-tab` **completed** gap 656 ms (inferred), `load:minimise` **completed** gap 297 ms (inferred),
    `ai-turn:alt-tab` **completed** gap 359 ms (read), reset rc 0, `ai-turn:minimise` **completed** gap 297 ms
    (read). The turn-8/9 load mismatch of job 91f4 did not come back once save-t8 ran first (36(a) confirmed).
  - Product, HI-KIT-MODERN-HUGE-T350: `save:alt-tab` **incomplete** (gap 656 ms; the save had ended when read 281 ms
    after the action), `save:minimise` **incomplete** (gap 296 ms; ended when read 1891 ms after). The laptop saves
    this Huge save faster than the workstation's 2.2 s, so the action lands after the save; a longer save
    (HI-KIT-INFORMATION-HUGE-T375, 2.63 MB) or a shorter `--delay-ms` is the next try `[I]`.
  - Restore: installer remove rc 0, the 5 backed-up files equal by hash, DLL by hash (set-dll back rc 0),
    `provision.py verify tb1` rc 0 as the last line; exit 0. No pair recorded in the run book yet (`phase.py record`
    after the lead's review).
- **Findings of runner 12 (trade-routes): wrong_state-step40 and wrong_state-step43, both stock too (impl-4609e,
  2026-11-18 10:07-10:19 EST) `[L]`.** `campaign.py --index <phase>/campaign/charters.json harden runs/trade-routes
  --finding 2|3 --charter trade-routes --expect '{"turn": 201, "gold": 50}' --out runs/trade-routes/repro-f2|f3`. The
  cut of f3 (steps 1-43) covers both findings and was replayed (`scripts-e/repro3.sh`, `vr3.sh`: fresh game from
  HI-KIT-TRADE-ROUTES-0F28F7A4 (0f28f7a4), `hi.py replay`, Esc, stop) on c5h-d and c5h-f (product: installer-installed
  i2-rc1, compositor on, AutoUnitCycle 1) and c5h-a (stock: the pin's -ia DLL b71a9f31, `prep_inst set`,
  AutoUnitCycle 1 as on c5h-d/f; `scripts-e/lay_stock_a.sh`). All mods cp,vp,eui, 1920x1080 windows. Each replay
  exit 0, 43 of 43 steps, checkpoint `owner-f3-step43` (turn 201, gold 50) passed, stop rc 0, no box.
  - **step40:** the top bar at turn 201 reads -8 gold per turn with treasury 50 on product 1, product 2 and stock,
    identical to the runner's picture. The treasury rose 38 -> 50 over the turn while the bar had shown +9 at step 17;
    that the per-turn forecast and the treasury change differ is the same game logic on both routes. **Stock too, not
    product-only.**
  - **step43:** Trade Route Overview, Your Trade Routes lists only Rotterdam-Stockholm after the caravan was sent
    Amsterdam-Samoa at step 17, on product 1, product 2 and stock alike. The caravan 13610 left the selection after
    the confirm (step 17's judge: selection moved to a missionary); a second caravan 13711 on the same plot is what
    step 41 selected. Why the route is not listed is not read `[I]`; it is the same on stock. **Stock too.**
  - Visual Checks (product, stock as reference): vc-20261118-101918-workstation-4fe7, -101919-2866, -101920-cca2,
    -101920-1bde.
- **Runner 13's wrong_state-step89 (the dig did not hold across a turn): not yet decided (impl-4609e, 2026-11-18
  10:21-10:40 EST) `[L]`.** `harden runs/archaeology --finding 1 --charter archaeology --expect '{"turn": 351}' --out
  runs/archaeology/repro-f1` (cut steps 1-89). Replayed the same way as above from HI-KIT-ARCHAEOLOGY-0D678F97
  (0d678f97) on c5h-d, c5h-f (product) and c5h-a (stock): **all three stopped identically at step 73**, exit 2,
  "step 73 wait refused: the judge cannot read 'state.turn>=351' now: channel silent (CONTRACT 6.4: refused, not
  waited on)", 72 steps done. Step 72 ended turn 350; the Lua channel then stayed silent for over 10 minutes (games
  at 24-64 % CPU, not hung, 10:40), so `cli.py stop` refused with rc 6 ("Lua channel silent ... for 300 s") on all
  three and the games were left running. (Corrected 2026-11-18 11:12 by impl-4609f: this was first read as a long AI
  turn; the silence was a leader scene waiting for the human, see the next entry.)
- **The turn-350 silence of the archaeology cut is a leader scene waiting for the player, not a long AI turn and not
  a hang (impl-4609f, 2026-11-18 11:05-11:11 EST) `[L]`.** Same three games as above (c5h-d and c5h-f: installer-
  installed i2-rc1; c5h-a: stock, -ia DLL b71a9f31), silent since 10:29-10:30, no box.
  - Progress read without Lua (`scripts-f/prog.py`: GetProcessTimes, IsHungAppWindow, WM_NULL SendMessageTimeout,
    newest file under ModdedSaves/Saves), 11:05:07 -> 11:06:11: CPU c5h-a game 1792.4 -> 1816.3 s; c5h-d game
    1666.8 -> 1696.3 s, server 669.6 -> 684.2 s; c5h-f game 1670.6 -> 1706.2 s, server 685.4 -> 701.7 s. All windows
    answered (IsHung 0, WM_NULL ok). Newest save AutoSave_Post_0350 at 10:29:05-10:30:20, unchanged.
  - Per thread over 30 s (`scripts-f/thr.py`, thread query only, 11:07:03): the busiest thread of every game and
    server grew 4.5-5.1 s (15-17 % of a core), the next ones 1-2.5 s. No thread computed: an AI turn runs one thread
    at a full core. The CPU that grew is the idle render and worker threads.
  - Pictures (Visual Checks, info, 11:07): all three show the same leader scene, Bismarck of Germany, GUARDED: "We
    have taken notice of the religious conversions you are undertaking inside our empire. We must ask you to cease
    this activity immediately!", answers "We will send our missionaries and prophets into other realms." / "Our
    missionaries and prophets go where they please." Stock and product identical: the original game's behaviour.
  - Hover, picture, click on the first answer (956,984 in the 1914x1051 client), then Back (956,1018), 11:08-11:09.
    The channel answered again at 11:09:52-11:09:56 (judge: turn 350, popup false); AutoSave_0350 written
    11:10:00-11:10:06; normal stops (`down.sh`, `--human-turn-hook tools.harness.turns:stop_readiness`) rc 0:
    c5h-a 11:10:59, c5h-d 11:11:36, c5h-f 11:12:13. No dump and no override stop were needed (MC #5628 not used).
    Restored 11:12-11:13: c5h-a `scripts-f/base_a.sh` (prep_inst restore ok, DLL 3c2f47da, verify rc 0); c5h-d and
    c5h-f `prep_inst.py restore` ok (settings by hash, copied kit saves removed; i2-rc1 stays installed).
  - **Harness gap 6 `[L]`:** while a leader scene waits for the player inside the AI turn, the Lua channel is silent
    and `judge.py state`, the replay's `state.*` waits and `cli.py stop` see only "channel silent"; the scene is
    visible only in a picture. Runner 13's cut and the step-73 refusal ran into this scene, not into a long AI turn.
    For 4610: when the channel is silent past a short bound, take a picture (or read the leader-scene window state
    without Lua) and report "leader scene waiting" instead of "AI turn".
- **The load-screen blend (runner 2's observation) is stock too (impl-4609f, 2026-11-18 11:22 EST) `[L]`.** c5h-a,
  stock (-ia DLL b71a9f31, 1920x1080, cp,vp,eui), HI-KIT-AI-PROPOSALS-563F19B1 (563f19b1): the load screen, pictured
  at `load_screen_ready` before it was closed (`scripts-f/stk.sh`, Visual Checks info), shows William's portrait and
  text over the loaded map, the map visible through the portrait. Same as runner 2 saw on the product: the original
  game's look, not a fault.
- **Runner 1's usability finding (no modify on AI proposals): stock check (impl-4609f, 2026-11-18 11:24-) `[L]`.**
  A replay of runner 1's steps 1-48 (`scripts-f/mkcut.py`: a usability finding cannot be hardened, contract 10:
  "a usability finding is never a report") on c5h-a stock stopped at step 7, exit 6, "control
  /InGame/WorldView/ActionInfoPanel/EndTurnButton not found on this client (1914x1051) after 6 tries": a PROMOTE A
  UNIT block that the product run did not meet at that step (pictures `F:/c5-64bit-work/0398/4609/stock-checks/
  r1-cut/stock-1/`). The check runs instead as a stock runner (trade-deal-answer charter, route stock,
  `scripts-f/runner_up_stock.sh`, out `F:/c5-64bit-work/0398/4609/stock-checks/r1-runner`, started 11:39).
  **Result: stock too, not a finding.** The stock runner (Sonnet, 11:39-12:11, 93 steps, 171,641 tokens, no box; load
  proof f4c3e788 ok) played turns 200-203. All three proposals came as leader scenes in turn 202's AI turn
  (Kamehameha: Research Agreement; Casimir III: Open Borders for 1 gold per turn; Alexander: Research Agreement),
  each with only REFUSE and ACCEPT and no modify (pictures `r1-runner/step_0060.png` to `step_0072.png`). Hovering
  the buttons showed no tooltip. The Lua channel was silent during each scene (harness gap 6).
- **Runners 14 on (impl-4609f, 2026-11-18) `[L]`.** Sonnet, product route (installer-installed i2-rc1, 1920x1080
  client, hi width 1280, AutoUnitCycle 1, cp,vp,eui), prompts with the before-picture rule (#5452) and the
  F-then-end-turn rule; kit proofs at load ok; recorded with `phase.py record --build i2-rc1`.

  | # | Charter | Instance | EST | Steps | Tokens | Box | Result |
  |---|---|---|---|---|---|---|---|
  | 14 | air-units | c5h-d | 11:19-11:38 | 71 | 190,100 | none | completed. Rebase, intercept, air sweep work. input_ignored-step38 (Bomber in Air Strike Mode: clicks on four enemy units in range do nothing, no preview) and input_ignored-step58 (Triplane strike on the AA gun ignored where an air sweep on the same plot worked): pipeline below |
  | 15 | nuclear | c5h-f | 11:19-11:47 | 70 | 200,277 | none | completed. Strike, fallout and one turn (to 351) done. wrong_state-step31 (ESC in nuke mode cancels it and opens the game menu), usability-step70 (no confirmation before the strike; real picture step_0037), wrong_picture-step70 (judge's selected unit UNIT_ANTI_TANK_GUN while the panel shows LIGHT TANK, steps 14-38; runner 13 saw the same on the archaeology save): all stock too (below) |
  | 16 | vp-vassals | c5h-d | 11:53-12:27 | 77 | 206,722 | none | completed. The save has us as Rome's vassal (tooltips only). usability-step77 (no vassal-tax line in the gold tooltip; -33 shown, +280 after the turn); usability steps 2-3 (Huge map: diplomacy buttons drawn over the leader list, labels cut: "DECLARE WA", "Request Hel"; a stale leader tooltip until the mouse moved). One turn (350 -> 351, about 16 s AI time) |
  | 17 | vp-corporations | c5h-f | 12:02-12:42 | 103 | 252,638 | none | completed. Overview, tabs, hovers work; a caravan route sent (franchises 18/21 -> 19/22 over 2 turns, not shown to be ours); turns 350 -> 352 (leader scenes Montezuma and Bismarck x3 answered). usability-step102: a ghosted RANGED ATTACK combat preview and an IRONCLAD info panel bottom left behind the Corporations dialog, following the mouse (also steps 11, 15, 18): stock too (below) |
  | 18 | vp-events | c5h-d | 12:36-12:59 | 80 | 171,230 | none | completed, no finding. No VP player or city event came in the 3 allowed turns (200 -> 203); the Notification Log (turns 200-203) holds no event; four leader scenes in the AI turns answered (gap 6) |

  Runner hours (14-18 and the stock-check runner): 0.32 + 0.47 + 0.57 + 0.67 + 0.38 + 0.53 = 2.94 h; tokens
  190,100 + 200,277 + 206,722 + 252,638 + 171,230 + 171,641 = 1,192,608.

- **Runners 19 on (impl-4609g, 2026-11-18) `[L]`.** Same set-up as 14-18 (`scripts-g/ru.sh`); spawn prompts carry
  `C:/c5-64bit-work/0398/4609/msg-g/runner-extra.txt` (before-picture rule #5452, leader scenes in the AI turn (gap 6),
  F on each waiting unit before End Turn on Large/Huge maps). Tokens are the agent's subagent total. Runner hours
  (19-24): 0.58 + 0.15 + 0.78 + 0.55 + 0.63 + 0.77 = 3.46 h; tokens 236,210 + 103,176 + 250,875 + 159,933 + 162,285 +
  220,124 = 1,132,603. With these six, every turn-ending charter of the plan has a recorded run.

  | # | Charter | Instance | EST | Steps | Tokens | Box | Result |
  |---|---|---|---|---|---|---|---|
  | 19 | great-people | c5h-d | 13:20-13:55 | 111 | 236,210 | none | completed, no finding. Great works (musician, writer) with their splash, artist's golden-age points (+200, meter 2,323/2,640), town then manufactory (merchant, engineer), citadel (general; a second one correctly refused next to it), academy (scientist), spread religion and a holy site (prophets), birth popup, eight overview screens: all worked and drawn right. One turn ended (200 -> 201), no leader scene; End Turn blocked by units cleared with F/SPACE. Not reached: trade mission and diplomatic mission (no city state in reach), admiral (none in the kit save), a real golden-age start. Usability: the left unit strip reorders after each selection and its tooltip lags one icon (steps 18, 21, 36, 45); no Culture Overview button found. |
  | 20 | combat | c5h-f | 13:20-13:29 | 24 | 103,176 | none | completed, no finding; kit gap (no enemy in reach, table above). One turn ended (9 -> 10, switch 2.6 s), diplomacy screen and overview open and close cleanly. Usability note: a held arrow key under about 600 ms gives no pan repeat, 800 ms 9 repeats. |
  | 22 | lifecycle-menus | c5h-d | 14:02-14:35 | 62 | 159,933 | none | completed, no product finding, no save made. Turn 350 -> 351 (about 30 steps of blockers, two leader scenes answered), the victory screen and its tabs, "one more turn", exit to the main menu (8.8 s), a new Duel game from the set-up screen, load of a kit save from the game's own Load list, RETIRE (defeat screen at once, no confirmation; exit to the main menu 17.1 s). Usability-step62 (harness): the `judge:in_game` wait does not end while the load screen waits for Begin/Continue, so new-game and load times cannot be read (seen twice). Note: the in-game menu of a new game starts with RESTART GAME, so its rows sit one lower than the brief's table. Not reached: defeat by a near-defeat save (none in the kit). |
  | 23 | bad-moments | c5h-f | 14:34-15:12 | 60 | 162,285 | none | completed, no finding recorded, no crash, box or hang. Input during the AI turn, held keys, ESC x10, ENTER held, six rapid End Turn clicks, clicks during a load and a save, stacked popups (F2-F7, ESC unwinds them in order), a leader scene and the war-declaration and delete-unit popups (modal, answers honoured): all without state damage. Three turns ended plus one after a reload. Save made: HI-PH-BAD-1 (turn 200, c5h-f `ModdedSaves/single/`, SHA-256 99c0826f...86e2). Observations, each seen once and not recorded as findings: F2 over the Education TechAward popup opened the Economic Overview and the popup could not be found again (research left unchosen until picked, step 20); a minimap click at the map's far edge filled the screen with the edge fog (white, steps 53-55); the save name box took focus only on the second click (steps 43-45); the unit panel's buttons move between units, so a click aimed for another unit hit Delete (step 35). Not reached: attacks (no enemy in reach), a real load-screen wait (the load took under 1.5 s). |
  | 24 | awkward-input-and-stale-selections | c5h-d | 14:42-15:28 | 78 | 220,124 | none | completed, no product finding. Three turns (8 -> 11). Save made: HI-PH-AWK-ÀÉÎÕÜ-0123456789-abcde (37 characters typed, 32 kept; c5h-d `ModdedSaves/single/`, SHA-256 02db307e...c29c). Usability-step11: the empty production slot's tooltip reads "Progress: 0/2,147,483,647" (INT_MAX). Observations, not recorded: city and unit name boxes stop at 15 characters with no hint; Latin-1 accents type and draw right (the judge's `city_name` returns them as mojibake: harness read side); a name of only spaces is refused with no message; the save name box lost the first click's input (as runner 23); RIGHT held in the tech tree pans the map behind it (as 4611's tech-tree run); minimap corner clicks barely move the camera (clamp); deleting the selected unit and then pressing its hotkeys acts on the next unit, no stale action; a pale vertical light column at the top of the map near the city in several pictures (steps 30, 31, 61, 63), gone in a burst at step 76, not recorded. Not reached: the weak-city capture (no enemy army in the save), a unit killed by the enemy, the wrap seam. |
  | 21 | turn-flow-and-overviews | c5h-f | 13:38-14:25 | 116 | 250,875 | none | completed. Four turns ended (200 -> 204); eight leader scenes in the AI turns answered by real clicks (rule 2), no hang; blockers PROMOTE, UNITS (about 25 units F'd per turn), CHOOSE PRODUCTION and CHOOSE RESEARCH cleared; 11 overviews, notifications, the EUI top-bar panels and the Civilopedia opened and closed right. Candidate **wrong_state-step57**: the Economic Overview's "Total Expenses" reads 1.22 while its own lines add to 122 and the gold tooltip says 122.00 (seen once; pipeline below). Usability-step116: a Civilopedia text link ("Chivalry") does nothing when clicked, the icon link works. Not reached: advisor counsel, the policies screen. Observation: at step 15 the first End Turn click after F on every unit selected a Longswordsman (judge block -1) and ended the turn only after five more F presses. |

  - **impl-4611's city-screen candidates (runner 1 of 4611, session c5h-e-20261118-131846; lead #5775): both stock
    too `[L]`.** A cut of steps 1-74 (`scripts-g/mkcut.py`, `runs/city-screen/pipe-g/cut.jsonl`) replayed on c5h-a
    stock (-ia DLL b71a9f31, HI-KIT-MEDIEVAL-LARGE-T200, 1920x1080, cp,vp,eui), 2026-11-18 13:50-14:00: exit 0, 74
    steps, no box (`pipe-g/stock-1/`). (1) wrong_state-step63, a left-click on the city NAME in the top-left list
    opens no screen and recentres the camera on open ocean: the same on stock (step_0002 side by side; judge
    `screens` [] at steps 2, 4 and 62 on both; the harness's hit test reads `plot` under the label on both).
    (2) wrong_state-step81 (the CTRL+clicks are steps 73 and 74): the trace records `delivered.mods ["CTRL"]` on both
    clicks, and stock's step_0074 shows the same queue as the product's (Caravan, Scout, Worker, Trebuchet, appended
    at the end). Whether the game sees the harness's CTRL at all is open (harness gap 8 `[I]`, for 4610: a check with
    a CTRL action whose effect is visible). No product repro was made for stock-too candidates.
  - **Runner 21's wrong_state-step57 (Economic Overview "Total Expenses" 1.22 against lines adding to 122): stock
    too `[L]`.** Steps 55-57 of the trace (hover and click on the top bar's gold, `/InGame/TopPanel/GoldPerTurn`)
    replayed with `hi.py replay --from 55 --until 57` (`scripts-g/vr3r.sh`) on c5h-a stock (-ia b71a9f31) from a fresh
    load of HI-KIT-MEDIEVAL-LARGE-T200 (turn 200), 2026-11-18 14:38-14:42: exit 0, 3 steps, no box. Stock's
    step_0003 shows Total Expenses 1.24 in red while the Expenses lines read Units 56.00, Buildings 23.00, Roads and
    Railroads 20.00, Diplomacy 25.00 (sum 124): the same hundredfold display slip as the product's 1.22/122 two turns
    later. A display defect of the original UI (VP/EUI's Economic Overview), not the patch `[I]` for which file.
  - **Runner 24's pale vertical light column (lead #5888): stock too `[L]`.** Steps 1-31 of
    `runs/awkward-input-and-stale-selections` (steps 15-16, the tech-tree scroll bar, as pixel drags: gap 9) replayed
    on c5h-a stock (-ia b71a9f31) from a fresh load of HI-KIT-ANCIENT-TINY-T8, 2026-11-18 15:48-16:04, exit 0, 31
    steps, no box (`pipe-g2/stock-1/`). Stock's step_0030 shows the same pale light column at the top of the map
    above Honolulu as the product's step_0030. Visual Checks vc-20261118-160449-workstation-11a1 (info). An
    observation for the renderer teams: the column is the original game's (probably the light shaft of a natural
    wonder or the fog edge's sky, not identified) `[I]`.
  - **Runner 23's edge fog (lead #5874): stock too `[L]`.** Steps 53-54 of `runs/bad-moments` (a minimap click at
    (1160,640) near the map's far edge, then wheel -15) replayed with `--from 53 --until 54` on c5h-a stock (-ia
    b71a9f31) from a fresh load of HI-KIT-MEDIEVAL-LARGE-T200, 2026-11-18 15:19-15:23, exit 0, no box
    (`pipe-g/stock-1/`): the world view is the full-white edge fog, as in the product's runner picture step_0054.
    Visual Checks vc-20261118-152505-workstation-3326 (info). Runner 23's TechAward/F2 observation stays an
    observation (lead #5874, EXE UI).
  - **impl-4611's tech-tree candidates (runner 3 of 4611, `runs/tech-tree`; lead #5800): both stock too `[L]`.** A
    cut of steps 1-70 (`pipe-g/cut.jsonl`) on c5h-a stock (-ia b71a9f31, HI-KIT-MEDIEVAL-LARGE-T200) stopped at step
    19, exit 6: "control /InGame/TechTree/TechTreeScrollBar not found ... after 6 tries" (the trace's hit for that
    control has no rect; harness gap 9 `[C]`, for 4610: a replay of a control hit without a rect should fall back to
    the recorded pixel). A copy with steps 19 and 21 as pixel drags at the recorded client_px
    (`scripts-g/fixcut.py`, `pipe-g2/cut.jsonl`, client_frac from 1914x1051) replayed whole, 2026-11-18 14:30-14:36,
    exit 0, 70 steps, no box (`pipe-g2/stock-1/`). (1) input_ignored-step17, a drag on the tree background does not
    pan: the same on stock (mean pixel difference before/after the drags at steps 16-17: stock 0.47 and 0.92 of 255,
    product 1.05 and 1.23; a real pan at step 19 is 33). (2) wrong_state-step69 and step 31, SHIFT+click on a tech
    changes the current research instead of appending: the judge's `research` is the same on both builds at every
    step read (26 -> 25 at step 31, 28 at steps 63 and 69), with `delivered.mods ["SHIFT"]` on steps 26, 31, 43 and 68
    in both traces. Whether the game sees the SHIFT is the same open point as gap 8. Usability notes of that run,
    listed only (lead #5800): step 31 (Education dropped from the queue on SHIFT+click), 38 (RIGHT held in the tree
    pans the world camera behind it), 42 (Future Tech's tooltip has an empty "Unique Actions:" bullet), 43 (the
    queue's turn counts overlap the tech names when 55 techs are queued), 76 (the legend's "Researched" label gold on
    gold), 82 (the science tooltip's "1 Turn -38.23 / 2 TURNS 40.10" line).

  - Air finding 1 pipeline: `campaign.py harden runs/air-units --finding 1 --charter air-units --expect
    '{"turn": 350}' --out runs/air-units/repro-f1` (cut steps 1-38, checkpoint owner-f1-step38). Product 1 on c5h-d
    (`vr3.sh`, fresh game from HI-KIT-ATOMIC-HUGE-T350, 11:44-11:48): exit 0, 38 steps, checkpoint ok. Product 2 on
    c5h-f (11:53-11:57): the same. The checkpoint checks only the turn, so the symptom was read from the traces
    (`scripts-f/mode.py TRACE 27 38`): in the original and in both replays the judge's mode stays
    INTERFACEMODE_AIRSTRIKE with UNIT_WWI_BOMBER selected after each of the four strike clicks (steps 29, 31, 33,
    36): reproduced 2 of 2 on the product. **Stock replay on c5h-a (stock, -ia b71a9f31, 12:18-12:22): exit 0, 38
    steps, the same modes after each strike click (`repro-f1/stock-1/trace.jsonl`): stock too, not a finding.** The
    cause (targets out of the strike's reach, or a click that a strike needs differently) is the original game's;
    runner 14's step-58 Triplane observation is the same kind and was not replayed separately.
  - Runner 15's three findings: **all stock too `[L]`.** A cut of steps 1-38 (`scripts-f/mkcut.py`) replayed on c5h-a
    stock (12:27-12:30, exit 0) gave the same judge reads at every step as the product run: step 31 ESC leaves
    INTERFACEMODE_NUKE and opens `/InGame/GameMenu` (popup true); step 37 the strike click returns to
    INTERFACEMODE_SELECTION with no confirmation; the judge's selected unit is UNIT_ANTI_TANK_GUN at steps 14 and 37
    on both. The judge/panel disagreement is therefore the original game's selection state (or the judge's read of it
    on both builds), not the product: a note for 4610 (harness gap 7 `[I]`: judge's `unit_type` versus the unit
    panel on the HUGE T350 saves).
  - Runner 16 (vp-vassals): a cut of steps 1-3 replayed on c5h-a stock (12:31-12:35, exit 0). Rome's diplomacy
    screen on stock shows the same overlap: the buttons are drawn over the leader list and their labels are cut
    ("DECLARE WA", "Request Hel", "Discus"). Compared side by side with the product's step_0003. **Stock too `[L]`.**
    The vassal-tax usability note (step 77) was not checked on stock (it needs a turn end). It is a missing display
    line, not a product change `[I]`.
  - Runner 17's usability-step102 (ghosted combat preview behind the Corporations dialog): **stock too `[L]`.** A cut
    of steps 1-18 replayed on c5h-a stock (12:47-12:50, exit 0). Its step_0018 shows the same half-transparent RANGED
    ATTACK preview (Approx DMG, YOUR STRENGTH 91.2, modifiers) and IRONCLAD panel bottom left behind the dialog as the
    product's step_0018 (side by side, `F:/c5-64bit-work/0398/4609/hang350/corp-18.png`, diagnostic).
  - **Harness gap 5 `[C]`:** a replay step whose recorded wait is a `state.*` condition is refused, not waited on,
    when the channel is silent (CONTRACT 6.4), so no cut that crosses a long AI turn can be replayed. The runner's own
    step 73 was a wait. For 4610: a replay wait on `state.turn>=N` should wait through a silent channel up to its
    timeout.
  - **Likely cause, from the runner's own pictures `[I]`, to be checked:** at step 8 the dig was confirmed on the
    archaeologist that moved to 102,26. At step 23 the runner selected that unit again (1.75/4 moves). At step 24 it
    clicked the dig button, whose tooltip read "Another unit is already building something here" (step_0024.png).
    Step 25 pressed SPACE, which is skip turn for the selected unit, before End Turn. A new mission replaces the
    unit's build, so the runner's own SPACE (or the step-24 click) would cancel the dig. The check: the same cut with
    `--skip` 23-25, or a short owner trace (steps 1-8, then End Turn through the blocking units), on product and stock.

- **Charters 27 unit-actions and 29 production-and-purchase (impl-4609j, 2026-11-18 21:00-) `[L]`.** The two
  explorer charters the first full phase had not run. Both on the kit situation `mid-game`
  (HI-KIT-MEDIEVAL-LARGE-T200, turn 200, Large map), no turn ended. Instance c5h-b (C:), installer-installed i2-rc1
  (VP64-Patch-i2-rc1-for-VP5.4.8.exe 6ed1dad6..., mods form, compositor on as the installer leaves it), laid 20:59-21:00
  by `scripts-j/lay_b.sh`: shell_config i2, vp64helper mode on, ServerRenderer 1, proxy 8d809d01, server 54e873a2,
  manifest 3fa8269f, vpproxy.ini f80d7590 (the same set as 4609i's lay of c5h-b at 18:45). Harness settings set by
  `prep_inst.py set` and `setkey.py`, backed up by SHA-256 in `F:/c5-64bit-work/0398/4609/backup-j/c5h-b/`: 1920x1080
  (was 2560x1440), SinglePlayerAutoEndTurnEnabled 0 (was 1), AutoUnitCycle 1 (was 0). Client 1920x1080, hi width
  1280, mods cp,vp,eui, deny.py with the 5 s busy-read bound (6cbe354b4, in 8e5ae75f0, the runners' checkout) on
  both runs. Runner 25 ran on c5h-b; runner 26 on c5h-e (F:), laid the same way at 21:22 after 0569's F: read window
  had ended (lead #6418, #6465) and F:'s read latency was under 50 ms (diskwatch `rlat_ms` of the F: disk, mean 0.3
  ms, max 6.2 ms over 21:16:50-21:18:45, F: idle 100 %; `provision.py align548` and `verify` rc 0 first). Each: fresh launch with the save
  loaded (`scripts-j/ru.sh`), kit proofs at load and start, `campaign.py prompt` plus the owner rules of 4611's
  `rules.md` (before-picture #5452, leader scenes, judge/panel note), Sonnet runner started by the stub pattern
  (`isolation: worktree`, `git checkout --detach 8e5ae75f0`), recorded with `phase.py record --build i2-rc1`. Run
  output written to `C:/c5-64bit-work/0398/4609j/runs/<charter>` during the F: window (lead #6418), then moved to
  `F:/c5-64bit-work/0398/phase/i2-rc1/runs/<charter>` with a SHA-256 list.

  | # | Charter | Instance | EST | Steps | Tokens | Box | Result |
  |---|---|---|---|---|---|---|---|
  | 25 | unit-actions | c5h-b (C:) | 21:06-21:59 (session c5h-b-20261118-210745) | 46 | 168,776 agent total, of which the stub turn 41,430 | none | completed, no finding recorded; kit proofs at load and start ok (42ed85cf, same). Done: Fortify and Alert by click on the Pathfinder (the hit was labelled `UnitPortraitButton`, the order still took), Alert on melee, F/A/H/SPACE/PERIOD hotkeys, Fortify Until Healed on a wounded bowman, a Galleass slept (zzz in the list and panel) then moved by Move Mode, Explore automation on a Knight, a worker's Lumber Mill started then Cancel Last Mission (build mark gone), Build Improvements (Automated) on a second worker, a promotion (Cover I) picked from the UNIT PROMOTED panel. Not reached: embark, pillage (none in reach), upgrade (disabled: 90 gold needed, not in friendly land; gold 38), delete (disabled: enemies nearby, tooltip), stopping an automation, the other worker builds. Usability notes (several times, not recorded): the mouse tooltip of a hovered left unit-list entry stays over the next picture and names a stale unit (steps 22, 31, 33, 35, 37); an order or build advances the selection at once, so cancel needs a re-select from the list (stock behaviour `[I]`). Harness note: the unit panel's bottom-row buttons hit as `UnitPortraitButton` or `pixel`, never by their own names; about 18 of 50 min went to finding workers in the left list |
  | 26 | production-and-purchase | c5h-e (F:) | 21:33-22:25 (session c5h-e-20261118-213533) | 29 | 127,486 agent total, of which the stub turn 42,051 | none | completed; kit proofs at load and start ok (42ed85cf, same). Changes: Amsterdam bought a Missionary with faith (1,044 -> 844, cost 200 as the tooltip said; step 26) and appended an Armory to its queue by a left click (step 28; queue Caravan, Armory). One finding, **wrong_state-step10**: three clicks on Amsterdam's NAME in the top-left city list (steps 2, 4, 9, hovered first) opened no city screen and moved the camera to foreign land; the city banner opened the city (steps 22-23). Closed by impl-4609j as observation, stock too `[I]`: the same click as 4611's city-screen wrong_state-step63 on the same save (first click on both at plot 74,10, hit kind `plot`; city-screen at client 75,112, runner 26 at 112,93 and 120,115), which impl-4609g's stock cut 1-74 on c5h-a showed on stock (above); accepted by lead-0398 (#6557 answer), no new replays. Observations, not recorded: Economic Overview "Total Expenses 1.24" against 124.00 in the tooltip (the stock-too slip of runner 21 above); the Overview's city names do not open the city; a minimap click jumped to Warsaw (not checked); gold 38 so no gold purchase or VP investment was possible (kit gap below). No slow step out of line on F:. A 17-min gap with no step (21:43:58-22:00) was the runner reading and thinking, not a hung command (its report; longest command 8.1 s) |

  Runner hours (agent spans, brief to report): 0.88 (21:06-21:59) + 0.87 (21:33-22:25) = 1.75 h; tokens 168,776 +
  127,486 = 296,262 agent totals, of which 83,481 for the two stub turns. Findings: 1 recorded (wrong_state, closed
  observation, stock too `[I]`); none product-only; no box, no crash, no hang. Instance-hours: c5h-b 20:56-22:06 (1.17
  h), c5h-e 21:20-22:38 (1.30 h). Both instances restored at release: the installed state read again and unchanged
  (`read_state.sh` diff empty), the installer's own removal exit 0, the four ini files at their pre-lay hashes, DLL
  back to 3c2f47da by `provision.py set-dll`, EUI's options db by hash (c5h-b a175df89, c5h-e 1b36fbd3; 4611's 15.17
  note), the tree against the pre-lay list differing only in cache/Logs run outputs and the DLL (the pre-lay list was
  taken after set-dll to the player's 262aee88), `provision.py verify` rc 0. With these two, 33 of the index's 34
  charters have a completed i2-rc1 record in the run book (read 22:45); the one without is `lifecycle-loads`, the
  model-free charter that the lead's verdict question (a) covers `[C]`.

  **Kit and harness gaps from these two runs `[L]`.**
  - HI-KIT-MEDIEVAL-LARGE-T200 has 38 gold (-9 to -13 per turn) against 1,044 faith; every gold purchase costs 212 or
    more, so production-and-purchase's gold buy and VP's gold investment are unreachable on it. The charter needs a
    mid-game save with a few hundred gold (or the recipe to add gold). Its unit side also has no upgrade (not in own
    land, 90 gold needed) and no delete (enemies near the workers), so unit-actions' upgrade and delete parts were
    unreachable.
  - The unit panel's bottom-row order buttons hit as `UnitPortraitButton` or `pixel`, never by their own control
    names (runner 25), so a replay of such a click relies on the pixel; the order still takes effect.
  - Both runners lost about 18-25 min finding their own units or cities: the left unit list and the city-list names
    give no direct way to a unit class or an own city on a Large map (runner 25: workers; runner 26: the city list
    acts as a map click, the banner works). A charter hint ("open a city by its banner; select units from the unit
    list") would save that time.
  - A runner may go 15 min without a step while it reads (runner 26, 21:43-22:00); a trace with no new step is not by
    itself a hung command.

### 15.17 The first full phase on i2-rc1: the screen charters (ticket 4611) `[L]`

**Screen-charter runners on i2-rc1 `[L]`** (2026-11-18, workstation). Sonnet runners on the product route: c5h-e and
c5h-g are installer-installed i2-rc1 (VP64-Patch-i2-rc1-for-VP5.4.8.exe, mods form, compositor on as the installer
leaves it: shell_config i2, ServerRenderer 1, helper on; laid by 4608's lay.py), a 1920x1080 client, hi width 1280,
AutoUnitCycle 1, mods cp,vp,eui. Each runner got a fresh launch with the kit save loaded, plus the kit proofs at load
and at start, and a `campaign.py prompt` with the #5452 before-picture, leader-scene and F-then-end-turn rules. Each
was recorded with `phase.py record --build i2-rc1`. Started by the stub pattern (`Agent` with `isolation: worktree`),
then the brief by SendMessage.

| # | Charter | Instance | Save | EST | Steps | Tokens | Box | Result |
|---|---|---|---|---|---|---|---|---|
| 1 | city-screen | c5h-e | MEDIEVAL-LARGE-T200 | 13:18-13:45 | 86 | 229,563 (+40,872 stub) | none | completed. wrong_state-step63 (a city NAME in the top-left city list acts as a plain map click, camera to the ocean under it; the icon opens the city screen; VC vc-20261118-133733-workstation-7207); wrong_state-step81 (CTRL+click in the production list appends at the end, the tooltip says front; may be a harness modifier gap; VC vc-20261118-134313-workstation-82b8). Production choice kept the city screen open (the charter says stock closes it): noted, not filed. Buying not reached (gold 38) |
| 2 | trade-deal-offer | c5h-g | ALL-MET-429ABBA4 | 13:18-14:00 | 58 | 167,230 (+40,871 stub) | none | completed. usability-step28 (OTHER PLAYERS: "We haven't met any third party players" on the all-met kit; may be correct; VC vc-20261118-133940-workstation-56f5). Gold/GPT amounts clamp right, "not included" tooltips right on both sides; no deal concluded. Kit gap below |
| 3 | tech-tree | c5h-e | MEDIEVAL-LARGE-T200 | 13:52-14:14 | 82 | 210,449 (+40,899 stub) | none | completed; research left on Education. 8 findings (VC ids `vc-20261118-` + ...): input_ignored-step17 (drag does not pan the tree; 135647-workstation-2330), usability-step31 (SHIFT+click drops the current research; 140031-workstation-e4f2), usability-step38 (holding RIGHT pans the world behind the tree; 140226-workstation-59c2), usability-step42 (Future Tech tooltip empty "Unique Actions" bullet; 140400-workstation-368c), usability-step43 (cumulative-turn text overprints names on a 55-tech path; 140445-workstation-b9c5), wrong_state-step69 (SHIFT+click Chemistry made Machinery current; 141049-workstation-f937), usability-step76 (legend gold on gold; 141257-workstation-3d7c), usability-step82 (unlabelled negative in the science tooltip; 141424-workstation-10b5) |
| 4 | leader-discussion | c5h-g | ALL-MET-429ABBA4 | 14:08-14:47 | 65 | 158,666 (+40,860 stub, + the refusal turn) | none | completed. usability-step65: after a click on a right-edge leader portrait, the scene shows the NEXT leader's tooltip (the in-scene strip sits about 40 px higher; steps 3, 31, 43; VC vc-20261118-144726-workstation-654d). Discuss, Trade, Demand, Goodbye, ESC (one level back) and ENTER all work. No hostile leader on the kit |
| 5 | diplomacy | c5h-e | MEDIEVAL-LARGE-T200 | 14:22-15:07 | 76 | 188,144 (+40,896 stub) | none | completed. 3 usability findings, all filed at step 76 (true pictures named): leader-scene buttons partly under the right-edge leader list (steps 21/25/50; vc-20261118-150713-workstation-6ddb); a stale "says: No." after the table was cleared (step 47; -150720-workstation-2361); Kyzyl's "Protected By" line runs past the frame (step 60; -150729-workstation-2d9d). Not reached: Demographics/Victory/Economic overviews, notifications |
| 6 | options | c5h-g | ANCIENT-TINY-T8 | 14:55-15:25 | 61 | 152,594 (+40,914 stub) | none | completed, no finding. Game, Interface and Audio options, the minimap map options and the strategic view all work and were set back (Accept, Cancel, ESC; values kept on reopen). VIDEO and Multiplayer never opened |
| 7 | camera-selection | c5h-e | MEDIEVAL-LARGE-T200 | 15:16-16:05 | 50 | 181,028 (+40,953 stub) | none | completed. usability-step7: horizontal brightness seams in the cloud layer and a hard cloud-to-black line at the south map edge, stable over 3 frames, may be stock (VC vc-20261118-152017-workstation-6624). usability-step28: a hover over the unit panel's gear took over 120 s, and the shot fell back to printwindow ("composite dump timeout: no complete back-buffer dump within 5.0 s"). Other slow steps of 14-31 s came after camera jumps: unit auto-select, minimap, city screen (VC vc-20261118-153749-workstation-6719). Pan, zoom, strategic view, selection, moves and banners all work, and the judge always matched the panel |

Totals: 7 runners (save-load and trade-deal-answer had been completed by 4609 before this ticket), 478 steps,
1,287,674 runner tokens (+286,265 for the 7 stub starts, +43,357 for r4's refusal turn), 4.24 runner-hours
(session spans), no box, no crash, 18 findings handed to 4609's pipeline (1 input_ignored, 3 wrong_state, 14
usability), none reproduced or stock-checked here.

**Kit and harness gaps.**
- Runner worktrees made by `isolation: worktree` branch from `origin/main`, which does not hold the phase's charters
  (team-0398's 2774d514e is not merged; main's `tools/hi/charters/` had 7 files on 2026-11-18). Each runner checks out
  `--detach 2774d514e` first. `[C]`
- `HI-KIT-ALL-MET-429ABBA4` is at turn 9 (ancient, no luxuries, no techs for agreements, no tradeable cities,
  embassies not possible): trade-deal-offer could add only gold and GPT, so its resource, agreement, city and
  third-party parts and a concluded deal were unreachable. The trade charters need an all-met save in a later era
  (Writing/Philosophy known, a luxury to spare). `[L]`
- The MEDIEVAL-LARGE-T200 save is not the "Large 21-civ save" the tech-tree charter text names (the runner noted it);
  the charter ran fully on it. `[L]`
- One Sonnet runner (r4) refused the brief sent by SendMessage: it read it as coming from "a sibling agent", not from
  its parent, because the stub named the parent only as impl-4611. It accepted after a second message naming the
  parent's agent id. Since 14:16 the stub prompt names the parent's agent id. `[L]`
- Harness gap 6 cost: the judge is silent (exit 4, an 8 s timeout per read) on every leader and trade screen. That
  is about 4-6 min per runner on the diplomacy and leader charters (r4, r5), enough to push r5's last overviews out
  of its hour. `[L]`
- A finding filed late (r5 filed all three at its last step) carries that step's picture in the Visual Checks feed,
  not the picture it is about. The runner named the true steps in the text. The pipeline should cut from the named
  step. `[L]`
- **4608's `lay.py restore` reports MISMATCH on every product-route instance `[C]`** (c5h-g, 2026-11-18 15:31). It
  passes `--apart PREFIX` once per prefix to `treehash.py diff`, whose `--apart` is `nargs='*'` with the default
  store action, so only the last prefix (`run`) stays apart: 495 Logs/cache run outputs counted as differences. With
  one `--apart Logs cache ModdedSaves Saves Screenshots "MODS/(1) Community Patch/VP64/install" HI hi run` the only
  real difference was `ModUserData/Enhanced User Interface Options-1.db`. That is EUI's options db, written by play,
  and it is not in 4608's backup set (1b36fbd3 at the lay, efe9fdd6 after the runs). It was put back to 1b36fbd3
  from a byte-identical copy on c5h-mpk (read only). The replaced copy is in
  `F:/c5-64bit-work/0398/4611/backup/c5h-g/`. Every installer lay should back up `ModUserData/` by hash as well.
  My copy `C:/c5-64bit-work/0398/4611/lay.py` passes one `--apart` list. Other notes: the c5h-g UserSettings.ini
  after r6's Accepts had SmallUIAssets 1 (pre-lay 0) `[I]`, put back by hash; on the stock routes (`installing`
  false) the diff does not run, which is why c5h-h and c5h-i passed.
- The judge's screen list does not know the Corporations & Monopolies popup (`screens` empty, `popup` true; r5 steps
  69-71). `[L]`
- Slow harness steps on the installed i2 product route `[L]` (r7, c5h-e, 15:16-16:05): normal steps took 2-5 s,
  but steps after a camera jump took 14-31 s and one hover took over 120 s. The shot fell back to printwindow
  ("composite dump timeout: no complete back-buffer dump within 5.0 s"), which suggests the compositor's back-buffer
  dump stalls while the world renderer re-bakes after a long camera move `[I]`. This cost r7 about 8 min of its hour.
  It is handed over as finding usability-step28 so that a stock replay can settle it.
- The hi.py `hit` of a unit-panel button can name the wrong control: Fortify reported `UnitPortraitButton` (r7
  step 29). `step "hold RIGHT 2000"` as one quoted string is refused; it must be `step hold RIGHT 2000`. `[L]`
- The options screen's judge name is `OptionsMenu_InGame`, not `OptionsMenu` (r6 lost a wait on it). The charter has
  no Keys tab; the in-game options tabs are Game, Interface, Video, Audio, Multiplayer. `[L]`

