# Idea 0400, stage 1: hot-seat play under human-input testing

Status: stage-1 study of ticket 0400 (D275), written 2026-11-26 21:38-22:05 EST by the 0400 implementer (account A,
dispatcher-A), branch `task-0400-hot-seat-hi-study`. Reading only: the knowledge base and this repository at main
d4ae16584 (which carries 98405aeae), the DLL source of the x64 pin a2627c8ac in the kept copy
`C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac` (the pin of `deps/dll.lock`), and, read only and not copied, the
front-end and in-game UI files of the test instance c5h-55-c (`install/Assets/UI/...` and its own `MODS` folder: CP,
VP, EUI as the 5.5 instances carry them). Nothing was built, launched or run, and no product code was changed. Marks:
`[C]` a source or document read by this study (the line names it), `[L]` a live result taken over from the file named,
or a directory listing made by this study (named), `[I]` inferred.

The question (the owner, D159, 2026-11-16, through the Architect's addition to the HI epic,
`../07-architecture-decisions/architect-2026-11-16-human-input-coverage-addition.md` 1-2): hot seat [reworded for privacy]: not now,
tracked as an idea. Stage 1 asks how the HI runner would play two human seats on one client (the hand-over screens,
whose turn it is, what the journeys record), what the harness already has, the cost, and the step tickets.

## 0. Summary

1. **Hot seat has never been run in this project, on stock or on the patch.** No instance holds a hot-seat save: the
   `Saves/hotseat` trees of all 16 `c5h-*` instances (8 on C:, 8 on F:) hold no file at all (only the empty folders
   `auto/prev`), and every instance's `ModdedSaves` holds only `single` [L, directory listing by this study, 2026-11-26
   22:0x]. The knowledge base says the same
   (`known-crash-causes.md` "Not covered"; `first-beta-known-issues-and-gaps.md` A1.4, C11; `../08-performance/hidden-embarks.md`
   "Not verified": "No hotseat set-up exists in the project").
2. **Two seats on one client fit the HI model with small additions, not a new harness.** Hot seat is one EXE, one DLL,
   one window and sequential turns; the hand-over is an ordinary in-game modal screen (`PlayerChange`) with a Continue
   button and the Enter key (section 1.3). The runner plays it as it plays any screen: a control click or a key, then a
   judge wait on the new active player. The judge already reports the active player as `state.player` (section 3).
3. **Four things block it today, all in the harness:** the deny list refuses the main menu's MULTIPLAYER button, the
   only door to hot seat (section 3.1); the front-end picture judge knows no multiplayer screen; the judge's screen list
   has no `/InGame/PlayerChange`, so "the hand-over screen is up" cannot be waited on; and `frontend.in_game` would call
   the hand-over's own pause "paused" and its dismiss step would unpause it through Lua, which skips what a player does
   (section 3.3).
4. **The value is on the product path, not in the harness.** A hot-seat hand-over is the one routine event that drives
   `CvGame::setActivePlayer` between two humans: a whole-map fog burst to the server, the net-id swap, the player
   options re-sent through the server's net queue, a modal screen that pauses the game, and the staging room's DLC and mod
   re-activation before the game starts (section 4). Several open "not verified" lines in the knowledge base wait for
   exactly this run (section 4, list).
5. **Cost: about 20-34 agent hours in four steps** (H1-H4, section 6), one game-instance place at a time on the
   workstation's posted lane; no real-input lane, no model in the loop for the journey itself.

## 1. Hot seat in the DLL and the UI (what the runner meets)

### 1.1 The game type and the turn order [C, DLL at a2627c8ac]

- Hot seat is the pre-game game type `GAME_HOTSEAT_MULTIPLAYER`: `CvPreGame::isHotSeat()` is `gameType() ==
  GAME_HOTSEAT_MULTIPLAYER` (`CvPreGame.cpp` 1108-1112). `CvGame::isGameMultiPlayer()` is network or PBEM or hot seat
  (`CvGame.cpp` 4727), so every "not multiplayer" branch of the DLL and the UI is off in hot seat too.
- An open slot counts as human in hot seat (`CvPreGame::isHuman`, `CvPreGame.cpp` 1118-1129: `SS_OPEN` is human for
  hot seat, pitboss and PBEM).
- Simultaneous and dynamic turns are forced off at game init for hot seat (`CvGame.cpp` 259-265), and
  `isSimultaneousTeamTurns()` is false outside network play (`CvGame.cpp` 6174-6189). So **every player, AI or human,
  plays its turn in slot order**: when a player's turn ends, the next alive player by slot number is made turn-active
  (`CvGame.cpp` 1787-1842).
- **The active player changes only when a human's turn starts.** `CvPlayer::setTurnActive(true)` calls
  `kGame.setActivePlayer(GetID())` for a human in hot seat or PBEM (`CvPlayer.cpp` 34498-34503), after clearing the event
  messages. While the AIs play, the active player stays the last human [C, by the same lines: nothing else sets it].
  A dead human is not passed over in hot seat, so that it gets its defeat screen (`CvGame.cpp` 1820).
- When a human ends its turn and more than one human is alive, `setHotPbemBetweenTurns(true)` (`CvGame.cpp` 1789-1792);
  the flag is cleared at the next human's activation (`CvGame.cpp` 6326) and is saved with the game (`CvGame.cpp` 10917).
- `CvGame::setActivePlayer(new, bForceHotSeat, bAutoplaySwitch)` (`CvGame.cpp` 6305-6370) for a human in hot seat:
  turn timer and game-data dirty bits, `resetTurnTimer`, `gDLL->getPassword(new)`, the two players' net ids swapped,
  `sendPlayerOptions(true)`; then, with graphics up, `PublishActivePlayer`, the trade-route visuals refreshed,
  `CvMap::updateFog` (a `GameplayFOWChanged` for every plot, `../05-graphics-composition/fog-of-war.md` 3.9),
  `updateVisibility`, `setCanEndTurn(false)`, the selection cleared and the UI dirty bits. The autoplay and observer
  switch (`observer-switch-minimap.md` 2) uses the same function with `bAutoplaySwitch`, so most of this path has run
  on the product already; the password call, the net-id swap and a human-to-human change have not.
- Other hot-seat differences a journey meets: end turn is allowed without a moved unit (`CvGame.cpp` 2257, 2274); RETIRE
  only before the player has ended its turn (`CvGame.cpp` 3546); no "who is winning" popup (`CvGame.cpp` 8702); no
  victory or leaderboard record (`CvGame.cpp` 11267); `CvPlayer::isTurnDone` holds popups and diplomacy until the
  player's next turn once it has ended its turn (`CvPlayer.cpp` 34931); in VP's active-diplomacy build every AI may open
  diplomacy with the active human during `updateTimers` when that human's turn is active (`CvGame.cpp` 9300-9312;
  the deferred-diplomacy Extra changes this, `../07-architecture-decisions/deferred-ai-deals-spec.md` 3.5).

### 1.2 The way in, through the front end [C, c5h-55-c's UI files]

- **New game:** main menu MULTIPLAYER, then the multiplayer select screen's Hot Seat button, which sets the game type to
  hot seat and queues the multiplayer game set-up screen (`MultiplayerSelect.lua` 77-87). The same file serves the
  mods form (`ModMultiplayerSelectScreen`, line 9), the form the HI journeys use ("ModdedSaves and the MODS menu",
  `tools/release/stages/hi_journey.py` 105). The set-up screen hosts and opens the staging room, where the host is
  readied at once in hot seat, the chat is hidden and a "local edit" button lets each seat be edited (CP's
  `StagingRoom.lua` override 1482-1487, 1097); Launch calls `Modding.ActivateAllowedDLC()` and then
  `Matchmaking.LaunchMultiplayerGame()` (260-271). Showing the staging room also calls `Modding.ActivateAllowedDLC()`
  and sends `SystemUpdateUI(RestoreUI, "StagingRoom")` "as the ActivateDLC may have swapped out the UI" (1472-1480).
- **Load:** the load menu lists saves by game type (`UI.SaveFileList(list, gameType, ...)`, `LoadMenu.lua` 545); its
  game type is single player only under the single-player screens and hot seat when `PreGame.IsHotSeatGame()` (693-702).
  So **a hot-seat save is not listed from SINGLE PLAYER, LOAD GAME**: it is loaded from the hot-seat path, where Start
  sets the load file, reads the header and calls `Matchmaking.HostHotSeatGame(name, slot count from the save)`
  (`LoadMenu.lua` 47-83), and the game starts from the staging room as above. Hot-seat saves are written under
  `Saves/hotseat` (`ModdedSaves/hotseat` for a modded game) [I: by the folder names in every instance; not seen with a
  file in it].
- **The load screen does not wait for a click in hot seat.** EUI's `LoadScreen.lua` (the file in effect,
  `run-and-drive-the-game.md` 3) calls its own Continue handler as soon as the load completes when the game is
  multiplayer or hot seat (lines 167-171), and neither the load-complete event nor the handler sets or clears the pause
  in hot seat (138-140, 181-185). The harness's `load_screen_ready` state (Continue shown, waiting for the click) is
  therefore never seen in hot seat [I: by the code; not run].

### 1.3 The hand-over screen [C, `Assets/UI/InGame/PlayerChange.lua` and `.xml` of c5h-55-c; no CP, VP or EUI override
found under the instance's MODS]

- The in-game context `/InGame/PlayerChange` is declared hidden in `InGame.xml` (base line 68, Expansion2 line 75) and no
  Lua file of the install or of the instance's mods shows it. The EXE shows it at a hot-seat hand-over [I: the EXE asks
  `ICvGame1::IsHotSeat`, which the proxy mirrors (`proxy/model/policy-table.tsv` line 776), and nothing else can show
  the context; not seen live].
- What it is: a full-window black box that takes the mouse, with the next player's name (nickname, else leader name)
  and civilization icon in a 400-wide panel; a password box shown only when that player has a password; buttons Continue,
  Change (or Add) Password, Save and Main Menu, and a Yes/No confirmation for Main Menu.
- When shown with a turn active it pauses the game for the active player (`Game.SetPausePlayer(active)`) and raises the
  turn-timer semaphore; when hidden it unpauses (`SetPausePlayer(-1)`). If it is shown while no player is turn-active it
  closes itself. A further active-player change while it is open re-pauses for the new player and refreshes the name.
- **Continue** closes it when the typed password matches (`PreGame.TestPassword(active, text)`; with no password set an
  empty box matches [I: by the button being enabled when no password is set]). **Enter** does the same while a turn
  is active (its input handler). Save opens the in-game save menu as a child of this screen
  (`/InGame/PlayerChange/SaveMenu`, a second save-menu path the deny list does not name, section 3.1).

## 2. How the HI runner would play two seats

### 2.1 The set-up (before `session start`, CONTRACT 1 item 4)

The journey's start is a hot-seat save with two human seats (slots 0 and 1) and two or three AIs on a Tiny map, on the
VP 5.5 mods form, made once and kept by SHA-256 beside the release journeys' saves (D83: never committed). Two ways to
make it (H1 decides by trying):
1. **By hand-in-the-harness:** posted clicks through MULTIPLAYER, Hot Seat, the set-up screen and the staging room on
   the stock route, then a save from the hand-over screen or the game menu. Needs the deny-list narrowing of 3.1 first,
   or a coordinator-approved one-off session with the list's front-end regions waived for that walk.
2. **By front-end Lua**, as `frontend.py prepare` and `tools/stressmap/ptwe.py` start single-player games (patched
   `MainMenu.lua`, `PreGame.SetGameType(...)`, slots, then the launch): the hot-seat form would set
   `GAME_HOTSEAT_MULTIPLAYER`, two `SS_TAKEN` human slots, and host and launch through `Matchmaking` instead of
   `SerialEventStartGame` [I: the Lua calls exist (LoadMenu.lua 77, StagingRoom.lua 268); whether the launch works
   without the staging room shown is not known].

Every journey start then copies the save into the instance's `ModdedSaves/hotseat` and walks in by posted clicks:
MULTIPLAYER, Hot Seat, Load Game, the save, Start, and the staging room's Launch (2.3). No Lua write happens inside the
session (CONTRACT 1).

### 2.2 Whose turn it is, and the hand-over step

- **Whose turn:** the judge's short state already carries `player` (`Game.GetActivePlayer()`) and `turn_active`
  (`tools/hi/judge_lua.py` STATE) [C]. In hot seat, `player` is the seat at the keyboard and changes only at a human's
  turn start (1.1). A runner that ends seat A's turn waits for `state.player == B` (an expression wait, CONTRACT 6.4: `state.FIELD OP VALUE` is
  readable in game) [C], not for `turn_advanced`, which fires only when the whole round ends.
- **The hand-over step** (one HI step per hand-over): wait `screen_open PlayerChange` (needs the screen in the judge's
  list, 3.2), then a `control` hit on `/InGame/PlayerChange/ContinueButton` (or `key ENTER`), then wait `screen_closed
  PlayerChange` and `turn_active == true`. With no passwords the password box is hidden, so nothing is typed. A
  password variant (type into `EnterPasswordEditBox`, a wrong password keeps Continue disabled) is a second journey
  later, not the first.
- **The judge reads while the screen is up:** the Lua channel answers while the game is paused (the harness's
  `take_begin_click` reads `Game.IsPaused()` during the load screen's pause, `tools/harness/frontend.py` 741-770) [C], so
  waits and checkpoints work on the hand-over screen [I: not tried on this screen].
- **The AIs between two humans:** with slots human 0, human 1, AIs 2-3, the round is A, B, AIs; with A, AI, B, AI the AIs
  play between the hand-overs while the hand-over screen may already be up (it pauses only "if this player is going to
  take a turn", PlayerChange.lua's show handler) [I]. The first journey uses humans in slots 0 and 1 so that each
  hand-over follows a human's end of turn directly.

### 2.3 What the journey records

The trace format (CONTRACT 8.1) needs no new record kind:
- the **header** names the start save (name, SHA-256) and the route as today; a hot-seat journey adds, in the
  `journeys.json` entry (not the trace), `"game_type": "hotseat"`, the human slots and their names, and the start
  folder `ModdedSaves/hotseat` (the release stage copies into `ModdedSaves/single` today, `hi_journey.py` 841-855);
- each **step** carries the judge's `player`, so a replay can check that every action was made by the seat the
  recording made it with; a **checkpoint** after each hand-over expects `player` and `turn_active`;
- the **hand-over screen's picture** is a step picture like any other. Its share of near-black pixels is high (the
  panel is about 400 x 240-320 of a 1914 x 1051 client, about 5-6 % of it), so `not_black` (fails above 95 % dark,
  `tools/hi/checks.py` 47-48) may fail on a correct hand-over [I]; checkpoints on this screen use the judge's fields and
  `not_flat` only until H2 measures the dark share.
- The **deny-list refusals** and the **findings** are recorded as for any session; the stock control replays the same
  trace on the stock route (CONTRACT 8.3), and the AI turns between two hand-overs may diverge as they do in single
  player (`diverged`, a parity note).

A first journey, "release-hotseat" (H3): load the two-human Tiny save from the hot-seat path, Launch, three rounds in
which each seat moves a unit, founds or works a city and ends its turn, one save from the game menu as `HI-0400-HS`
and its load through the hot-seat load menu, the Save button of the hand-over screen once, then Main Menu from the
hand-over screen and EXIT. About 40-60 steps [I].

## 3. What the harness has, and what it lacks

| Need | Today | Gap |
|---|---|---|
| Reach the hot-seat menus | posted clicks, centre-anchored front-end hits (CONTRACT 8.2) [C] | `deny.py` refuses the main menu's `MultiplayerButton` region (its reason names internet and LAN) [C, `FRONT_END_REGIONS["main_menu"]`]; 3.1 |
| Know the front-end screen | `frontend_screen.py` signatures: main_menu, single_player, setup_game, load_game, options [C, lines 46-52] | none for the multiplayer select, the game set-up, the hot-seat load menu or the staging room |
| Whose turn | judge `state.player`, `turn_active`; expression waits [C] | none; a named condition `seat_is N` would only be sugar |
| Hand-over screen open/closed | `screen_open`/`screen_closed` over `judge_lua.SCREENS` [C] | `/InGame/PlayerChange` is not in `SCREENS` [C, judge_lua.py 96-106]; the hit on its Continue is a `control` hit once the screen's buttons are in `uimap.py`'s candidate paths, else a `pixel` hit [I] |
| In game after the load | `frontend.in_game` / `probe_in_game`: in game = load screen hidden, InGame up, not paused [C, frontend.py 908-944] | the hand-over pause reads as `paused`; `dismiss_load_screen`'s unpause step would call `Game.SetPausePlayer(-1)` behind the hand-over screen, a write no player makes; 3.3 |
| Start save placement | `hi_journey.py` copies into `ModdedSaves/single` [C] | the hot-seat folder |
| Picture checks | `not_flat`, `not_black`, `not_frozen`, `no_flicker` [C] | `not_black` on the hand-over screen (2.3) |
| Coverage | W7's three measures [C, human-input-testing.md 12] | the multiplayer screens and `PlayerChange` enter the UI inventory once reached; nothing new to build |
| Explorers, sweeper, kit | the kit's recipes and the sweeper act on `Game.GetActivePlayer()` [C, `sweep.py` 87-110, `kit/recipes.py`] | they work on whichever seat is active; a hot-seat situation is one more kit entry (not in this idea's first steps) |

### 3.1 The deny list

The MULTIPLAYER button is refused for a good reason: the internet and LAN paths start network sessions on the
workstation (CONTRACT 9). Hot seat is behind the same button. The narrowing H2 proposes: allow the main menu's
MULTIPLAYER (and the mods form's), add the multiplayer select screen as a front-end screen with its Internet, LAN,
Pitboss and Reconnect regions refused, so that only Hot Seat and Back can be pressed there. In the staging room the
chat is hidden in hot seat (StagingRoom.lua 1487); whether the invite button is (`ShowHideInviteButton`, line 117) was
not read, and H2 refuses it by region if it shows. Add `/InGame/PlayerChange/SaveMenu/Delete` to `DENY_PATHS` beside the other save menus' Delete. The change to
the list is the MC's (CONTRACT 9 is the contract's safety section).

### 3.2 The judge's screen list

`/InGame/PlayerChange` (and its `SaveMenu`) join `judge_lua.SCREENS`, and its buttons (`ContinueButton`,
`ChangePasswordButton`, `SaveButton`, `MainMenuButton`, `Yes`, `No`) join the candidate paths `uimap.py` asks
`HasMouseOver` of; the static read-only scan (`test_hi_judge_static.py`) is unaffected, since both only name paths. The world witness (CONTRACT 8.1, kind 2)
must not classify a click on the full-window black box as a plot hit: the box takes the mouse (`ConsumeMouse`), and a
shown screen already forces `pixel` or `control` hits (`judge.py` 358-359) once the path is in `SCREENS` [C].

### 3.3 `in_game` and the hand-over pause

In hot seat the right answer after a load is "in game, the hand-over screen up, paused by its seat", and the next
action is a posted Continue. `probe_in_game` should report that as its own kind (for example `handover`) when the
game is hot seat and `/InGame/PlayerChange` is shown, and `dismiss_load_screen` must leave it alone. Without this a
harness load of a hot-seat save either waits out its timeout or unpauses the game through Lua, which removes the
hand-over from the test [I, by the code of 3 and 1.3].

## 4. Why it is worth running on the product path

The hand-over exercises paths the single-player journeys never take or take only through the observer switch:
- the whole-map `GameplayFOWChanged` burst on a human-to-human change, which the server's fog records follow by events
  (`fog-of-war.md` 3.9 risk 5 [C there]) and which feeds the EXE's 4 MB queue (`../02-exe-internals/message-queue-and-fixed-buffers.md`,
  upstream #13397 names "when the active player changes, hotseat"; guarded on the product by EngineQueueGuard,
  `known-crash-causes.md` K3);
- `getPassword`, `GameplayActivePlayerChanged` and `PublishActivePlayer`, each crossed synchronously to the EXE by the
  proxy (`proxy/model/policy-table.tsv` lines 348, 412, 413) [C]; and `sendPlayerOptions(true)`, delivered by the
  server's net queue to the active player at delivery time (`server/src/srv_ifaces.cpp` 430-439) [C];
- the staging room's `Modding.ActivateAllowedDLC()` and UI restore before launch, on the route where the Architect
  left open whether the proxy loads before `GameView::Startup` (`../07-architecture-decisions/architect-2026-11-14-d33-at-run-time.md`,
  "NOT VERIFIED BY ME", multiplayer and hotseat);
- a paused game behind a modal screen while the server keeps its own clock (lockstep, D242) [I].

Knowledge-base lines that wait for a hot-seat run (each would be closed or corrected by H3 or H4):
`../08-performance/hidden-embarks.md` (d) and "Not verified" (the hot-seat test of step 2 is written there: two humans,
AIs, 10 turns, deferrals > 0, missed reveals 0); `../08-performance/turn-time-study.md` 0718 "Not verified" (which
seat's menu changes Quick Combat and Quick Movement in hot seat; the options menu sets them only "single player, not hot
seat", `../02-exe-internals/quick-combat-movement-seat.md` 1); `../02-exe-internals/leader-scene-player-limit.md` 7.9;
`../03-dll-engine-boundary/ui-lua-lock-holds.md` (`GameplaySetActivePlayer` not exercised);
`../07-architecture-decisions/deferred-ai-deals-spec.md` 3.5 and O4 (the Extra's hot-seat behaviour, and whether the
turn-start `DoTurn` advances counters twice); `known-crash-causes.md` "Not covered"; `docs/BETA-TESTERS.md` (players are
told hot seat is untested and to set `HiddenEmbarks = 0`).

## 5. Cost

| Item | Hours [I] | Places |
|---|---|---|
| H1 probe and start saves (stock, then one product boot) | 3-5 | one game instance, workstation, posted lane |
| H2 harness changes and tests | 7-11 | none for the hermetic part; one instance for the live checks |
| H3 the journey, recorded on stock and product, stock control 2 of 2 | 5-9 | one instance at a time |
| H4 the product checks of section 4 on the journey's saves | 5-9 | one instance; a quiet window only if a timing claim is made (none planned) |
| **Total** | **20-34** | |

Running cost once built: one more journey in a release candidate's HI stage, about the length of release-basic
(a Tiny save, three rounds) [I]. Nothing needs the real-input lane, a model, a second machine, or a second game
instance: one client plays both seats, which is the point of hot seat.

## 6. Step tickets, drafted for the MC (D274: each ready or blocked; ids from the MC, COMMON rule 26)

| Step | Title | State | Blocker | Touches (proposed) | Size [I] | Acceptance |
|---|---|---|---|---|---|---|
| H1 | Hot seat, live probe and start saves: set up a two-human Tiny hot-seat game on a test instance (VP 5.5 mods form) by front-end Lua (2.1 way 2) or, on the MC's waiver of the MULTIPLAYER region for this ticket, by posted clicks; record when the EXE shows `PlayerChange`, that it pauses and that Continue and Enter close it, the save folder, the load path, the load screen's behaviour, and one product-route boot through the staging room | ready | - | this file (a section "H1 live"), `research/06-tooling-and-workflows/runbook.md` (the set-up command), saves on F: by SHA-256 (never committed) | 3-5 h | `[L]` facts for 1.2 and 1.3 with build, instance and save named; two start saves (turn 0 and after one round) with hashes; stock route first, then one product boot that reaches the first hand-over with the server running (or the failure root-caused as a bug, COMMON 30); diagnostic pictures in the Visual Checks feed under the ticket (D112) |
| H2 | HI harness for hot seat: deny-list narrowing for the multiplayer select screen (3.1) and `PlayerChange/SaveMenu/Delete`; front-end signatures for the multiplayer screens; `/InGame/PlayerChange` in the judge's screens; `probe_in_game` kind `handover` and no unpause behind it (3.3); hot-seat save folder in the release stage; contract v1.13 | blocked | H1 merged (the live facts and the screens' regions) and the MC's yes to the deny-list change (CONTRACT 9) | `tools/hi/deny.py`, `frontend_screen.py`, `judge_lua.py`, `uimap.py`, `CONTRACT.md`, `tools/harness/frontend.py`, `tools/release/stages/hi_journey.py`, their tests; `human-input-testing.md` (a new section) | 7-11 h | hermetic tests for each change with a negative case each (Internet, LAN, Pitboss, Reconnect still refused; a single-player load still classified as before); `test_safety_static.py` passes; live on stock: a posted walk MULTIPLAYER, Hot Seat, Load, Launch reaches the first hand-over, `screen_open PlayerChange` and `state.player` waits answer |
| H3 | The hot-seat journey "release-hotseat" (2.3), recorded and replayed on stock and on the product, stock control 2 of 2; `journeys.json` entry with `game_type`, seats and folder; listed for the release candidate's HI stage only on the MC's word | blocked | H2 merged | `tools/hi/journeys/release-hotseat.jsonl`, `journeys.json`, `tools/release/stages/hi_journey.py` (if the stage needs the type), `human-input-testing.md` | 5-9 h | the journey passes `check-trace`; replays 2 of 2 on stock and on the product build of the current pins with every checkpoint passing (or `diverged` listed); no box; any patch-only finding filed as a bug with its root cause (COMMON 30) |
| H4 | Hot seat on the product: the checks of section 4 on H3's saves (HiddenEmbarks union of two humans' sight, the fog records after each hand-over, Quick Combat and Quick Movement per seat, the deferred-diplomacy Extra if merged, a larger map for the queue's whole-map burst) and the knowledge-base lines of section 4 corrected | blocked | H3 merged | the research files named in section 4; `docs/BETA-TESTERS.md` and `known-crash-causes.md` lines on hot seat | 5-9 h | each line of section 4's list closed `[L]` or kept open with its reason; `hidden-embarks.md` (d)'s pass rule met or a bug filed; beta-notes line updated to what was run |

## 7. Not verified

- Nothing here was run. The hand-over screen's behaviour is read from its Lua and XML (1.3); that the EXE shows it at
  every hand-over, and only then, is inferred (`[I]`, H1 checks it).
- Whether a hot-seat game can be set up and launched by front-end Lua without the staging room on screen (2.1 way 2)
  [I].
- The hot-seat save folder `ModdedSaves/hotseat` (only its unmodded sibling `Saves/hotseat` exists in the instances) [I].
- What the proxy and the server do across the staging room's `ActivateAllowedDLC` and UI restore (section 4) [I]; H1's
  product boot answers it.
- `not_black` on the hand-over picture (2.3) and the top-band glitch test of `capture.py` (the black box covers the top
  bar; on stock, PrintWindow's band test may call the frame a glitch until a retake agrees, `capture.py` 251-257) [I].
- The cost figures are estimates [I]; the files cited for the knowledge base's own facts carry their own marks and dates.
