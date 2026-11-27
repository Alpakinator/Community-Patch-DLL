# Increment 2 acceptance: the full-game soak on the D33 product path (ticket 0325)

Increment 2's Definition of done (0003) ends with "one full game soak". This file holds that soak: a new game played by
the human-input (HI) lane from turn 0 to turn 300 (or the game's own end) on the product path, beside a stock arm of
the same game. Marks: `[C]` read in code or records, `[L]` verified live, `[I]` inferred. Times EST.

## 1. Set-up (2026-11-16, impl-B-0325, account B)

**The product arm** (c5h-f, F:, Goldberg; claimed long, assert policy record-and-continue, address dispatcher-B):
- Binaries built from branch `task-0325-increment2-soak` (main cffd773e) at the DLL pin 04b1211b8 by
  `shell/evidence/0325/build_0325.sh` (0030's recipe: `server/build_server.py` release against 9045's pin source and
  release deps, `shell\build.cmd`): server.exe cce22d24..., x64 DLL 74588135... (the pin's release deps, as every
  product-path ticket since 0368; the pinned layout `x64-04b1211b-pfdee656` carries 29F58E17..., a later link of the same
  commit), proxy f41bcd94..., vp64helper.exe d4fe287e..., shell_composite.dll 8e3e27bc....
- The release folder staged as a player gets it by `run_0325.py stage` (0030's stage: `tools/release/pack.py`, the purity
  file, the manifest bound to the tag's own DLL 262AEE88..., vpproxy.ini `mode=on`, `pack=1`, the composite with its
  ini and `shell_composite.sha256`), installed by `run_0325.py prep` (every changed or created file backed up by
  SHA-256 first; the tag DLL renamed to `CvGameCore_Expansion2_x86.dll`, the release files copied in), the game started
  once to the main menu (VP64Options.ini written, 9497f85b..., defaults), then **only `vp64helper on`** (0030's one
  switch): config.ini D33 A'' keys written (Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons), `[Renderer]
  ServerRenderer = 1` [L].
- `GraphicsSettingsDX11.ini` WindowRes 2560x1440 -> 1920x1080 (D158; client 1914x1051) and `UserSettings.ini`
  `SinglePlayerAutoEndTurnEnabled` 1 -> 0 (the driver ends every turn itself, as 4506's journeys), both backed up by
  SHA-256 (f1d9cf01..., 85f9d806...).

**The stock arm** (c5h-d, F:, Goldberg; same claim terms): **stock EXE + i0-ship 32-bit DLL**, the instance's own
`i0ship-26234371-ia` (3C2F47DA...), no proxy, no vp64helper; the same two ini changes. Not the tag's DLL: the tag DLL
has no Lua channel, so the judge (the HI driver's only reader) would be blind; the i0-ship pin has the same game rules
as the product's x64 DLL (MC #3007, accepting question #2788; 0438 measured i0-ship = x64 at the turn played).

**The game:** Communitu_79a, Standard size (7 AI majors, the size's default city-states), Standard speed, ancient era,
handicap Settler (MC ruling on #2785, so that the passive player lives to turn 300), the rest the front end's defaults;
mods CP, VP, EUI (the harness default, MC #2508). The new game is started on the product arm by the harness's front-end
hook (`frontend.py` `newGameSetup`, test infrastructure, not part of the human's play); its turn-0 save, made by the
HI driver through the save menu before any move, is the stock arm's start (the front end has no seed key, as 0322).

## 2. The HI soak player (`shell/evidence/0325/soak_0325.py`)

Posted mouse and keyboard only (`tools/hi/actions.py`); the game read only through the HI judge's read-only chunks
(`tools/hi/judge.py`: `state`, `snapshot`, `under`, `control`, `plot-pixel`). The play is passive by the MC's ruling
(#2785): the cities build what the game's own production automation picks, research follows Future Tech's path, a
military unit stays fortified in the capital. An end-of-turn blocker the driver cannot answer stops the arm (a finding,
not a reason for Lua).

### 2.1 What the human's paths look like at 1914x1051 [L, 2026-11-16, dev game on c5h-d, stock]

| Blocker or screen | The human's answer (posted) | Evidence |
|---|---|---|
| start: settler | `B` founds the city; the city screen opens by itself | state: city 1042 Paris, `/InGame/CityView` |
| PRODUCTION | the end-turn button (1733,830, `/InGame/WorldView/ActionInfoPanel/EndTurnButton`) opens the city screen; EUI's named **`/InGame/CityView/AutomateProduction`** (400,1020) hands the city to the game's production automation for good: the governor put a Monument in the queue at once | picture; no production blocker for that city afterwards |
| RESEARCH | the end-turn button opens EUI's tech tree; 12 clicks on the scroll track's right end (1880,885) page it to the end; a click on **Future Tech** (1640,518) queues the whole path; with EUI the choice closes the tree | state `research` -1 -> 1 (Pottery) |
| UNITS | the auto unit cycle selects the unit; `F` fortifies (warriors), `E` explores (pathfinder), `A` automates (workers), `B` founds (settlers); the first warrior was moved into the capital by a right click on the capital's plot pixel (`plot-pixel` read) and fortified | state: next unit selected after each key |
| UNIT_PROMOTION | EUI's "UNIT PROMOTED!" panel beside the unit panel; its first choice (412,1025) is instanced (no path) | blocker cleared |
| POLICY | the end-turn button opens EUI's policy screen; a branch's Adopt (`BranchButtonN`) or a policy icon opens `PolicyConfirm`; **its Yes is `/InGame/SocialPolicyPopup/Yes`** (956,514). **The end-turn blocker is re-read only after the screen closes**: `GetEndTurnBlockingType` still said POLICY with the policy adopted until ESC closed the screen | state before and after ESC |
| FOUND_PANTHEON | the end-turn button opens **`/InGame/ChoosePantheon`** (not `ChoosePantheonPopup`, the name in the judge's SCREENS list, so `state.screens` stays empty while `UI.IsPopupUp()` is true); the first belief (200,175), then `/InGame/ChoosePantheon/Yes` (956,514) | `control` read of the context: hidden false -> true |
| popups (ruins, city-state greeting, ...) | ESC | `UI.IsPopupUp()` false after |
| a leader scene (an AI's greeting) | ESC (= Goodbye) | **the Lua channel is silent while a leader scene is up** (request not picked up for minutes: the in-game Update that polls it does not run); it answered again at once after ESC |
| a leader scene that asks (a Declaration of Friendship) | ESC does nothing; the first response button (956,985) is the refusal ("Sorry, we prefer to go it alone."), then "Back" or ESC | the picture; the driver answers silence by ESC and the first response in turn. The scene's top band does not tell it from the map (a night scene, Venice, has the top panel's dark band, 12.8) |
| LEAGUE_CALL_FOR_PROPOSALS | the end-turn button opens `/InGame/LeagueOverviewPopup` (a context the judge's SCREENS does not name); "Click to Propose" (632,740, an instanced row), a resolution without a sub-choice (World Science Initiative, 875,708), `CommitButton` (871,806; **disabled until a resolution is chosen**), `ConfirmYes` (956,530) | stock arm turn 208: blocker cleared |
| LEAGUE_CALL_FOR_VOTES | the same popup in session: `CommitButton` (enabled with delegates left), "Some of your Delegates are not assigned. Commit anyway?" `ConfirmYes`: the human abstains | product arm turn 230: blocker cleared |
| the save | ESC opens the game menu; **its rows move with the game's state** (RESTART GAME only at turn 0, RETIRE later: SAVE GAME at y 402 at turn 0 and y 357 at turn 10), so SAVE GAME is found by posted moves down the centre line and `HasMouseOver`; the name box has the focus when the menu opens (W1 5.2): END, 40 BACKSPACE, the name as `WM_CHAR`s, read back by `control`; `/InGame/GameMenu/SaveMenu/SaveButton` (1268,813) | the file written in `ModdedSaves/single` |
| picking | `plot-pixel` (the EXE's camera projected, confirmed by a posted move and a `hex` read) on plots around the plot under the screen's centre | 3 of 3 at turns 10 and 20 (dev) |

Hazard noted: an AI leader scene that opens between the driver's read and its click puts **Declare War** close to the
end-turn button's point (the leader scene's button stack is at x 1672-1874, y 802-984 at this size). The driver
re-checks the end-turn button with `under` (a posted move and a read) right before every end-turn click; while a
leader scene is up that read cannot answer and the click is not sent [C].

## 3. Results (2026-11-16, both arms to turn 300) [L]

Workstation, the two arms side by side (MC #2798), not for timing (COMMON 23): every memory slope here is a workstation
indication. Records: `shell/evidence/0325/acceptance.txt` (the summary of `analyze_0325.py`); the run folders, saves,
memory samples and the product's logs on `F:/c5-64bit-work/0325/` (`saves.sha256` lists the 22 saves).

**Pass criteria.**

| Criterion | Product arm (c5h-f) | Stock arm (c5h-d) |
|---|---|---|
| reached | turn 300 (15:39-18:53 EST, 3 h 14 min, 37.3 s per turn mean) | turn 300 (15:53-19:05, 37.2 s per turn) |
| crash, box (D155) | none; no dialog-watcher marker at any check | none |
| server restart | none: one server run folder for the launch, `vpproxy.log` one `boot: server attached`, one server pid (57648) from turn 0 to 300 | - |
| D118 / D125 report | none (no `VP64/reports` folder) ; no LOUD line in `vpproxy.log` or `server.log` | - |
| saves every 50 turns, by the save menu | 7 of 7 (T0000-T0300, 264,776 to 1,281,843 B) | 7 of 7 |
| picking: 10 click-to-plot checks every 50 turns | **70 of 70** confirmed (camera pixel, posted move, `hex` read names the plot) | 70 of 70 |
| gray frames (server's 1 s lines) | **0** in all 11,668 `terrain 1s` lines; terrain state OK in 11,611; the 57 others (HIDDEN 35, NO_CAMERA 21, NO_FIELD 1) all in the first minute, 15:37:37-15:38:33, the new game's load | - |
| frames | host draws = published = 335,365; no `lost` | - |
| lockstep (composite's 1 s lines, 11,549) | exact 172,828, older 1,161, none 16, waits 7,473 (max 2.56 ms), late 0, 1 give-up with 1 recovery, no wrong-thread, no source failure | - |
| D10: the final save loads in stock | `HI-0325-PRODUCT-T0300` (8cfc5c1c...) loaded on the stock route and played turn 300 -> 301 by the same driver, rc 0 | - |

**Memory per turn (perf/memsampler, rows at each end-of-turn autosave, 300 rows each, D78's measure).**

| Process | private MB first -> last | private slope MB/turn (95 % CI), all / second half | largest free block slope, all / second half |
|---|---|---|---|
| product EXE (32-bit) | 1338.9 -> 1538.7 | **+0.45 (+0.43..+0.48)** / +0.41 | **-0.65 (-0.68..-0.62)** / -0.54 |
| stock EXE + i0-ship 32-bit DLL | 1784.0 -> 1989.5 | +0.54 (+0.52..+0.56) / +0.45 | -0.72 (-0.76..-0.69) / -1.04 |
| server.exe (product) | 1054.4 -> 1176.0 | +0.17 (+0.17..+0.18) / +0.19 | - (64-bit) |

- The product EXE starts 445 MB lighter (the DLL lives in the server) and grows slower than the stock route on both
  measures, the intervals apart: D78's direction holds for a 300-turn human game on the D33 product path. The smallest
  largest free block was 1606 MB (product) against 1096 MB (stock); at the second-half slope the product EXE's free
  address space lasts about 2,955 turns (stock about 1,400), a straight-line projection only.
- The two games are the same game for a long time (the same gold and borders at turn 100, by eye) but had parted by
  turn 208 (the stock game at war, the product's World Congress proposal phase without a blocker), so the second
  halves compare different games.
- Heavy jobs registered on the machine at the 14 saves: 5 to 9 (other teams' games and servers), recorded in each
  arm's `soak.json`.

**Driver stops** (none a game fault): product turn 0 (a unit that stayed selected), 16 (a popup raced the end-turn
check), 52 (a promotion with another unit selected), 230 (World Congress vote); stock turn 208 (World Congress
proposal); plus stop-file restarts to load rules. Each was answered by posted input and the driver resumed on the
live game. Rule counts per arm are in `acceptance.txt` (for example product: 107 ESCs and 38 first responses to leader
scenes, 32 tech awards, 13 promotions, 10 policies, 4 abstentions in the World Congress).

**Seen in passing [L, diagnostic pictures]:** the product arm draws no trees on forest plots at turn 100 where the stock
arm does (D33's `Forest` system off; trees are epic 0431's work), every other part of the two turn-100 pictures looks
alike by eye.

## 4. Not verified

- **No picture here is ACCEPTANCE (D112):** the build is a branch build, not a release candidate, and every picture is
  the posted lane's hidden window (DIAGNOSTIC in the feed). The release candidate's picture still has to be taken.
- The memory numbers are workstation indications with other teams' jobs running (5-9 heavy jobs at the saves).
- The product's x64 DLL is 74588135 (the pin's release deps), not the pinned layout's 29F58E17 link (as 0030).
- The two stops for blockers without a rule were answered by me through posted input with the driver stopped; the
  rules were then added. A game that meets a new kind of blocker (ideology, religion founding, archaeology, a
  free-tech pick) still stops the driver: none of these came in these 300 turns.
- Leader scenes are answered blind (the channel is silent while one is up): ESC, then the first response button. The
  first response was the refusal in every scene I looked at, not in all 75 answered.
- The HI judge's `state.screens` list (`tools/hi/judge_lua.py` SCREENS) names `/InGame/ChoosePantheonPopup`,
  `/InGame/ChooseReligionPopup` and `/InGame/LeagueOverview`; this game's contexts are `/InGame/ChoosePantheon`,
  `/InGame/ChooseReligion` and `/InGame/LeagueOverviewPopup` (Expansion2's InGame.xml), so those screens read as
  "no screen, popup up". Reported for epic 0398 (which holds tools/hi); the driver reads them itself by `control`.
