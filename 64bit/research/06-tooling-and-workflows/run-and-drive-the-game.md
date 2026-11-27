# Run and drive the game

Two project skills plus the shell-gates tools. Skill commands below are as written in the skills (run from the
Community-Patch-DLL repo root as `.claude/skills/...`, or from the durable copies in `%USERPROFILE%\.claude\skills`).
Flags listed are only those seen in the sources.

## 1. The skills and their recovery

| Skill | Purpose | Main scripts |
|---|---|---|
| `vp-dll-dev` | build/install, start or load games, bounded autoplay runs, stats.db | `vp_build.py`, `vp_game.py`, `vp_modgame.py`, `vp_stats.py`, `vp_common.py`, `reset-to-main-menu.bat`, `assets/` (patched MainMenu.lua, FrontEnd.lua, LoadScreen.lua, RunAutoplayGame.lua) |
| `civ5-game-ui` | drive a live game: Lua channel, screenshots, input, session state | `vp_lua.py`, `civ_ui.py`, `game_session.py`, `dismiss_assert.py`, `gen_lua_api.py`; `reference/lua-api.md` (3,466 methods), `cookbook.md` (81 recipes), `engine-api-observed.md`, `firetuner.md`; `experiments/` |

History to know (memory notes, 2026-11-09): the skill sources were tracked by accident in `memory-diagnostics`
commits f92e3d586 and **f181104a2**; branch checkouts deleted them (lost for good by `git checkout -b diplomacy-log
master` on 2026-10-28; between 10-07 and 11-09 only `__pycache__` bytecode was left, which Python 3.11 can run
directly). **Recovered 2026-11-09** with `git show f181104a2:<path>` (all 67 civ5-game-ui files; vp-dll-dev's
SKILL.md, 3 assets, 5 scripts); `reset-to-main-menu.bat` was never in git and was reconstructed from a transcript.
Durable copies: `%USERPROFILE%\.claude\skills\vp-dll-dev` and `...\civ5-game-ui`, each with `RECOVERY.md`; keep them
side by side (they import each other through `HERE.parents[1]`). `.claude/.gitignore` (`*`) has prevented a repeat
since 10-30. `vp_common.py` falls back to `~/Documents/GitHub/Community-Patch-DLL` when it cannot derive the repo.
`civ_ui.py` needs pywinauto from user site-packages: do not run it with `python -I`. `civ_bg.py` finds the skills via
`CIVBG_SKILLS_DIR` (default `~/.claude/skills`).

## 2. Launching: `vp_modgame.py` (mods paradigm) and `vp_game.py` (modpack)

```bash
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py status          # resolved paths + preflight; --mods lists installed mods
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py launch --mods cp,vp,eui --load "OOM-G88_0280"
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py launch --mods cp,vp,eui,infoaddict --new-game --ais 7
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py launch --mods cp,vp,eui --menu-only
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py stop
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py restore-lua     # (GATES.md; also vp_game.py restore-lua)
python .claude/skills/vp-dll-dev/scripts/vp_modgame.py unblock         # unpause and clear popups in a running game
```

- `--mods` takes aliases (`cp vp eui squads infoaddict unitscaling quickturns vernstweaks autoplay ige`) or any
  substring of a mod Name, **in activation order**. `--exclusive` is the **default**: every other enabled mod is
  disabled, and stays disabled after the run.
- `--new-game` options: `--map` (map script basename), `--size`, `--ais`, `--minors`, `--speed`, `--era`,
  `--handicap`, `--maxturns` (verified 2026-09-23: `--map Continents --size WORLDSIZE_STANDARD --ais 7` gave 80x52
  with 8 majors and 16 city-states).
- It launches detached and returns; `--wait SEC` blocks until really in play. It does not babysit a turn target:
  drive turns with the Lua channel, e.g. `vp_lua.py "Game.SetAIAutoPlay(350)"` (hands the human slot to the AI for
  350 turns and gives it back; the `autoplay` mod's `SetAIAutoPlay(1,-1)` instead leaves a permanent observer).

**How it gets past the Mods menu:** the `-Automation` Lua state is a bare MainState, so the work happens in the
skill's patched `Assets\UI\FrontEnd\MainMenu.lua`, configured by rewriting five locals at its top: `loadOnStart`,
`saveNameFilter`, `modsToEnable`, `modsExclusive`, `newGameSetup`. Two non-obvious parts that must not be
"simplified": MainMenu's show handler calls `Modding.ActivateDLC()`, which deactivates every mod (the patch skips it
once the wanted set is live); and activating mods swaps the whole UI out, so "are the wanted mods active?" is asked
of `Modding.GetActivatedMods()` each time (`.ID`; `GetEnabledModsByActivationOrder` yields `.ModID`). A per-launch
`loadToken` in `Modding.Get/SetSystemProperty` (in `Civ5ModsDatabase.db`) stops a double load after the UI re-executes
MainMenu.lua (a `local`, `PreGame.GameStarted()`, `PreGame.GetLoadFileName()` and `_G` all fail as guards; the
double load showed as `0xc000001d` in `CvDllGame::Uninit`). **The token is used by one launch only (auditor cycle 3
C5, 2026-11-13):** a relaunch without a new `frontend.py prepare` finds its token already recorded, does not load, and
sits at the main menu with the Lua channel silent (two workers lost 13 min to this on 2026-11-11, I-0582, I-0588).
Since tracker 4000, `cli.py launch` records the token it launched with in the run's `frontend.json` and refuses
(exit 2, "already used by the launch at ...; prepare again") a second launch with the same token; `--reuse-load-token`
launches to the menu on purpose. Launches that do not go through `cli.py` (`launch.launch` called from a script) are
not checked: prepare before every launch there [C, `tools/harness/tests/test_cli_exit_codes.py`]. It checks `Modding.CanLoadSavedGame(file)` first (0 ok,
2 missing DLC, 3 DLC not purchased, 4 missing mods, 5 incompatible mods). `UI.SaveFileList`'s third argument selects
autosaves only, so the asset lists both lists (fixed 2026-09-21; "438 saves visible"). The patched `LoadScreen.lua`
(`autoBegin`) clicks "Begin your journey"; without it a load sits paused (`Game.IsPaused()`); rescue:
`vp_lua.py --state LoadScreen "Events.LoadScreenClose() UI.SetDontShowPopups(false)"` then
`vp_lua.py "Game.SetPausePlayer(-1)"`.

**`vp_game.py` (modpack, AI autoplay, turn-bounded):** `start --turns N`, `load --turns N [--save-turn T | --save
SUBSTRING | --save latest]`, `--keep-running`, `--turn-timeout`, `--startup-timeout` (default 600 s),
`--max-runtime`, `--death-grace`, `--no-prune`, `stop`, `status`, `restore-lua`. Results in
`%LOCALAPPDATA%\vp-dll-dev\runs\<timestamp>-<kind>.{log,json}` (`status` one of `ok`, `load_failed`, `lua_error`,
`startup_timeout`, `turn_timeout`, `process_died`, `crashed`, `no_process`, `max_runtime`, `error`; plus
`final_save_settled`, `pruned_saves`, `game_logs`). Always run it in the background; the process exit is the
completion signal; do not poll. The `headless-dll-ab-test` note (2026-10-30) said "vp_game script is gone"; it was
recovered on 2026-11-09.

**Known launcher traps:**
- **`--turns N` is not honoured on `load` runs** when the save's filename turn and the DLL's logged `Turn` differ
  (e.g. an Industrial-era start begins at game turn 250 while the logger stamps elapsed turns: 329 - 250 = 79): the
  target is computed from the filename. NOT fixed. Check `target_turn` against the logged `Turn`; pass
  `--max-runtime`.
- **Map size is not selectable through `vp_game.py start`**: it replays whatever PreGame holds. `config.ini`
  `[UserSettings] LastMapScript/LastMapSize` and the cached `AdvancedSettings` were ignored
  (`PersistAdvancedSettings = 1` makes them lie; Huge is 5, there is no 6); and **the map script overrides the size
  table** - `Communitu_79a.lua` declares Standard 79x53, Large 87x59, Huge 97x66 = 6,402 plots, against stock Huge
  128x80 = 10,240. Use a stock script (Continents) for the full grid; verify with
  `SELECT NumPlots, GridWidth, GridHeight, AliveMajors FROM MemEntityCounts ...`. `vp_modgame.py --new-game` is the
  way to choose map and size.
- **The `-Automation` Lua state has no `print`**: `print`, `pcall`, `pairs`, `tostring`, `PreGame`, `GameInfo` and
  `debug` are nil there; calling one aborts `RunAutoplayGame.lua` before `Events.SerialEventStartGame()` and the game
  sits at the main menu (looks like a DLL hang). Keep that file to its one line. Diagnostics trick: runtime errors
  do reach `Lua.log`, so `SOME_NAME.x = 1` after `SerialEventStartGame()` reports a fact; `vp_game.py` ignores
  errors containing `VPDEV_DIAG`.
- Launching the EXE directly normally makes Steam kill it and start its own copy (vp-dll-dev SKILL; scripts adopt
  the new PID). But `civ_bg.py launch` on 2026-11-09 "started the EXE directly (no Steam relaunch, same pid
  throughout)", and on 2026-09-22 the DX9/Tablet EXEs launched directly without a swap (`steam_appid.txt` in the
  install root). The sources disagree; expect either.
- `--keep-running` only survives if the launching console stays open (a wrapper `.cmd` started with
  `cmd /c start "" /min runload.cmd`, ending in `ping -n 5400 127.0.0.1 > nul`).
- Git Bash mangles single-slash switches: write `tasklist //FO CSV //NH //FI ...`, `taskkill //F //IM`. A wait loop
  needs three exits: wall-clock deadline, process liveness, and the launcher's real failure vocabulary
  (`CRASHED run ...`).
- Background jobs from an abandoned launch adopt the next game ("the running Civ 5") and re-issue commands (seen
  2026-09-21 and 09-23). Kill old jobs first.
- The game truncates same-named logs in `USER_DIR\Logs\` at every start; `vp_game.py` archives `Lua.log`,
  `CustomMods.log`, `Database.log`, `xml.log` per run. `Runtime Error: Error loading VPUI_loader.lua.` appears on
  every good run.
- `reset-to-main-menu.bat` (double-click) restores the stock front-end Lua, kills a leftover game, reports a stray
  `TurnByTurn` holder, prints the installed build and offers a Steam launch.

**Mods database and `vp_modgame` facts (2026-11-10, tracker 0071 phase A, read-only on a copy of the owner's
`cache\Civ5ModsDatabase.db`):** `ModRelationships.Type` is 1 = References, 2 = Dependencies, -1 = Blocks [C, checked
against the modinfo files]. `vp_modgame.installed_mods()` only sees `MODS/*/*.modinfo`, so a mod whose modinfo is
nested deeper (Continental Drift) cannot be named in `--mods` [C]. With `--no-exclusive`, or `--mods` naming a mod the
engine will not activate, the patched MainMenu re-activates on every pass, because `vpDevModsAlreadyActive` compares
`#activated == #wanted` [C, from the code; not seen live]: a second "activating" line in `Lua.log` means abort.
`vp_modgame.py` records nothing about the set it disables. The owner decided on 2026-11-10 that the enabled/disabled state
of the Steam install's mods need not be preserved or restored (tracker 0071, dropped).

## 3. The Lua channel (`vp_lua.py`)

```bash
python .claude/skills/civ5-game-ui/scripts/game_session.py luaexec on      # opt-in, once, no restart
python .claude/skills/civ5-game-ui/scripts/vp_lua.py "return Game.GetGameTurn(), Game.GetActivePlayer()"
python .claude/skills/civ5-game-ui/scripts/vp_lua.py --state InGame "return ContextPtr:GetID()"
python .claude/skills/civ5-game-ui/scripts/vp_lua.py --states
python .claude/skills/civ5-game-ui/scripts/vp_lua.py --json -f script.lua
```

- Opt-in: the DLL ignores requests until `luaexec.enabled` exists in the **cache folder** (`USER_DIR\cache`), or the
  game was started with `VP_LUAEXEC=1`; `VP_LUAEXEC=0` forces it off. Event `Local\VPLuaExec`, request file in the
  cache folder.
- **Any Lua state:** `--state NAME` picks one of ~150 environments by `StateName` (InGame, TechTree, CityView,
  MiniMapPanel, UnitFlagManager, CityBannerManager, LoadScreen, ...); default `Main` resolves to the first that can
  see `Game` (the game-core API is in thread environments, not `_LOADED._G`); `--state _G` is the raw global table.
  Inside a chunk: `vp` (persistent table), `States()`, `G`. `print` is captured. Answers in 0-15 ms.
- Dead in the front end and while loading (`game_session.py wait-ingame` polls). Error line numbers include two
  header lines (`luaexec:N` = your line N-2).
- Runs on the game-core thread inside `ICvEngineScriptSystem1::CallCFunction` (holding the Lua lock) on the memory
  probe's persistent Lua thread. Hence the two hard rules: **no NO_*/NUM_* values or unvalidated IDs** (a failed C++
  ASSERT opens a system-modal dialog while the lock is held: the game freezes; OK continues, Cancel/Enter exits;
  `dismiss_assert.py --check` shows it; answering is a human's call), and **no window-touching engine calls**
  (`OptionsManager.CommitResolutionOptions()` deadlocked the game on 2026-11-09; use a one-shot
  `Events.LocalMachineAppUpdate` handler on the main thread).
- **Lua calls that are unsafe** (game state changed from the channel crashes the DLL; first note, 3910, 2026-11-15):
  a chunk that called `plot:SetOwner(player, -1, false, false)` (no acquiring city) on unowned studio plots (one
  with its improvement just removed, one to get `ChangeBuildProgress`) and `plot:SetImprovementType(...)` on others
  crashed the DLL [L, c5h-a, 2026-11-15 00:17:44 EST, pin `i0ship-26234371-ia`, Tiny copy `C5H-0301-CAT2_0009`]:
  AV reading 0x9d0 at DLL RVA 0x340379, stack (llvm-symbolizer on the pin's pdb, inlined frames)
  `CvPlot::setOwner` (CvPlot.cpp:7178) -> `setOwningCity` (9835) -> `CvPlot::setImprovementType` (8764) ->
  `CvCity::ChangeImprovementCount` (CvCity.cpp:17596) on a null city. Reading [C, source]: `setImprovementType`
  calls `pOwningCity->ChangeImprovementCount` whenever the plot is owned, and a plot made owned with no acquiring
  city has no owning city; which of the chunk's calls hit it first is not established [I]. Rule: **never call
  `SetOwner` from Lua with no acquiring city**; the scenario drivers set no plot owners (3910 driver: landmarks on
  unowned plots or on plots a city already owns). A Lua-caused DLL crash is not a fault of the pin. Minidump kept in `F:/c5-64bit-work/0301/3910/crash/`.
- `stats.db` lags the live game (rows batched until 1,024), so ask the game over the channel for live progress.
- Not available on the `engine-queue-guard` branch (no channel outside memory-diagnostics-derived builds).
- **No `loadstring` (ticket 0591, 2026-11-19; epic 0590).** Firaxis' build 26577625 sets `_G.loadstring` (and `io`,
  `os.execute` ...) to nil in the engine's one Lua VM; `load`, the C API and `luaL_loadbuffer` stay
  (`02-exe-internals/new-exe-build-26577625.md`, "The Lua sandbox"). The channel's runner (`LUAEXEC_RUNNER`) took
  `G.loadstring` to compile every request, so every request would answer `ok=0` on that build. Now:
  - **Proxy** (`proxy/src/proxy_luaexec.cpp`, the product route): `RunExternalLua` passes the runner a C function
    `LuaExecCompile` (`luaL_loadbuffer` with the request's byte count; returns function, or nil and the message, as
    `loadstring` did; chunk name `=luaexec`, so error lines read `luaexec:N:` as before) as its third argument
    (`local _, _, compileC = ...`; the first line stays `local REG, T = ...`). Without that argument the runner compiles
    with `load` and a reader function; with neither it answers `ok=0` "no compiler". The runner text no longer names
    `loadstring`. Answers are otherwise byte-identical in form.
  - **Unit test** `proxy/tests/luaexec/test_luaexec.py --scratch DIR` [C, 2026-11-19]: the file under test compiled
    with `VP_LUAEXEC_UNIT_TEST` (engine parts left out) against stock Lua 5.1.4 from source, in a state that nils
    `loadstring`, `io` and the `os` functions as 26577625 does, with a metatable on `_G` that counts every read of
    `loadstring`. 23 checks (results, syntax and run-time errors, print capture, persistence, unknown state, the byte
    count honoured, `load` nil too, the `load(reader)` fallback, a non-string chunk, and a negative control: the old
    runner fails there and the trap sees its read): 23/23 on the proxy toolchain (clang-cl -m32, VC9) and under MSVC
    `/fsanitize=address`; `tools/harness/tests/test_luaexec_modpack.py` 9 passed unchanged.
  - **Live** [L, c5h-c, old build 4390913, 2026-11-19 22:49-22:59 EST, batch-5 layout x64-1fd51353-p1e09fc7 with the
    pin's proxy rebuilt with only this change, 920d53f0...; driver `proxy/tests/luaexec/live_0591.py`]: the new build's
    sandbox reproduced by setting `_G.loadstring = nil` through the channel itself after load; with it nil, results,
    syntax and run-time errors (`=luaexec:3:` with the two harness header lines), print, the persistent table, the
    states InGame, `_G` and an unknown one all answered, and `turns.py` played 2 AI turns. Side finding: the channel
    thread's own environment (Main, mods form) still held a `loadstring` copied at load while `_G`'s was nil, and has no
    `load` of its own (`type(load)` nil there; the runner takes `load` from `_G`). Not yet run on a build-26577625
    instance (0590's provisioning of such instances had not merged).
  - **The DLL's own channel** (the 32-bit `-ia` builds of the stock route): the runner lives on the fork's
    instruments branch (440878983), not on the x64 or i0 pins; the `-ia` build applies it as a patch
    (`tools/release/stages/build.py` `IA_PATCHES`; the pins since batch 3 take
    `tools/d10/evidence/0463/ia-39f17f754-440878983.patch`). The same change is on the new fork branch
    `ia-luaexec-no-loadstring` d45303627 (parent 440878983, pushed 2026-11-19); its delta applies cleanly on i0 pin
    5d18d3d6 after 0463's patch, and the compile block, the call and `LuaExecCompile` are text-identical to the
    proxy's [C, `git apply --check` and a text comparison, 2026-11-19]. Until an `IA_PATCHES` row (or a regenerated
    0463 patch) carries it, `-ia` DLLs still read `loadstring`: the stock route's channel on build 26577625 answers
    `ok=0` for every request [I, from the runner text; not run].
  - Offline harness tests that run Lua in their own interpreter (`tools/harness/tests/test_hi_*.py`,
    `test_diplo_rearm.py`) still call `loadstring` there; they never run in the game, so the build does not touch them.

## 4. Screenshots and input

**Validation screenshots go to the Visual Checks feed (one-off 9051, 2026-11-13).** The harness `shot`
(`tools/harness/cli.py -i NAME shot OUT --what ... --result pass|fail|info --issue N --reference STOCK.png --ref-how
same`) records every shot of the real game into `D:/c5-64bit-work/visual-checks` with the D94 asset set; a shot made
another way (`civ_bg.py shot` below, `terrainshot`, a figure) is added with `tools/visualcheck/visualcheck.py record`.
The owner reads them in the tracker tab Visual Checks. Details: [visual-checks.md](visual-checks.md).

Two tools; on a background game use only `civ_bg.py`.

| | `civ_ui.py` (civ5-game-ui) | `civ_bg.py` (shell-gates nofocus) |
|---|---|---|
| Screenshot | `shot out.png --scale 0.5` via `PrintWindow(PW_RENDERFULLCONTENT)`; works covered/unfocused; see 4.1 for the product path; `--method screen` grabs the screen | `shot out.png --scale 0.5`, same PrintWindow, on the cloaked window |
| Input | `key "{F6}"`, `click 0.4995 0.3749`, `wheel 960 540 -5`, `move`; **refused (exit 4) unless the game is foreground and under the point** - it focuses and uses SendInput | posted window messages (`WM_KEYDOWN/UP`, `WM_SYSKEY*`, `WM_MOUSEMOVE`, buttons, wheel); no focus needed |
| Info | `info` (client size) | `status [--json]` |

Coordinates: integers are client pixels, `0.x` floats are client fractions. Fractions survive moving the window but
**not a resolution change** (UI anchored in pixels). The skill's menu fractions were measured at client 1914x1051;
the owner's install runs 2560x1440 (client 2554x1411) since 2026-09-20; re-measure from a screenshot. Hotkeys: Y yields,
R resources, F6 tech tree, Enter end turn, Shift+Enter force end turn, Esc. `Game.DoControl(CONTROL_YIELDS)` /
`CONTROL_TECH_CHOOSER` work from the channel when input would need focus.

### 4.1 Picture rounds: what moves the camera or spoils a shot (cycle-6 audit, adopted by the MC 2026-11-14 23:53) [L]

Read this before the first picture round of any task; each line cost a live epic time.
- **A unit selection or a left click on a unit recentres the camera** (overlay-system.md 9.1). `UI.ClearSelectionList()`
  does not leave nothing selected while a unit needs orders: the game selects the next unit and pans. Before the
  first shot put every unit of the active player to sleep (`PushMission(MISSION_SLEEP, ...)` (arrows-and-path-line.md 2) with numeric
  arguments; a boolean argument fails into the stock log, not the channel) and turn the auto unit cycle off. Put a
  unit you clicked back to sleep before the next arm. On a stacked plot a click selects another unit: choose a plot
  with one unit.
- **Read the camera after every shot** and fail the arm when it moved (the 3804 driver's `camera_equal`).
- **PrintWindow on the product path** returns flat grey or UI-less frames for a share of the shots (proxy-design.md
  20): ACCEPTANCE pictures come from the composite's back-buffer dump; a PrintWindow shot is kept only if it is not
  flat and its top bar matches stock (arrows-and-path-line.md 11, overlay-system.md 12). Measured 2026-11-16 (ticket
  0401, huge save, 3 runs): 8 of 30 captures flat, 2 UI-less, against 0 of 30 back-buffer dumps; the presented frame is
  never grey. Since 0401 `shot` / `cli.py shot` retake a one-colour, not black capture up to 4 times, 1 s apart (result
  fields `flat`, `flat_retakes`; `--flat-retakes 0` for the raw capture); UI-less captures are still not caught
  (`05-graphics-composition/terrain-surface-verification.md` 13).
- **Lua state:** stimuli run in the `WorldView` state (in the default state `Vector4` is nil); the UI sandbox has no
  `rawequal`; `;` is the Lua channel's history separator.
- **Keys:** send no key after a state is set (ESC opens the game menu).
- **Look at one contact sheet before running the series** on the second map.

## 5. Session state: human mode, popups, freezing

`game_session.py`: `status`, `human-mode on` (comments out `Game.SetAIAutoPlay(1,-1)` in the modpack's
`Mods/autoplay/autoplay.lua`; always `restore` - autoplay runs time out silently while it is on), `diplo-shutup on`
(`DIPLOAI_SHUT_UP = 1` in the modpack's merged `Override/CIV5Units.xml`; needs a restart; verify with
`Game.GetCustomModOption('DIPLOAI_SHUT_UP')`), `luaexec on`, `lua-profiler`, `launch`, `autoload "<filter>"`,
`wait-ingame`, `quit`, `restore`, `quick-anim on`. Human mode and diplo-shutup target the **modpack**; in the mods
paradigm the AI-popup problem is handled by `wait_turn.py --answer` (below) and `vp_modgame.py unblock`.

- An AI trade/peace/demand screen **blocks the AI turn indefinitely**; ESC does not close it. A posted click at
  client (0.49, 0.977) hits the last (bottom) button on leader discussion dialogs, which is not always the refusing
  one. **Corrected 2026-11-10 (1117, from epic 1300's c5h-b run):** on a two-button deal screen it does NOT hit
  REFUSE: REFUSE is at ~0.459 and ACCEPT at ~0.541 of a 2554x1411 client, and 0.49 falls between them [L]. Replaced
  in the harness by a located click (section 12.3). (Was: "hits the neutral/last button on leader dialogs and REFUSE
  on deal screens".)
- Turn-start popups close through their own state: `OnCloseButtonClicked()` in `GreatPersonRewardPopup`,
  `UIManager:DequeuePopup(ContextPtr)` in `TechAwardPopup`; repeat until `UI.IsPopupUp()` is false.
  `UI.SetDontShowPopups(true)` stops new ones for the process.
- When AI autoplay expires the human slot is seated and every queued popup lands (`BUTTONPOPUP_WHOS_WINNING` is
  periodic): `vp_modgame.py unblock`.
- **Freeze switch:** hold the named mutex `TurnByTurn` and the DLL pauses (`CvGame::ExternalPause`, compiled into
  every build): renderer and UI stay live at ~5 fps (`Sleep(200)` per tick). Release it to resume; close the holder
  properly (an abandoned mutex leaves the game paused for good).

**Where the diplomacy overview and the leader screen open and close (2026-11-10, issue 1110, static reads)** [C]:
the stock `Assets/UI/InGame/Popups/DiploOverview.lua` (context `/InGame/DiploOverview`, Expansion2 `InGame.xml` line
45) toggles on `BUTTONPOPUP_DIPLOMATIC_OVERVIEW` with `Data1 == 1` in `OnPopup` (lines 12-27; closes if shown, else
`QueuePopup(InGameUtmost)`); `OnClose()` (83-84) is `UIManager:DequeuePopup(ContextPtr)`; no VP or EUI file in the owner's
MODS overrides it. EUI's `LUA/LeaderHeadRoot.lua` (lines 357-363): `OnClose()` = `DequeuePopup(ContextPtr)`,
`UI.SetLeaderHeadRootUp(false)`, `UI.RequestLeaveLeader()`, wired to `Controls.BackButton` (`OnReturn = OnClose`);
its `AILeaderMessage` handler (line 351) queues the context with `PopupPriority.LeaderHead` (line 348). While a leader
screen is up the Lua channel is silent (`CvGame::update` is held), so a close must be armed beforehand. No Lua
interpreter (lua, luac, luajit) is installed on the owner's machine: Lua chunks can be checked only live.

## 6. Ending a turn unattended: `end_turn.py` + `wait_turn.py`

From `experiments/shell-gates/tools/` (both call `../nofocus/civ_bg.py lua`):

| Tool | Usage | Does | Exit codes |
|---|---|---|---|
| `end_turn.py` | `python end_turn.py [--tries 40]` | per try, one second apart: cities with an empty queue get `PROCESS_WEALTH`, ready units `SetMoves(0)`, pending promotions cleared, research picked if none; if the blocking type is -1 or `ENDTURN_BLOCKING_UNITS`, `Game.DoControl(CONTROL_FORCEENDTURN)`; otherwise dismiss the blocking notification (`DismissNotification(GetEndTurnBlockingNotificationIndex(), true)`; city range attack, pending deal, ...) | 0 once forced, 1 after all tries |
| `wait_turn.py` | `python wait_turn.py FROM [--timeout 900] [--stall 60] [--shot path.png] [--answer N]` | polls `Game.GetGameTurn()` and `IsTurnActive()` every 3 s; on a channel stall (a leader screen holds `CvGame::update`) posts a move + click at (0.49, 0.977) up to N times | 0 turn advanced and active; 2 channel silent for `--stall` s; 3 timeout; 4 process gone |

Typical: `python ../tools/end_turn.py && python ../tools/wait_turn.py <turn> --answer 12 --stall 45`. Huge-map AI
turns took 24-86 s on 2026-11-09 (t350 save); the original skill recipe (two clearing passes, then FORCEENDTURN) is
in civ5-game-ui SKILL "Ending a turn unattended".

**`tools/harness/turns.py wait`'s default stall, first AI turn of a launch (ticket 0550, 2026-11-18) [C, offline]:** while
no AI turn of the current launch has been measured (the state file's `ai_turns` absent or from another pid), the default
stall is `STALL_FIRST_S` = 600 s instead of 60 s; from the second turn on it stays 0158's max(60 s, 2 x the longest AI
turn of the launch), and an explicit `--stall` still wins. Reason: epic 0528's worker 4854 (M2 on oversize 43-civ saves)
had two T1600 loads' first turn waits cut at 177-179 s while the EXE was still working (it answered 25 s later, no
crash). An instance without a readable state file keeps 60 s (nothing is ever measured there). Tests:
`tools/harness/tests/test_turns_first_stall.py`. This replaces "first wait 60 s (no AI turn known)" in
`capacity-series.md` section 3 (row "Harness: turn wait") and section 7 for runs after 2026-11-18.

## 7. No-focus driving: `civ_bg.py`

*Note 2026-11-21 (one-off 9080): the commands below are the 2026-11-09 experiment's. `experiments/shell-gates/`
is not in this repository (only the verbatim copy `research/sources/shell-gates/tools/civ_bg.py`, without its
`bgwin.py` and `bin/`), so they do not run from a checkout. On test instances the no-focus launch is
`python -I tools/harness/cli.py -i <inst> launch` and the same verbs of `cli.py` (`runbook.md` section 3.5).*

Purpose: live experiments while the owner works or games, without the game ever taking focus or covering anything. Used
for every live run of 2026-11-09 (menu test, baseline, B4, seven Gate 3 launches) with no slip.

```
cd experiments/shell-gates/nofocus
build.bat                                                     # once: bin/stand_in.exe, bin/focus_guard.dll, bin/bg_helper32.exe
python civ_bg.py launch -- --mods cp,vp,eui --load VP8P-HUGE_0350 --wait 900    # vp_modgame.py launch args after --
python civ_bg.py watch 3600                                   # 20 ms foreground/visibility monitor, exit 5 on a slip
python civ_bg.py shot out.png --scale 0.5
python civ_bg.py key ESC | key F6 | key SHIFT+ENTER | key "{F6}" [--hold 60] [--char C]
python civ_bg.py click 0.5 0.5 [--right] [--hold 50] | move 0.3 0.4 | wheel 960 540 -3
python civ_bg.py lua "return Game.GetGameTurn()"              # = vp_lua.py; its options after --
python civ_bg.py status [--json] | front | back | stop
python civ_bg.py guard on|off | cloak on|off | isolate on|off | pretend-active on|off | park [offscreen|bottom|X,Y]
python civ_bg.py inject | wake
```

**How it works:**
1. `launch` starts the EXE via `bin/bg_helper32.exe` **suspended** with `STARTUPINFO.wShowWindow =
   SW_SHOWNOACTIVATE` and loads `bin/focus_guard.dll` with a remote `LoadLibraryW` before the first instruction; for
   the real game it wraps `vp_modgame.py launch` and replaces only its spawn step. It refuses `FullScreen != 0` in
   `GraphicsSettingsDX11.ini` and refuses to start if the game is already running.
2. The guard inline-hooks the process' copy of **user32** (never the CEG-protected EXE): `SetForegroundWindow`,
   `BringWindowToTop`, `SwitchToThisWindow`, `SetActiveWindow`, `ClipCursor` become no-ops; a `WH_CBT` hook vetoes
   activation; `ShowWindow`/`SetWindowPos` lose activate/raise; `SetWindowsHookEx(WH_KEYBOARD_LL / WH_MOUSE_LL)` is
   not installed (the game's LL hook swallows Alt+Enter machine-wide); `GetAsyncKeyState`/`GetKeyState` hide the owner's
   keys and report keys civ_bg holds; `GetCursorPos` returns civ_bg's virtual cursor. Shared memory
   `Local\CivBgGuard_<pid>`, log `%TEMP%\civbg_guard_<pid>.log`.
3. The main window is **cloaked** (`DWMWA_CLOAK`, set by the guard since only the owner can cloak): DWM keeps
   composing it, PrintWindow returns live frames, it is click-through. It is also parked off-screen at the bottom of
   the z-order.
4. Input = posted window messages; game actions = the Lua channel.

| Mode (stand-in measurements) | Renders | PrintWindow | Keys / clicks | Focus taken |
|---|---|---|---|---|
| Normal desktop, no guard | yes | yes | yes / yes | **yes** |
| Minimised + guard | **no** (occluded) | fails | yes / yes | no |
| Off-screen + guard | ~32 fps | yes | yes / yes | no |
| **Cloak + off-screen + guard (default)** | **~32 fps** | **yes** | **yes / yes** | **no** |
| Cloak + `--park bottom` + guard | monitor rate (165 fps) | yes | yes / yes | no |
| Separate desktop (`--mode desktop`) | **no**: every Present occluded | all white | yes | impossible; Lua-only mode |

Rejected: VMware VM (one Steam game per account at a time, full install, 8-12 GB guest RAM, different
address-space numbers because the SVGA driver replaces NVIDIA's), RDP / second session (impossible on Home), Task
View desktops, virtual display driver, guard compiled into the VP DLL (loads too late).

Options: `--mode cloak|offscreen|desktop`, `--park offscreen|bottom|X,Y|none` (default offscreen; `bottom` = full
frame rate, still cloaked; GATES used it for Gate 3), `--settle` (default 120 s), `--inject-mode
suspended|running`, `--no-guard` (never while the owner works), `--stand-in` / `--exe PATH [--proc NAME]` to drive a
substitute; `VP_EXE_NAME` picks the game EXE (default `CivilizationV_DX11.exe`).

Why it works (Ghidra on the EXE): `SetForegroundWindow` is used only by `FUN_00842920`
(`FindWindowA("WinClass_FXS", title)`), which DX11 startup never reaches. The real single-instance check is the
mutex `Local\Civ5App` (*corrected 2026-11-10 from "used only by the single-instance check"; see
headless-and-parallel-testing.md section 4.5*). `SetActiveWindow` runs at every window-mode set (startup, resolution
change); hotkeys arrive as window messages (`GetAsyncKeyState` only for Ctrl/Alt drag modifiers; no `GetKeyState`,
raw input or DirectInput).

Residual risks: Steam relaunching the EXE (new process without the guard; civ_bg warns and injects, but may lose the
race); system-modal assert boxes (topmost, not cloaked, visible without focus); WerFault / crash dialogs (other
processes); `front` (on purpose; `back` re-arms); exclusive fullscreen (refused); GPU cost of background rendering;
antivirus may flag a suspended start + remote thread. **Incident:** the stand-in took the owner's foreground three times
during development on 2026-11-09 (17:27:26 for ~12 s; twice around 17:47 with `--inject-mode running` / `--no-guard`
+ `inject`). The foreground lock did not prevent it: the guard must be in place from the first instruction.

Note: `civ_bg.py stop` terminates the process (psutil `terminate()`, then `kill()` after 15 s). See the safety box in
README.md about autosaves.

**Safety note: a forced stop from a driver's cleanup path (2026-11-10, epic 1100, issue 1114).** [L] At 04:11:21 EST a
1114 driver script, after `tools/harness/crash.py` raised `PermissionError` while taking its baseline (it opened the
game's locked `install\CvAssert.log`), ran its `finally` block, which called `launch.stop(force_unsafe=True)`. That
terminated Goldberg instance `c5h-a` in the front end during mod activation. It was harmless this time because no
game was loaded (no autosave or save could be in writing); the only possible damage was the instance's own cache
databases, which the game rebuilds at the next activation, and nothing outside `c5h-a` was touched. It is the hazard
the "never kill mid-autosave" rule exists for, so the harness keeps five hard rules (MC, 2026-11-10; enforced with
offline tests, issue 1115):
1. No code path calls a forced stop automatically: `force_unsafe` (or its successor) is reachable only by an
   explicit argument a coordinator passes after deciding, never from exception handlers, `finally` blocks, atexit
   hooks or test teardown.
2. Stop acts only on PIDs the harness launched and recorded, verified by PID plus process start time, never by
   image name; a CivilizationV process the harness did not start (the owner's own game) can never be a stop target.
3. With a game loaded, stop refuses unless the newest autosave's size and mtime have settled and the turn is the
   human's; in the front end it is allowed only when no mod activation or save operation is in progress, otherwise
   it waits with a timeout and then escalates instead of forcing.
4. When a driver script fails, the instance is left running and the failure reported: an orphaned instance is the
   safe state, a killed one is not.
5. Evidence capture (`crash.py` and the like) never raises into control flow.

**Safety note: a DLL message box on the owner's desktop (2026-11-10, epic 1300's run on Goldberg instance `c5h-b`).**
[L] Pid 45256, epic 1300's first run, AI turn 354 to 355 (ended 05:38:13 EST). At 05:39:37 the Lua channel went
silent and stayed silent; the harness's `shot` (PrintWindow) hung; CPU was near zero. Cause (found by epic 1300 with a
non-invasive cdb stack): the DLL's own precondition dialog, `CvPreconditionDlg` -> `MessageBoxA` on the game-core
thread inside `CvPlayerEspionage::DoTurn` -> `LogSpyStatus` -> `CvGlobals::getCityEventChoiceInfo` ("Expression:
e > -1", `CvGlobals.cpp` line 4020), holding the game-core lock while the window thread waited for it. **The message
box had a NULL owner and was visible and uncloaked at screen (1097,569) from about 05:39 to 05:47 EST on the owner's
desktop.** It never became the foreground window. The focus guard cloaks only the game's main window, so any other
top-level window of the process (DLL or EXE message boxes, assert and crash dialogs) could appear. This is residual
case 2 of the nofocus README, now seen live. The MC authorised pressing its OK button by a message to the dialog;
the DLL then crashes on purpose. Requirements (MC, 2026-11-10; issue 1118):
- every top-level window the instance's process creates, of any class and any owner, is cloaked or kept off-screen
  at creation and can never activate or take the foreground (an in-process creation hook, a watchdog as second line);
- the harness detects such a dialog, reads its text through window messages, reports a distinct "dialog" state
  with the text, and never clicks it by itself; answering it is a coordinator's decision;
- `shot` times out instead of hanging its caller (PrintWindow blocks while the window thread is not pumping).

All five are implemented and tested offline since 1115 (2026-11-10; `tools/harness/README.md`, "Stop safety";
`tests/test_safety_*.py`, including a syntax-tree scan of every `.py` under `tools/` for the override in cleanup paths).
[L] On Python 3.11 a file another handle holds with share mode 0 still answers `os.stat`, but `open(p, "rb")` raises
`PermissionError` (errno 13): that was the 1114 `CvAssert.log` failure; `crash.baseline` now records it as
`skipped: locked`. During a load the patched MainMenu re-executes and prints "requested mods are already active"
again, so that line alone does not mean an idle front end.

### 7.1 The activation veto failed on both routes; the session's hook-module table (issue 9009, 2026-11-10)

**What happened [L].** Each `%TEMP%\civbg_guard_<pid>.log` says, for each window thread, whether the WH_CBT
activation veto installed. Every guard log up to 08:04 says `ok`. From 14:28 on, every import-route stand-in of worker
2501's `test_shim.py` runs (14:28-14:33, 12 processes) and **every real `CivilizationV_DX11.exe` launch from 14:43 to
15:39 (9 processes, c5h-a, early route, injected focus_guard.dll)** say `activation veto (WH_CBT) on thread N:
FAILED (err 126)`. The cloak still worked (`win_cloaked_create` counted every box, the watchdog 0). With no veto,
win32k's own activation inside `MessageBox` / `DialogBox` went through, and the cloaked box became its thread's
active window. On the owner's desktop, at 14:28-14:33, it became the foreground window (`"foreground": true` in 2501's
results): an invisible window holding his keyboard. The early-route stand-ins of the same minutes still said `ok`.

**Root cause [L, mechanism I].** The guard called `SetWindowsHookExW(WH_CBT, cbtProc, <guard module>, tid)`. With a
module handle, win32k registers the module's path in a per-session table of hook modules [I: the NT source names it
`aatomSysLoaded`, 32 entries (`CLIBS`); one entry per distinct DLL path]. When the table is full, a path that is not
already in it fails with `ERROR_MOD_NOT_FOUND` (126) [L]. Windowless probe (`o9009/atomprobe.py`, a WH_CBT hook on the
probe's own thread, removed at once), 16:05 and again 16:30, no game running:

- hMod NULL: installed;
- hMod = python.exe's module: 126;
- hMod = a copy of a DLL at a new path: 126;
- the user atom table was not full (`RegisterWindowMessageW` of a new string -> 0xC2FC; `GlobalAddAtomW` -> 0xC06B).

So the route was never the cause. Only DLL paths that were new to the table failed: each import-route test
run uses a new wrapper path (`...\_test1105\<stamp>\import\<id>\install\probe_static.dll`), while the stand-ins' early
route used one `bin\focus_guard.dll` path that was registered earlier. The real EXE's focus_guard.dll path was new
to the table too.

**Why the table filled, and what else is affected [I].** It was not full at 08:04 and was full by 14:28; the 05:59
run had registered about 15 new wrapper paths. Probable fillers: every distinct guard DLL path in use (each worktree's
`native\bin\focus_guard.dll`, each test run's wrapper copy, each instance's wrapper), and other programs' global
hooks. Entries are released when no hook uses them any more [I]. Whether exited test processes leaked theirs could not
be measured without kernel tools, and nothing else was tried (no system setting was changed). On the probe evidence,
until the table frees up (at the latest when the owner signs out or restarts), **any program of the owner's session that sets a
hook with a DLL path new to the table fails too**: a newly started tool with a global keyboard or mouse hook, an IME
or accessibility helper [I; not observed]. A stable install path for the guard DLL would use a single entry. It would
still fail whenever that path's entry had been released and the table was full, so it is not a fix; hMod NULL does
not use the table at all.

**Fix (issue 9009, merged to main at 120294e).**

- `focus_guard.cpp` installs the veto with hMod NULL. Every hooked thread is the process' own, which is the
  documented case for NULL. `server/src/srv_guard.cpp` did this from the start.
- The guard self-tests every part (hooks, the veto, the cloak API, shared memory, watcher) and publishes
  `guard_state` / `guard_missing` (offsets 1276 / 1280) and `win_veto_ok` / `win_veto_failed` (1268 / 1272).
  Under the harness, a missing part ends the process before the EXE's entry point (`0xC5F00031`).
- `bg_helper32` gives the go only with `guard_state == 1` on every route (exit 4).
- `launch.py` checks again; an older guard or wrapper (state 0) is refused.
- `wrapper.py install` replaces an outdated wrapper.

`tools/harness/native/README.md` "Issue 9009" has the layout and the test results. The server's guard logs a failed
veto but has no counter and is not fail-closed; that went to epic 2700, which owns `server/`.

**The test that could not see it.** The README's 03:23 run had no `dialogs` test, because that test came with 1118.
`import/dialogs` passed at 05:59 because the veto installed then. The test could fail, but it measured only the owner's
foreground, which depends on the foreground-lock state. On a hidden desktop `GetForegroundWindow` returns NULL [L],
so a foreground check there can never fail. `test_shim.py` now does three things:

- runs every stand-in on a hidden desktop it creates;
- checks `win_veto_ok >= 2` and `win_veto_failed == 0`;
- checks that the box is not its thread's active window (`GetGUIThreadInfo`).

The pre-9009 build fails it (16:12: veto 0 and the box active, both boxes); the 9009 build passes (see the README
table). `fail_closed` forces the failure (`C5_GUARD_TEST_FAIL=veto|veto-report`): exit 4, no process, `main()` never
ran.

**Dialog dismiss (rule D29).** `cli.py -i <inst> dismiss --button LABEL --requested-by WHO` (`launch.dismiss_dialog`)
presses one of the dialog's own buttons. It acts only in the instance's recorded EXE or a recorded child, and only
after the settled-save check of `stop`, with no override. The press is a posted `WM_COMMAND` with the button's control
id; it never activates or shows the dialog. Every attempt is recorded in `run\evidence\dialog_dismiss.jsonl`. `stop`'s
`DialogOpen` message names the command. Tests: `tools/harness/tests/test_live_dismiss.py` (stand-ins on a hidden
desktop, 3 passed at 16:28). Not verified: a real EXE or server dialog dismissed by the command.
Exit codes (pinned 2026-11-13 by `tools/harness/tests/test_cli_exit_codes.py`, auditor cycle 3 C4): 0 only when the
dialog closed; 1 pressed but still open; 2 refused; 4 the settled-save check timed out; 6 needs a decision (silent Lua
channel + busy front end). The "exit 0" of 2026-11-11 22:04 (I-0575) was the caller's pipeline, `cli.py ... dismiss
... 2>&1 | tail -15`, whose status is `tail`'s: read `${PIPESTATUS[0]}` or do not pipe [C, the lead's transcript].

### 7.2 What the harness watch saw, and what it now sees (issue 9010, 2026-11-10)

The independent verification of epic 2500 (`server/evidence/2500/verification/`) found four gaps in the harness that
the offline suites did not notice. Each was shown on main at 3e3f5b9, fixed, and shown caught (one-off 9010,
stand-ins only, all on a hidden desktop).

**1. The watch checked only the EXE's main window for visibility [L].** Before 9010, `guard.watch` checked every
top-level window of the EXE's *children* but only the *main window* of the EXE itself. A visible dialog of the EXE was
not a hit. Live, 17:16, main's harness and binaries, stand-in on a hidden desktop: the stand-in's NULL-owner box was
made visible by switching its guard off, and a 1 s watch reported `seen_polls` 0 in 48 polls, `ok: true`. With 9010
the watch checks every top-level window of the EXE and of each child, on each 20 ms poll, for:

- visibility (`seen_polls`, `seen_hits` with hwnd, class, title, cloaked, main);
- cloak state (`uncloaked_polls`: visible and not cloaked anywhere; reported only, a hit with
  `require_cloaked=True`);
- foreground.

The same live case now fails the watch with the `#32770` box as a hit (`tools/harness/tests/test_live_watch.py`,
4 passed 17:15). Consequence for earlier evidence [I]: "0 visible polls" in the watch results of 1108-1116, 1109,
1110 and epic 2500 covers the main window and the children, not the EXE's other windows. The guard counters of
those runs (`win_cloaked_watchdog` 0, every box cloaked at creation) and the drivers' own pollers over every
top-level window (12.4, 12.5) are the evidence that the EXE's dialogs stayed cloaked.

**2. The offline suite did not notice the loss of the children [C].** The verification's mutations M6 (`ok` ignores
the children's counts) and M7 (the watch never scans for children) left 392 tests passing. The new
`tests/test_safety_watch.py` describes a desktop with test doubles and fails under M6, M7, M8 (the watch checks only the
main window), M9 (no start-up watch) and M10 (the guard log is not recorded). Mutation runs on copies, 2026-11-10:
all caught (`tools/harness/evidence/9010/mutations.txt`, script `mut9010.py` beside it).

**3. The start of the process was not watched [C].** The first watch slice began only after `launch()` returned, 14
to 20 s after process creation for the real EXE (2500 runs). `launch()` now runs the same poll on its own thread
(`guard.BackgroundWatch`, attached to the desktop `launch()` runs on) from the helper's return until `launch()`
returns. The result goes to the state file under `startup_watch`, with `from_s_after_create` and the guard's window
counters at the watch's start and end. Before the helper returns, the process' main thread is suspended (the shim
self-tests run first), so no EXE code can make a window then; `guard_windows_at_start.win_created` would show one made
by a DLL. A hit is logged as a `warning` and does not fail the launch. Live, 17:15: mode "offscreen" with the window
held on the monitor is reported (`seen_hits` main), and a cloak-mode start is clean.

**4. Guard logs were shared by pid [L].** `%TEMP%\civbg_guard_<pid>.log` was opened for append, so a reused pid
added a second process to an old log (verification file `exe-guard-logs-epic2500-runs.txt`). Since 9010 the EXE's
guard writes `civbg_guard_<pid>_<creation time UTC YYYYMMDD-HHMMSS-mmm>.log`. It publishes the path in shared memory
(offset 1284), and `launch()` records it in the state file (`guard_log`). Live, 17:15: one `FocusGuardInstall` line
per log, path recorded, found again from pid + creation time. The server's guard (`srv_guard.cpp`, owned by epic 2700)
still uses the old name.

**Follow-up, 2026-11-10 17:26-17:45: no live test on the owner's desktop.** `test_core_standin.py` and
`test_live_server_guard.py` started their stand-ins on the interactive desktop (2501's "hidden" case put only the
server there). Both now run on a hidden desktop. `tools/harness/tests/conftest.py` refuses any `bg_helper32 launch`
without a hidden `--desktop`, and any stand-in started directly, unless `C5_HARNESS_INTERACTIVE_DESKTOP` names the
MC's permission (`tools/harness/README.md`, "Hidden desktop only"). Measured on a hidden desktop [L]:

- `GetForegroundWindow` is NULL, so foreground checks run offline only.
- `PrintWindow` returns the right size but an all-white image (17:28), so screenshot content cannot be checked there.

**Incident [L], 2026-11-10 about 17:37.** A mutation run of the new refusal test, with `conftest.py` removed on
purpose, started the real `stand_in.exe` directly on the owner's desktop for about 1 s. The test meant to show that the gate
refuses this. The stand-in ran with no guard, at its defaults: `SW_SHOWNORMAL` at 100,100, 800x600, and
`--start-steal 1`, so it may have taken the foreground. Its log went into the scratch copy, which was deleted, so
whether it took the foreground is not known. The test now uses only fake binaries (not executable images). The
mutation run was repeated safely, with the binaries removed from the copy. Lesson: a test of a safety gate must not
be able to do the harmful thing when the gate is absent.

**Also from the verification (tools/instances, 9010).** A `vpproxy.log` the proxy writes into the mod folder with
`mode=off` survived `set-proxy --original`, and nothing reported it. `verify` now flags every file in the DLL's mod
folder that no manifest and no proxy record lists. `--original` moves `vpproxy*.log` out of the mod folder to
`--runs-to` or `<inst>\run\proxy\mod-folder\<stamp>\`, and refuses on any other unlisted file. New tests catch the
verification's mutations P3 (a replaced proxy DLL) and P4 (a changed kept DLL). `tools/instances/README.md` has the
details.

### 7.3 The patch's D118 popup is another process: the guard follows 32-bit children (issue 0473, 2026-11-17)

**What happened [L].** The patch's error popup (D118) was visible on the owner's desktop on 2026-11-17 at 01:23-01:38 (epic
2600's modpack test on c5h-mpk, #3397/#3419) and at 01:24-01:25 and 01:40 (epic 0437's hidden S4 runs, #3453). The
popup is `vp64helper.exe`, a separate process [C, `proxy/src/proxy_session.cpp` `StartHelper`]: the proxy in the EXE
starts it with `CreateProcessW`, `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB`, the
inherited environment and the proxy's folder as working folder. The helper makes a `WS_EX_TOPMOST` window of class
`VP64HelperPopup` and calls `ShowWindow(SW_SHOWNORMAL)`, `SetForegroundWindow` and `SetFocus(Close)`
[C, `proxy/helper/vp64helper.cpp` `ShowWindowed`]. The focus guard lived only in the EXE, so nothing hid it. The x64
server starts the same helper for an EXE crash or a DLL-fatal report [C, `server/src/remote/srv_exe_crash.cpp`
`StartHelper`]; that route was not covered by 0473 (ticket 0475, MC 2026-11-17 02:07). **Covered since 0475: section
7.3.1** (corrected 2026-11-17; `tools/harness/native/README.md`, "Children", still says "Not covered ... ticket 0475").

**Fix [C].** With `C5_CHILD_GUARD=<focus_guard.dll>` (set by `winapi.helper_launch` on both launch routes), the EXE's
guard hooks `kernelbase!CreateProcessInternalW` and gives every 32-bit child the guard before its first instruction,
by a remote `LoadLibraryW` into the suspended child, after writing the child's shared section in "child mode" (cloak
on, no c5 shim, no frame cap, no launcher handshake). The child runs only when its guard reports `guard_state` 1;
otherwise it is terminated and `CreateProcess` fails with 1114 (fail closed). x64 children (the server) pass through
unchanged. Layout, counters (offsets 1572-1596) and rules: `tools/harness/native/README.md`, "Children". The popup
stays a real window (cloaked), so dismiss can find it and press its own Close button (ticket 0472).

**Hermetic test [L, 2026-11-17 02:16-02:18, workstation, hidden desktop].** `tools/harness/tests/test_focus_guard_children.py`,
5 passed twice (63 s): a stand-in EXE (early route) starts a second stand-in 2 s in, exactly as the proxy starts the
helper (`--spawn-helper`, out of the job), which opens the helper's popup 1.5 s later (`--popup-after`: same class,
styles and calls). Poll every 20 ms over the hidden desktop's windows:

| Case | Result |
|---|---|
| guarded | child guarded (`child_seen` 1, `child_guarded` 1, child's `guard_state` 1, `child_parent` = the stand-in); popup on 289 of 482 polls, **0 polls visible and uncloaked**; its "Close" button found by enumeration; child's guard: cloaked at creation 1, watchdog 0, `SetForegroundWindow` 1 call 1 refused, `ShowWindow` rewritten, 1 activation vetoed; the popup's own log: `visible=1 cloaked=1 active_is_me=0` every second |
| teeth: `C5_CHILD_GUARD` removed | popup visible and uncloaked on 286 of 286 polls, `active_is_me=1`, plus visible `UAC_InputIndicatorOverlayWnd` windows of the child (the check can fail) |
| fail closed: `C5_CHILD_TEST_FAIL=inject` | `SPAWN_HELPER FAILED error=1114`, `child_failed` 1, no child, no popup |
| x64 child (`Sysnative\cmd.exe /c exit 0`) | started, `child_other` 1, not guarded |
| `C5_CHILD_GUARD` naming no file | the stand-in's guard fails its self-test: `TERMINATED ... exit code 0xc5f00031`, no process |

On a hidden desktop `GetForegroundWindow` is NULL, so foreground is shown by the guard's counters and the popup's
`active_is_me`, not by a foreground poll.

**Live run [L, 2026-11-17 02:45-02:56, workstation, c5h-f, interactive desktop].** The proxy layout of
`C:/c5-64bit-work/0450/layout` (proxy 6D2FEEEA, built `dirty=1`; x64 DLL 29F58E17; helper 071A4B77) installed with
`provision.py set-proxy` and a `vpproxy.ini` with `pack=1` (the default keys plus `pack=1`); `vp64helper.exe` copied
beside the proxy; no `manifest.json` in `VP64\`, so the pack check fails (`no-pack`) in a stock session. The instance's
import wrapper was rebuilt from this branch for the run (`wrapper.py install`) and put back by hash afterwards.
`cli.py launch` with the front end set to load `C5H-0367-TINY_0008`; at the load (02:49:49.18) the proxy fell back to the
32-bit DLL, wrote the report and started `vp64helper popup` (pid 28960), which the EXE's guard injected and resumed
(`child pid 28960 ... guarded (guard_state 1)`, 88 ms from the report written to the helper started, injection included).
In the helper: its popup cloaked at creation (`cloaked ... at creation, class VP64HelperPopup`), `ShowWindow(1)` made
`SW_SHOWNOACTIVATE` (4), `SetForegroundWindow` blocked, activation vetoed; watchdog 0. A read-only watcher listed every
top-level window every 100 ms from before the first launch to after the close: **4,831 polls, 0 with a window of the
instance's processes visible and uncloaked, 0 with the foreground in them; the popup on 2,900 polls (355 s, on the
monitor at 942,520-1618,919), `visible=1 cloaked=1` on every one.** The game went on to the map on the 32-bit DLL and was
stopped through `cli.py stop` (exit 7: the helper an orphan, left running). With the EXE gone, the popup was closed by
one `WM_COMMAND` to its own Close button (id 102), matched by hwnd, class, pid, creation time and image path under the
instance, under the MC's one-off approval of 02:26 (#3513). The branch did not carry 0472, which had reached main at
02:31 without my noticing; run 2 below repeats the close with 0472's rule. The helper ended 0.2 s later. This branch's
dialog watcher, from before 0472, did not see the popup. Then
`frontend.py restore`, the report and `vp64helper.exe` removed, `set-proxy --original`, the wrapper restored by hash,
`verify` ok, `leftovers` clean. Evidence: `evidence/0473/live/` (the watcher and close scripts, their output, the guard
logs, `vpproxy.log`, the report); hermetic results `evidence/0473/hermetic/`.

**Live run 2, closed by 0472's dismiss [L, 2026-11-17 03:02-03:06, c5h-f, branch merged with main e2bf1ac1].** 0472 had
reached main at 02:31, before run 1's close (02:55); run 1 used the one-off message because the branch did not carry
it. Run 2 repeats run 1 on the merged state. Helper pid 42824 guarded at 03:04:17.9. 0472's watcher reported it as the
instance's `patch` box at 03:04:32 (marker, queue note to the claim's address), and `frontend.py ingame` stopped with exit
8 on it. Closed with `cli.py -i c5h-f dismiss --button Close --requested-by ... --coordinator-override "MC 02:26 #3513 ..."`
with the game alive: exit 0, save settled 10.4 s, `button_id` 102, record `cloaked: true, seen_by_art: false,
foreground: false`; the helper ended. Then the load screen was closed, `stop` exited 0 with no orphans, and the
instance was restored as in run 1. Watcher from 03:02:56 to 03:06:25: **1,762 polls, 0 seen by the owner, 0 foreground; the
popup on 487 polls, cloaked on every one** (one poll caught it created and not yet shown). This is also 0472's first
live check: dismiss and the watcher find the cloaked popup of a guarded helper. Evidence `evidence/0473/live2/`. Not measured: Windows 10's WOW64 kernelbase prologue of
`CreateProcessInternalW` (oldpc unreachable 2026-11-17 02:10); a different shape fails the self-test there (bit 0x40),
which ends every harness launch on that machine until `hookAt` knows the shape [I]. **Measured since (ticket 0475,
2026-11-17 04:53) [C]:** on oldpc (Windows 10 19045) the WOW64 `C:\Windows\SysWOW64\kernelbase.dll`
`CreateProcessInternalW` (RVA 0x100210) starts `68 c0 0c 00 00 68 c8 86 1e 10 e8 5d` (push imm32; push imm32; call), not
`8B FF 55 8B EC`, so 0473's hook does not go in there and a guard with `C5_CHILD_GUARD` set fails its self-test (bit
0x40). This workstation (26100): `8b ff 55 8b ec 6a fe 68` at RVA 0x161820. Read from the files on disk with
`pe_export_bytes.py` (a 40-line PE export reader, scratch, not committed). Reported to the MC (queue note on 0475).

### 7.3.1 The server's own helper start: srv_guard follows its 32-bit children too (ticket 0475, 2026-11-17)

**The gap [C].** The x64 server starts `vp64helper.exe popup --report <dir> --options <vpproxy.ini>` itself for the
EXE-crash report (ticket 0418) and the DLL-fatal report (ticket 0422): `server/src/remote/srv_exe_crash.cpp`
`StartHelper`, `CreateProcessW` with `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB` (again
without the breakaway on `ERROR_ACCESS_DENIED`), the server's environment, the proxy's folder as working folder. The
helper is 32-bit. 0473's hook lives in the EXE's 32-bit guard and sees only the EXE's children; the server's guard
(`server/src/srv_guard.cpp`, issue 2501) covered the server's own windows only.

**Fix [C].** `srv_guard.cpp` gets the same child hook, x64:
- **When.** Only with `CIVBG_GUARD=1` (the guard is installed at all) and `C5_CHILD_GUARD=<absolute path of the 32-bit
  focus_guard.dll>`. Both reach the server through the EXE's environment (the proxy's `CreateProcessA` passes it on;
  `winapi.helper_launch` sets `C5_CHILD_GUARD`). Without them nothing changes: the product (no `CIVBG_GUARD`) and a
  harness without 0473 run as before.
- **The hook.** The x64 `kernelbase!CreateProcessInternalW`, which every `CreateProcessA/W` path reaches. Its first 12
  bytes are seven pushes, `40 53 56 57 41 54 41 55 41 56 41 57`, on this workstation (Windows 11 26100, RVA 0x7d6e0)
  and on oldpc (Windows 10 19045, RVA 0x41570) alike [C, from the live process here and the file on disk on both,
  2026-11-17]. The hook checks those bytes exactly, copies them into a trampoline that jumps back with
  `jmp qword ptr [rip]`, and writes `mov rax, imm64; jmp rax` over them (`HookPrologueAt`). Any other shape is not
  patched.
- **The injection.** A 64-bit process cannot start the 32-bit `LoadLibraryW` in a 32-bit child itself (the WOW64
  kernel32 is not mapped in the server). So the server runs the harness' 32-bit helper, `bg_helper32.exe inject <pid>
  <dll>`, which must sit beside the guard DLL (`build.bat` puts both in `tools/harness/native/bin`). The helper is
  started from the same thread with a thread-local flag set, so the hook lets it through unguarded (it has no window).
- **Per child.** Created suspended, counted in `child_seen`. Not 32-bit: `child_other`, resumed unchanged (no 64-bit
  child of the server is known). 32-bit: the server writes the child's section `Local\CivBgGuard_<pid>` as 0473's
  parent does (defaults, `cloak` 1, `child_parent` = the server's pid, so the child's guard runs in child mode), runs
  `bg_helper32 inject` (wait 30 s; bg_helper32 itself waits 15 s for its remote thread), and reads the child's
  `guard_state` back. Only exit 0, the child alive, the section's pid = the child and `guard_state` 1 resume it
  (`child_guarded`, `child_last_pid`). Otherwise the child is terminated with 0xC5F00051 before its first instruction
  and `CreateProcess` fails with `ERROR_DLL_INIT_FAILED` (1114): `StartHelper` logs "could not be started:
  CreateProcessW error 1114", the report stays (`child_failed`; fail closed). A caller's own `CREATE_SUSPENDED` is kept.
- **Install failures.** `C5_CHILD_GUARD` not an absolute file path, no `bg_helper32.exe` beside it, or an unknown
  prologue: `child_guard` -1, `guard_missing` bit 0x40, and the self-test fails, so the server exits 0xC5F00032 before
  any DLL (the proxy then sees a server that ended before it attached, as in test_proxy_rt's `guard-veto` [I]). The
  status line names the part:
  `children:C5_CHILD_GUARD-not-a-file`, `children:no-bg_helper32`, `CreateProcessInternalW:shape(..)`.
- **Shared memory.** The server's section now carries 0473's fields at the same offsets (1572 `child_guard` ... 1596
  `child_last_pid`; 1544-1568, the EXE's frame cap, stay 0), so `winapi.Guard(pid).children()` reads the server like
  the EXE. The server log's guard line (`SrvGuardDescribe`) ends with `children: guard G, seen S, guarded N, other O,
  failed F, last pid P`.
- **Test switch.** `C5_CHILD_TEST_FAIL=inject`, as in 0473, fails every injection (tests only).

**Hermetic test [L, 2026-11-17 04:48-04:51, workstation, hidden desktop].** `python -I server/tests/guard/
test_server_child_guard.py --out DIR [--asan]` (through `quietlock.py run`). `child_guard_unit.cpp` links the real
`srv_guard.cpp`, installs the guard as `srv_main.cpp` does and starts a child exactly as `StartHelper` does; the child is
0473's `stand_in.exe --popup-after` (the helper's popup: class, styles, `ShowWindow(SW_SHOWNORMAL)`,
`SetForegroundWindow`). A poller on the hidden desktop lists every top-level window every 20 ms.

| Case | Result |
|---|---|
| guarded | `child_guard` 1, seen 1, guarded 1, failed 0, last pid = the child; the child's `guard_state` 1, `child_parent` = the unit, `cloak` 1; popup found with its Close button, on at least 50 polls, **0 visible and uncloaked**; child's guard: cloaked at creation, foreground request refused, watchdog 0; every POPUP line `cloaked=1 active_is_me=0` |
| teeth: no `C5_CHILD_GUARD` | the guard installs (`child_guard` 0); the popup is visible and uncloaked (hidden desktop): the check can fail |
| product: no `CIVBG_GUARD` | nothing installed ("not requested"), the child starts with no guard |
| fail closed: `C5_CHILD_TEST_FAIL=inject` | `SPAWN FAILED error=1114`, `child_failed` 1, the child's log empty (it never ran), no popup |
| x64 child (the unit `noop`) | started, `child_other` 1 |
| `C5_CHILD_GUARD` not a file | exit 0xC5F00032, missing 0x40, nothing started |
| no `bg_helper32.exe` beside the DLL | exit 0xC5F00032, missing 0x40, nothing started |

Result: **23/23** (clang-cl x64, /W3 /WX, 50 s); with `--asan` **45/45** (the same plus guarded, fail closed and x64 on
an MSVC `/fsanitize=address` build, the canary caught, no AddressSanitizer report; 76 s).

**Regression suites [L, 2026-11-17 04:56-04:59, workstation].** On the build below: `proxy/tools/test_proxy_rt.py
--build B` **107/107** (its guard section builds the fake server with the real `srv_guard.cpp` and starts the real
`server.exe`); `tools/harness/native/tests/test_guard_shapes.py` (includes `srv_guard.cpp`) 43 checks, 0 failures,
plain and `--asan`. `tools/harness/tests/test_safety_static.py` on the state merged with main (05:06): 55 passed.

**Live run [L, 2026-11-17 05:00:19-05:04:26, workstation, c5h-a, interactive desktop].** Build
`C:/c5-64bit-work/0475/b`: `server/build_server.py --src <git archive of the pin 04b1211b8> --deps <a copy of a release
build_all of the pin> --config release --offline` from this branch (OK in 169 s; server.exe 72E97DF2...). The pin has no
0421 guard, so 0422's probe stops the DLL. Driver `evidence/0475/live_0475.py`: 0422's prep (backup by SHA-256, the
pack=1 test layout of the build, `set-proxy`), then the REAL `vp64helper.exe` of the build beside the proxy (0422 used a
fake); the instance's import wrapper was renewed with `wrapper.py install` first (steam_api.dll fce6ddbc... ->
d6bb5979..., kept: it is what main's harness requires). Save `C5H-0167-TINY_0008` (the bytes of HI4510-TINY_0008,
b2ceb663), loaded read only, unchanged after. 0473's `watch_visible.py` listed every top-level window of the
interactive desktop every 100 ms from before the launch to after the close.
- The EXE's guard: self-test ok with `children 1` (0473); the server's guard line in `server.log`: `guard: installed ...
  children: guard 1`; its guard log: `children: CreateProcessInternalW hooked`, `CreateProcessInternalW:ok`.
- The stop at the server's 58.864 s: `remote: LOUD: dll-fatal: ... eImprovementNum > -1 (CvGlobals.cpp:4279); Lua call:
  Plot.IsResourceConnectedByImprovement(a Plot object, -1) on the core channel`; report written; at 59.296 `dll-fatal:
  helper: started ... vp64helper.exe popup --report ...`, pid 50944.
- The server's guard log: `child pid 50944 (...vp64helper.exe): guarded (guard_state 1 (bg_helper32 pid 50336), log ...)`.
  The helper's guard log: self-test ok, **CHILD mode (parent pid 40956 = the server)**; `cloaked ... at creation, class
  VP64HelperPopup, title "Vox Populi 64-bit patch: error report"`; `blocked SetForegroundWindow`; `vetoed activation
  (HCBT_ACTIVATE)`.
- The watcher: **1,658 polls, 0 with a window of the instance's processes visible and uncloaked, 0 with the foreground
  in them; the popup on 687 polls, `visible=1 cloaked=1` on 686 and `visible=0 cloaked=1` on one** (created, not yet
  shown).
- The EXE ended by itself (the proxy's on_server_death=exit); the dialog watcher reported the popup as the instance's
  `patch` box (D155 queue note to the claim's address). The popup was closed by the claim's owner's rule for an ended
  game (0479): `cli.py -i c5h-a dismiss --button Close --patch-popup`, exit 0, `via: recorded child`, `cloaked: true,
  seen_by_art: false`; the helper ended; the marker cleared. arm.py's own stop had said exit 7 (the helper an orphan),
  as in 0473's run 1.
- Restore: `set-proxy --original`, `frontend restore`, the backup restored by hash, `verify --quick` all rc 0;
  `leftovers` clean; claim released. Evidence `evidence/0475/` (`live/`, `hermetic/`, `regress/`; `collect.py` scrubs).

**Not verified.** A server-started helper for an EXE crash (stage `exe-crash`, ticket 0418) live: the same
`StartHelper` and the same hook, covered only by the hermetic test. The Windows 10 testbed live: the x64 prologue is the
same there [C], but the EXE's side fails there (0486; since 0486 the harness does not ask for the child hook off the
workstation, section 7.3.2). A 64-bit child of the server: none exists; it would pass unguarded (`child_other`).

### 7.3.2 The child hook only on the workstation; the WOW64 prologues of both Windows builds (ticket 0486, 2026-11-17)

**The prologues [C, from the files on disk, 2026-11-17, ticket 0475].** First bytes of the 32-bit (WOW64)
`C:\Windows\SysWOW64\kernelbase.dll` export `CreateProcessInternalW`, which 0473's hook (`hookAt` in `focus_guard.cpp`)
patches in the EXE:

| Machine | Windows build | RVA | First bytes | 0473's hook |
|---|---|---|---|---|
| workstation | 11, 10.0.26100 | 0x161820 | `8b ff 55 8b ec 6a fe 68` (`mov edi,edi; push ebp; mov ebp,esp`) | installs |
| oldpc | 10, 10.0.19045 | 0x100210 | `68 c0 0c 00 00 68 c8 86 1e 10 e8 5d` (`push 0xcc0; push imm32; call`, an SEH-prolog helper) | does not: `child_guard` -1, `guard_missing` bit 0x40, self-test fails, the launch is refused |
| laptop | 10, 10.0.19045 | not read [I: same build as oldpc, so the same bytes are expected] | | |

The x64 `CreateProcessInternalW` (0475's server hook) is the same on 26100 and 19045 (section 7.3.1).

**The fix: the hook is not asked for off the workstation [C].** The ticket offered two ways: teach the hook the
Windows 10 shape (a trampoline that re-issues both pushes and turns the relative `call` into an absolute one), or not
set `C5_CHILD_GUARD` where nothing needs hiding. The second was taken: D63 says nothing is hidden on the test machines
(COMMON rule 24), so following children there buys nothing, and it needs no native rebuild on the instances.
`tools/harness/winapi.py`: `child_guard_wanted()` is true on the workstation (the SHA-256 of a lower-cased host name in
`WORKSTATION_HOST_SHA256`, the same constant as `tools/hi/realinput.py` and `tools/testbed/jobs/steam_launch.py`) and when
no host name can be read (fail toward hiding); `helper_launch` then sets `C5_CHILD_GUARD=<focus_guard.dll>` as before
0486. On any other machine it removes the variable, an inherited one too, unless the caller names it in `env` (the
hook's own live test, `test_focus_guard_children.py`, now names it itself, so it tests the hook wherever it runs and
fails closed on Windows 10, as it should). Because the harness's environment reaches the EXE and from it the server,
the server's child hook (7.3.1) is also off on the test machines (`child_guard` 0 in both).

What this leaves: on a test machine the patch's D118 popup (vp64helper.exe) is an ordinary visible window. Nobody works
there (D63), the dialog watcher and `dismiss` find it by process (section 17.4), not by the guard. A renamed
workstation would no longer match the hash and its children would go unguarded; the real-input fence of
`tools/hi/realinput.py` has the same dependency (in its case the refusal would fail open), so a rename must update all
three constants together (`tools/harness/tests/test_child_guard_where.py` checks they agree). If a Windows 10 machine
ever needs hiding, the hook must learn the second shape first.

**Tests [C, offline, 2026-11-17].** `tools/harness/tests/test_child_guard_where.py` (9 tests, a made-up workstation
name whose hash replaces the constant; no machine name in the file): the constant equals both fences'; on the
workstation the variable is set on both routes; off it, it is absent on both routes and an inherited one (any case) is
removed; an explicit value is kept; `None` still leaves children unguarded on the workstation; no host name sets it.
With the launch-route, core-launch, children and fence suites: 179 passed, 5 skipped (the live ones), under
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`.

**Live on oldpc [L, 2026-11-17].** Job `20261117-054743-a110`: `tools/testbed/jobs/headed_probe.py --instance tb2
--phase frontend --mode offscreen --dwell 45`, branch `task-0486-child-guard-win10`, natives built from it (the 0473
child hook inside: `focus_guard.dll` 8ab46825..., `bg_helper32.exe` 6660d8e5...). Expected: `launch` rc 0, the
game running at the menu, the guard's `child_guard` 0 ("not requested") in the collected guard log, `stop` rc 0.
**Result: not yet run.** At the hand-back (2026-11-17 06:35) the job was still queued behind 0470's quiet series
(`20261117-041041-c3b6`, rotation R4 of 18 at 06:30, about 30 min each); by the MC's decision the ticket was handed
back on the offline proof and the MC reads this job's result when it runs (if it fails, 0486 reopens with its log).
Whoever reads the result replaces this paragraph with it.

## 8. Other shell-gates tools (`research/sources/shell-gates/tools/`)

| Tool | Usage | What |
|---|---|---|
| `addrspace.py` | `python addrspace.py <pid\|name> [--json]`; `--csv out.csv --every 5 [--for 600]` | read-only (`OpenProcess` query + VM read, `GetProcessMemoryInfo`, `VirtualQueryEx`; no attach, no injection): private, WS, peak WS, committed / reserved / free, free below 2 GB, largest free block and its address, free blocks >= 1 / 16 MB, wow64; CSV rows until the process exits |
| `gate1_checklist.py` | `python gate1_checklist.py --out results.json [--skip-endturn] [--turns 1] [--turn-timeout 600]` | Gate 1 functional checklist through the Lua channel: status, city screen open/up/closed, unit select + LookAt, minimap / unit-flag / city-banner states alive, hex picking by posted `WM_MOUSEMOVE` (`UI.GetMouseOverHex` must follow), then N end-turns via `end_turn.py` + `wait_turn.py --answer 8 --stall 45`. Needs the channel on (`game_session.py luaexec on`) and the civ5-game-ui scripts (repo `.claude/skills` or `~/.claude/skills`) |
| `disable_systems.py` | `backup`, `set B4` (or `A4`, `A5`, `FluidFOW`, `none`, or explicit keys), `restore`, `show` | sets `[DisableSystems]` keys in `USER_DIR\config.ini` for one run, restores with SHA-256 verification; backup in `%USERPROFILE%\Documents\shell-gates-backup` |
| `scan_exec.py`, `ab_live.py` | see `05-graphics-composition/shared-texture-and-compositor.md` | compositor insertion scan and live A/B |
| `view_mask.py` | `python view_mask.py show`; `python view_mask.py clear 0x400100 [--wait 300]` | reads or clears bits of the EXE's per-view active-systems mask `[[*(0xCC8E74)]+0xC0]+0x1B4` in the running game (River 0x100, Arrows 0x400000); a heap data write via `WriteProcessMemory`, no code bytes; DX11 EXE 0x546CD0A8 only, rebased on the module base. The out-of-process twin of the one-line DLL stub (STUBS.md) |

Sources: `research/sources/skills/vp-dll-dev-SKILL.md`, `civ5-game-ui-SKILL.md`,
`research/sources/memory-notes/civ5-vp-dll-dev-skill.md`, `civ5-game-ui-skill.md`, `civ5-modpack-save-rules.md`,
`civ5-exe-message-queue-overflow.md` (direct DX9/Tablet launch, branch without channel),
`headless-dll-ab-test.md` (first line), `research/sources/shell-gates/GATES.md` (top recipe, Gate 1 checklist,
"Running experiments without stealing the owner's focus"), `nofocus-README.md`, `tools/civ_bg.py`, `end_turn.py`,
`wait_turn.py`, `gate1_checklist.py`, `addrspace.py`, `disable_systems.py`, `view_mask.py`.

## 9. Crash evidence and where it lands (2026-11-10, epic 1100 / issue 1104)

Read while building `tools/harness/crash.py`; nothing was crashed on purpose.

| Evidence | Where | Notes |
|---|---|---|
| `crashes.log`, `CvMiniDump_*.dmp`, `CvAssert.log`, instrument logs | `<install>\crashlogs\` and the install root, i.e. the EXE's current directory, which the EXE sets to its own folder (`SetCurrentDirectoryA` at 0x0081B7A3) [C, 1102] | per instance when each instance runs from its own install copy |
| WER local dumps | `%LOCALAPPDATA%\CrashDumps\CivilizationV_DX11.exe.<pid>.dmp`, full dumps of 138-145 MB [L] | the PID in the name is the only reliable link to one instance; not redirected by the shim (WER writes them from outside the process) |
| WER reports | `C:\ProgramData\Microsoft\Windows\WER\ReportArchive\AppCrash_CivilizationV_DX_*` [L] | folder names can be listed; contents are access-denied without elevation; not attributable to an instance |
| gbe crash printer | `crash_printer_location` in the install's `steam_settings\configs.main.ini`, relative to `steam_api.dll` [C] | pure_vp still has the placeholder `./path/relative/to/dll/crashes.txt`. On the c5h instances (`<inst>\run\gbe\crashes.txt`) it is **the only record of an EXE crash with its address**: one entry per unhandled exception, `[yyyy/mm/dd - hh:mm:ss]` in **UTC**, the code and `@address` = the faulting EIP as an absolute address. The EXE has `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE` (DllCharacteristics 0x8140 [C]; dumps show it at varying bases, `02-exe-internals/addresses.md` conventions), and nothing records its base for a past run (the proxy and guard logs print `CivilizationV_DX11.exe+RVA` only), so the address cannot be mapped afterwards: log the EXE base at boot if crashes are to be mapped. The c5h-a crash of 2026-11-10 18:06:12 EST (issue 2708) left no Application event 1000/1001, no WER folder, no `crashlogs` dump and no `CrashDumps` file; only this line (23:06:12, 0xC0000005, 0x00A07F3B) [L, 2026-11-10] |

Also [L]: for a 32-bit process that is not large-address-aware, `VirtualQueryEx` shows the upper 2 GB of the 4 GB
WOW64 window as reserved. Do not count it as the game's reservation.

## 10. The harness's native shim and `--early` injection (2026-11-10, epic 1100 / issue 1105)

Code: `tools/harness/native/` (focus guard lifted from shell-gates nofocus, plus the instance shim); details,
shared-memory layout and exit codes in its `README.md`. Measured on the stand-in only; the real CEG EXE under
`--early` is checked live in 1108.

**Windows 10 (one-off 9026, 2026-11-11)** [C, L]: the guard was matched to Windows 11 26100 and refused every start on
Windows 10 19045 (the quiet testbed): `GetActiveWindow:unknown(6a 01 ff)`, `NtUserSetForegroundWindow:missing`. It
now also knows Windows 10's `GetActiveWindow` (`push 1; call [NtUserGetThreadState]; ret`) and, where win32u lacks
`NtUserSetForegroundWindow`, hooks `NtUserCallHwndLock` for exactly the index user32's `SetForegroundWindow` passes
(0x70), read from user32's code; the server's x64 guard got the same fallback (before it, the patched game's server
would have exited 0xC5F00030 under the harness on Windows 10). Self-test strict as before; Windows 11 unchanged
(stand-in suite 10/10, real EXE on c5h-d ok). Details: `tools/harness/native/README.md` ("Windows 10" under issue 1118
and under the server's guard). A game started from a session without a Steam client (the testbed's session 0) ends by
itself after the EXE's 30-s DRM handshake, before any window: `quiet-testbed.md` 11.1.

- [L] The old remote-thread start (`bg_helper32 --suspended` + `CreateRemoteThread(LoadLibraryW)`) is **too
  late for static imports**: the remote thread runs process initialisation first, so a static import's DllMain
  and the EXE's TLS callbacks see the real Documents folder. gbe's `steam_api.dll` is a static import of the EXE.
- [L] `bg_helper32 launch --early` loads the guard at the loader's debugger-break point instead: it sets
  `PEB.BeingDebugged` and places a one-shot 5-byte hook in the child's own 32-bit `ntdll!NtQueryInformationThread`;
  `LdrpDoDebuggerBreak` then calls it and the guard loads before any import's DllMain or TLS callback, while the
  main thread stays suspended until the guard's self-tests pass. `BeingDebugged` is 0 again before any DllMain
  or TLS callback runs; `NtGlobalFlag` stays 0 (`_NO_DEBUG_HEAP=1`). Nothing is written to the EXE image.
- [C] SysWOW64 ntdll on Windows 11 26100 (base 0x4B280000): `LdrpDoDebuggerBreak` at 0x4B39917D, called at
  0x4B2B261C and 0x4B34AF96 behind `cmp byte [peb+2],0`; it calls `NtQueryInformationThread(-2, 0x11, &b, 1, 0)`
  and executes `int 3` only if that succeeds with b == 0.
- [L] Shell32's folder exports on 26100 x86 are 12-byte stubs (`mov edi,edi; push ebp; mov ebp,esp; pop ebp;
  jmp [IAT]`) into windows.storage; `FindWindowA/W` are separate stubs into an internal function (not via
  `FindWindowExA/W`); kernel32 `CreateMutexA/W/ExA` and `OpenMutexW` are `jmp [kernelbase]`, `OpenMutexA` is
  kernel32-only.
- Redirection: `C5_DOCUMENTS` / `C5_LOCALAPPDATA` answer CSIDL 5 / 0x1C and FOLDERID_Documents / LocalAppData
  through every shell32 folder API; every other folder request is logged. `C5_ISOLATE_SINGLE_INSTANCE=<id>`
  renames exactly `Local\Civ5App` to `Local\Civ5App_<id>`; every other name passes through, and the CEG handshake
  objects are never touched (CONTRACT section 7). Paths longer than 140 characters are refused (the EXE appends
  into MAX_PATH buffers).
- Fail closed: the guard self-tests in the still-suspended process (`redirect_state`, `isolate_state`; the
  isolate test creates no object and refuses if `Local\Civ5App_<id>` already exists) and terminates its own
  process on failure; `bg_helper32` refuses to resume, refuses Steam-install and pure_vp EXE paths, and refuses
  a `C5_*` launch without `--early`. Stand-in test (2026-11-10 02:53): two instances at once each saw only their
  own folders (153 and 126 answers), both passed the renamed gate, no focus taken in 2,296 polls at 20 ms;
  12 broken configurations each left no process.

**Import-time guard loading (issue 1113, 2026-11-10).** Default route for harness launches since 1114 (2026-11-10, live on the real CEG EXE, section 12.1): `launch.launch(route=None)` picks it whenever `install\c5_wrapper.json` exists. *Corrected 2026-11-10; replaced "Proposed default route ... `launch.py` still uses `--early`".* `--early` (1105) is
the fallback. The instance's `install\` copy gets a wrapper DLL under the name of a DLL the EXE imports statically
from its own folder (`steam_api.dll`; the real gbe DLL becomes `steam_api_gbe.dll`). Its DllMain, on the main
thread during process initialisation, installs the focus guard and the c5 shim (only when `C5_IMPORT_GUARD=1`,
set by `bg_helper32 --import-time`), self-tests, holds the loader in a handshake until the launcher has read
`redirect_state` / `isolate_state` and given the go, then loads the real DLL from its own folder and fills one
jump slot per export. Under the harness every failure ends the process with TerminateProcess (never FALSE from
DllMain, which would make the loader raise a STATUS_DLL_INIT_FAILED hard-error box). No injection, no PEB write,
no ntdll patch, nothing written to the EXE image.
- [C] DX11 EXE (pure_vp copy, `dumpbin`): no TLS directory, no delay imports; import descriptors in order WS2_32,
  PSAPI, WINMM, d3dx9_42, WINHTTP, zlib1, IMM32, **steam_api (8th)**, lua51_Win32, MSIMG32, gdiplus, KERNEL32,
  USER32, GDI32, COMDLG32, ADVAPI32, SHELL32, ole32, OLEAUT32, MSVCP90, VERSION, MSVCR90, SHLWAPI, dbghelp, mss32,
  d3d9 (26th), d3d11, d3dx11_42, dxgi. App-folder DLLs among them: zlib1, steam_api, lua51_Win32, dbghelp, mss32,
  d3d9 (proxy). The d3d9 proxy would load the guard after gbe's DllMain: steam_api is the right wrapper.
- [L, stand-in] Sibling static imports run DllMain in import-descriptor order: a probe DLL listed before the wrapper
  saw the real Documents; the wrapped DLL's DllMain, the EXE's TLS callback and main() saw the redirect. With pure
  loader forwarders instead of stubs, the forwarder target was mapped at snap time but its DllMain still ran after
  the wrapper's (not documented behaviour; stubs are the default for that reason).
- [C] What initialises before the guard on the real EXE: descriptors 0-6 and their closures. The only non-system
  one, zlib1.dll, imports only MSVCRT and KERNEL32; d3dx9_42 imports RegQueryValueExA and no shell folder API;
  [I] the Windows components (WS2_32, PSAPI, WINMM, WINHTTP, IMM32 and dependencies) do not read the user's
  Documents in DllMain. No later app-folder import uses a shell folder API either (lua51 getenv, Mss32 environment
  strings, dbghelp `_wgetenv`/RegQueryValueExA, d3d9 proxy GetEnvironmentVariableW). gbe's steam_api.dll imports
  SHGetFolderPathW and SHGetKnownFolderPath and has a TLS directory: with the wrapper it initialises after the guard.
- [L] Timing table (stand-in, run 20261110-032347): `--suspended`: static DllMain and EXE TLS see the real
  Documents; `--early`: everything redirected, including a sibling imported first; `--import-time`: everything from
  the wrapped DLL on redirected, a sibling before it not. No debugger flags in any route.
- [L] link.exe rejects export forwarders in a .def (`a=dll.a`, LNK2001); `#pragma comment(linker,
  "/export:a=dll.a")` works (VS 2022 14.43 link via clang-cl 19.1.7).

**Every top-level window cloaked at creation (issue 1118, 2026-11-10).** After the c5h-b incident (the DLL's
NULL-owner precondition MessageBoxA visible on the owner's desktop ~05:39-05:47 EST, section 7 safety note):
- [I] MessageBox / DialogBox create and show their #32770 window through user32-internal paths, so hooks on the
  exported CreateWindowExA/W and ShowWindow do not see them; [L] the box's creation does pass through
  `win32u!NtUserCreateWindowEx` (it was cloaked there, counted in win_cloaked_create). [C, prologue bytes of the
  32-bit win32u on 26100] the stubs are WOW64 syscall stubs `B8 imm32 / BA imm32 / FF D2 / C2 imm16`:
  NtUserCreateWindowEx `ret 0x44` = 17 args (dwStyle = arg 4, hWndParent = arg 9 [L]), NtUserShowWindow `ret 8`,
  NtUserSetForegroundWindow and NtUserSetActiveWindow `ret 4`.
- The guard (both routes) hooks those four stubs in the process' own win32u: before a top-level window is created the
  WH_CBT activation veto is placed on the creating thread and WS_VISIBLE is removed; right after creation the window
  is cloaked (DWMWA_CLOAK) and then shown with SW_SHOWNA if it wanted to be visible; every show cloaks an uncloaked
  window first and drops activation; SetForegroundWindow/SetActiveWindow are refused while the process is not the
  foreground; a 100 ms watchdog cloaks anything missed (counted); a cloaked window can never be activated, not even
  from its taskbar button; DisableProcessWindowsGhosting() avoids a DWM ghost window (another process) for a hung
  game window. The main window still follows the cloak switch. Nothing in the EXE image; CEG handshake untouched.
- [L, stand-in, run 20261110-055909 and the full run] Two stand-ins at once, under each route: a NULL-owner
  MessageBoxA raised on a thread that had owned no window, and a MB_SYSTEMMODAL one, with the main thread not pumping
  for 12 s: both boxes cloaked at creation (win_cloaked_create 2 each: main window + box; win_cloaked_watchdog 0), at
  (1145,670)-(1475,829) and (1196,691)-(1469,850) on the primary monitor yet never visible to the owner and never foreground
  at any 20 ms poll; the system-modal box's internal SetForegroundWindow was refused (win_fg_blocked 1).
  `crash.dialog_state` read the exact title, two-line text and the "OK" button through WM_GETTEXT; `launch.stop`
  refused with that text; a PrintWindow shot of the blocked main window returned "timeout" after 3.0 s (timeout 3 s)
  instead of hanging. The boxes were never answered; the processes ended by themselves.
- [L] PrintWindow(PW_RENDERFULLCONTENT) blocks while the window's thread does not pump (it hung the harness on
  c5h-b); `shot` now runs it in an abandoned-on-timeout worker thread (default 10 s).

## 11. The harness core library `tools/harness` (issue 1103, 2026-11-10)

(2026-11-11, one-off 9027, D57) RenderDoc frame captures of a test game: `cli.py launch --preload <the allowlisted x86
renderdoc.dll>` then `cli.py capture`; the command sequence, the conditions and the huge-map limit are in
`research/05-graphics-composition/renderdoc-capture.md` and `tools/harness/README.md` ("Preload and RenderDoc
captures").

The library form of `civ_bg.py` for several instances at once (CONTRACT: `tools/harness/CONTRACT.md`; usage:
`tools/harness/README.md` section "Core (1103)"). Every call takes an `Instance` (`instance.py`) and addresses
its process by PID; state per instance in `<instance>\run\harness_state.json`, events in `run\harness.log`.

| Module | Lifted from | Differences that matter |
|---|---|---|
| `winapi.py` | `nofocus/bgwin.py` | binaries from `C5_HARNESS_BIN` (default `tools/harness/native/bin`, 1105's `build.bat`); the helper is called with `--show 4 --inject <guard> --early`, or since 1114 with `--show 4 --import-time` when the instance has the 1113 wrapper (the default then; *corrected 2026-11-10 from "always ... --early (no other path"*; until 03:05 it was `--suspended`, which section 10 shows is too late for static imports); no separate-desktop mode |
| `launch.py` | `civ_bg.py launch/status/stop` | refuses legacy_steam, any EXE under the Steam library or pure_vp, any EXE outside the instance's `install\`, and `C5_DOCUMENTS`/`C5_LOCALAPPDATA` over 140 characters; sets the CONTRACT 2 environment and forces `CIVBG_GUARD=1`; fails closed (process gone, `LaunchRefused`) when the helper exits non-zero or prints no `RESUMED`, the guard is absent or disabled, or a requested `redirect_state`/`isolate_state` (shared memory 952/956) is not 1; no adoption of another process by image name |
| `launch.stop` | new | waits until the newest `*.Civ5Save` under `Saves\` + `ModdedSaves\` (recursive) has had the same path, size and mtime for `settle_s` (10 s) **and** a registered human-turn check returns True, re-checks, then an optional in-game exit hook, else TerminateProcess (as `civ_bg.py stop`). Refuses a game without a human-turn check; on timeout leaves the process running. Records the run's gbe `STEAM_LOG_*.log` files, never deletes them |
| `input.py`, `shot.py`, `guard.py` | `civ_bg.py key/click/move/wheel`, `shot`, switches, `watch` | `watch` takes several instances; no `front` (it takes the focus on purpose) |

Game detection: `Instance.is_game` is true for a `CivilizationV*` file name **or** PE TimeDateStamp 0x546CD0A8 /
0x546CCB59 (1101: identify the EXE by timestamp, never by SHA-256). A game is launched only with Documents
redirection and single-instance isolation on, and only with `FullScreen = 0` readable in the instance's
`GraphicsSettingsDX11.ini`.

[L] 2026-11-10 02:35, stand-ins rebuilt from the shell-gates sources (`build.bat`, clang-cl i686), two at once
(`--start-steal 0 --steal 0`, 640x360, cloak + off-screen park, guard from the first instruction), test
`test_two_standins_by_pid_without_focus`: an 8 s watch over both (290 polls, i.e. ~28 ms per poll for two
instances, not 20 ms) saw **0 foreground and 0 visible polls**; the stand-ins' own logs have no `fg_is_me=1`,
`active_is_me=1` or `focus_is_me=1`; F6 reached only instance A and F7 + Shift+Enter only B (by PID); the guard
reported the held Shift (`shiftAKS=0x8000` on the Enter keydown); click, right-click and wheel arrived at the
posted client points; PrintWindow shots 640x360 and a 0.5-scaled 320x180 were live (not black, not white);
stopping A left B running. Cloaked off-screen frame rate: 19-26 fps while both ran, 141-159 fps for B alone
afterwards (stand-in `SEC` lines). Evidence: `%USERPROFILE%\AppData\Local\Temp\claude\bff3c9d097f5e142\7e38855b-f0fc-4153-8e6b-ab6806273359\scratchpad\testinst\t1qz63lsf\`
(`sa\run\`, `sb\run\`: `harness.log`, `stand_in.log`, shots) and `scratchpad\pytest-live.txt` (48 passed). [L] The
first manual stand-in launch at 02:26 from the scratchpad triggered Defender (`headless-and-parallel-testing.md`
section 1 setup note); none of the 16 launches after the owner's exclusions did.

Revisions of 2026-11-10 ~02:45-03:00 (CONTRACT 2 revised after 1102, CONTRACT 7): `C5_ISOLATE_SINGLE_INSTANCE`
carries the instance name; `C5_LOCALAPPDATA` and `GseSavePath` are set (from 1106's manifest `env` when present;
both folders created before the start). Starts are serialised by the named mutex `Local\C5HarnessLaunchGate`,
held from the helper call until the main window exists; no window within `wait_window_s` is an error
(`LaunchStartupTimeout`, process left running, no retry). `stop` ends in `StopTimeout` when the human-turn check
keeps saying not-ready and never kills on a timer. *Since 1115 (2026-11-10): `force_unsafe` is gone; the only override is
`stop(coordinator_override="<reason>")`, passed from the CLI's `--coordinator-override REASON` and never from cleanup
paths; stop acts only on the recorded PID + creation time + exe; a silent channel now means "front end", where stop
waits (up to `frontend_timeout_s`, default 300 s) while a mod activation, load or save is in progress and then raises
`NeedsDecision` (tools/harness/README.md, Stop safety). Replaced: "refuses at once when the human-turn check reports a
silent Lua channel ... `force_unsafe=True` skips only the human-turn condition".* [L] Stand-in start-up as recorded in the state file
(2026-11-10 02:58, two stand-ins one after the other, times after gate_acquired): A created +0.30 s, helper
returned (resume upper bound) +0.53 s, guard confirmed +0.53 s, window +0.59 s; B created +0.60 s, resumed
+0.93 s, window +0.99 s. The real EXE's CEG handshake (1102: up to ~30 s per start) is not measured here.
Suite after the revisions: 113 passed with the live stand-in tests (02:57-02:58), 106 passed + 7 skipped
offline; no Defender detection since 02:30.

[L] 2026-11-10 03:08-03:11, 1105's binaries (`tools/harness/native/build.bat`), `--early`, full shim (stand-ins as
class `WinClass_FXS` with `--mutex Local\Civ5App`), instance folders under `%USERPROFILE%\c5-instances\_test1103\`:
two stand-ins by PID, both `redirect_state = isolate_state = 1`, each renamed `Local\Civ5App` to
`Local\Civ5App_<name>` (shim log) and passed the gate (`lasterror=0`); 8 s watch 0 foreground / 0 visible polls;
input, shots and stop as above. Start-up after gate_acquired: created +0.13-0.89 s, resumed and guard confirmed
+0.26-1.39 s, window +0.31-1.45 s (five starts). The same instance id in a second folder: helper exit 4, the
second stand-in's `main()` never ran (no log), the first kept running. `C5_SHIM_TEST_FAIL=redirect|isolate`:
exit 4, `TERMINATED`, no process left. Suite: 115 passed with the live tests, 109 passed + 6 skipped offline;
no Defender detection. Evidence: `%USERPROFILE%\c5-instances\_test1103\pytest-full.txt`,
`pytest-live-early.txt` and the `t*` folders there (`run\harness.log`, `harness_state.json`, `shim.log`,
`stand_in.log`).

Not verified (answered for one instance on 2026-11-10 by section 12: `--early`, the gate, the CEG handshake, gbe's STEAM_LOG growth and the stop policy all worked live): anything with the real EXE (1108): the CEG handshake under `--early` and the gate, gbe's
STEAM_LOG growth, the stop policy against 1104's Lua check in a live game. (Corrected 2026-11-10 03:12; replaced
"the shim self-test fields read None until 1105 publishes the offsets": they are read since 1105's merge.)

**Which states allow a stop (bug 0105, one-off 9023, 2026-11-11) [C: code and unit tests, not run live].**
`turns.stop_readiness` (the human-turn check of `launch.stop` and of `cli.py stop --human-turn-hook
tools.harness.turns:stop_readiness`) says ready in exactly these states, each with the same settle check (the newest
file of the save folders unchanged in path, size and mtime for `settle_s`, 10 s) and a re-check after it:
1. the human turn is active; re-check: same turn, still active;
2. the game is finished: the turn is not active and `Game.GetGameState()` is 1 (GAMESTATE_OVER) or 2
   (GAMESTATE_EXTENDED); re-check: same turn, game state still 1 or 2. The reason text says `game finished (game state
   1 over) at turn N`. Before the fix this case (9034, c5h-b, turn 411, the human defeated) could only be stopped
   with the coordinator override;
3. the process is already gone.
The front end (Lua channel silent) is `launch.stop`'s own path through `frontend_state`. Not a stop condition: an AI
turn in a running game (game state 0), a silent game-state read, a save still changing. An open application dialog
stays what it was: the harness does not answer it (`cli.py dismiss` is the coordinator's command). Tests:
`tools/harness/tests/test_turns_game_over.py` (7). The game-state read was not tried against a live end-game
screen by this fix; 9034 read GameState 1 there through the same channel.

### 11.1 Harness follow-ups of issue 9006 (2026-11-13, bugfix team 4000) [C, offline]

- **One command for the whole offline suite:** `python tools/harness/run_tests.py` (bash or any shell, any folder).
  It runs pytest over `tools/harness/tests` and unittest over `tools/instances/tests`, with `-p no:cacheprovider`,
  `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1` and `PYTHONDONTWRITEBYTECODE=1`, and with `C5_HARNESS_LIVE`, `C5_HARNESS_TEST_ROOT`
  and `C5_HARNESS_INTERACTIVE_DESKTOP` removed. Live tests no longer default into `%USERPROFILE%\c5-instances\_test*`:
  without `C5_HARNESS_TEST_ROOT` they skip with a message. Counts from a clean copy of the branch are in issue 9006's log.
  Since issue 0146 (2026-11-13) it also fails when `-rs` lists `test_diplo_rearm.py` skipped, naming its dependency `lupa`
  (`REQUIRED` in `run_tests.py`); the child pytest runs without `-I`, so lupa in the owner's user site counts [C, offline].
- **Load screen: channel only.** `frontend.dismiss_load_screen` no longer posts a click at the fixed client fraction
  (0.867, 0.695) when the channel step fails (that point held for one client size only; section 12.3 below records
  the one time it was used, 1117). Success needs the probe to see the screen closed and the game unpaused; otherwise
  "load screen not dismissed" and the caller decides. `CONTINUE_BUTTON_FRACTION` and `continue_button_point` are gone.
- **No `launch.stop()` in a failure or cleanup path**, anywhere under `tools/`: `tests/test_safety_static.py` now also
  scans for `stop` of the launch module (any alias, or imported by name) inside an except handler, a finally block, an
  exit/cleanup callback, a fixture or a teardown. It found `harness/evidence/1108/run_1108.py:337` and
  `harness/evidence/1114/run_1114.py:467` (both in `finally`); both now stop in the normal path, and a driver exception
  leaves the instance running.
- **The terminate at the end of `stop()` is an explicit step** (CONTRACT section 5): after all gates, when there is no
  exit hook, the hook raises, or the process outlives `exit_wait_s`, `_terminate_after_gates` logs `stop_terminate`
  (reason), `stop_terminated`, and the result carries `terminate_reason`. (Corrected 2026-11-23, ticket 0700: the
  `stop_kill` escalation is gone; one terminate, then a bounded wait for the end, section 11.10. Was: "`stop_kill` if
  it escalates".)
  **Fault, fixed by ticket 0700 (section 11.10) (ticket 4984, 2026-11-23, c5h-new-b, EXE 26577625, the patch layout laid) [L, 4 of 8 game stops: off, b, e1on, e1off]:** the
  second `proc.wait(15)` after `proc.kill()` is not caught, so a 26577625 EXE that is still alive 30 s after
  `TerminateProcess` (the workstation busy) makes `cli.py stop` (and `perf/profilekit/acceptance.py`'s own stop, which
  then exits 1 after a complete autoplay) raise `psutil.TimeoutExpired`; the process ends by itself within the next
  minute and a second `stop` reports "exited by itself". The saves had settled before the terminate. Workaround: wait
  60 s and stop again (`server/evidence/0455/4984/smoke_4984.sh` `stop_game`). Details: `research/05-graphics-composition/extras-units.md` 4984.10.
- **One path to a dialog button** (MC decision 2026-11-12 21:08): `launch.dismiss_dialog` / `cli.py dismiss`.
  `crash.answer_dialog` was never written (it existed only as the MC's 2026-11-10 proposal in the issue).
  `crash.answer_assert` used to post `WM_COMMAND IDOK` to "Assertion Failed" boxes with no settled-save check, no
  recorded-process check and no log of who asked: it now only reports, `press=True` raises `ValueError` naming the one
  path, and `crash.py dialogs --press-ok` exits 2 without posting. `tests/test_safety_static.py` fails on any
  `WM_COMMAND`, `BM_CLICK`, `WM_CLOSE` or `WM_SYSCOMMAND` posted or sent outside `launch.dismiss_dialog`, on a key or
  mouse message in a function that looks dialogs up, and on synthetic input (`SendInput`, `keybd_event`,
  `mouse_event`), over `tools/` and `server/`. Named exceptions: the live tests' teardown of the stand-in's own boxes
  (`TEST_STANDIN_CLEANUP`, 6 functions since 0390 on 2026-11-16: 0386's and 0388's `press_ok` joined the first 4), two
  pressing scripts outside the harness, `tools/i0b/answer_precondition.py` (epic 1900's one-off, authorised 2026-11-10)
  and `server/evidence/2500/press_ok.py` (retired by D29), to be removed by their owners, and since 0390 the MC's
  real-input exception off the workstation (`REAL_INPUT_OFF_WORKSTATION`, 11.5).

### 11.2 The SHA-256 backup's standard set (`tools/harness/backup.py`; issue 0144, 2026-11-13) [C, offline]

`python -I tools/harness/backup.py backup --out DIR --instance NAME` (or `backup.standard_files(instance)`) records the
AGENTS.md section 4 set of one instance: every `CvGameCore_Expansion2.dll` under `install\Assets\DLC` and the user
folder's `MODS`, the install-root DLL if present, `config.ini`, `GraphicsSettingsDX11.ini`, the front-end Lua
(`MainMenu.lua`, `LoadScreen.lua`, `FrontEnd.lua`), the `crashlogs` markers `allocTrace.enabled`,
`queueguard.capture`, `shell.helper`, `nodumps.please`, `nopopups.please` and **`viewmask.enabled`**, and
`cache\luaexec.enabled`. A marker absent at backup time is recorded with `existed=false`; once a run creates it,
`verify DIR` lists it as `created` (exit 1) and `restore DIR --remove-created` deletes it (without the flag it is
reported as "left (created after backup)" and kept).

`viewmask.enabled` (the D33 system mask written by `shell/tools/darkred.py` and `shell/tests/tap_live.py config --arm
d33`) was missing from the set until 0144 (2026-11-13): epic 3300's runs removed it by hand
(`shell/evidence/3310/run_3310.py` adds it to its own file list; auditor cycle 5 F7 found one removal by `rm` after a
`cp`). Since 0144 a D33 run that backs up the standard set before arming gets the marker restored, or removed, by
hash; this is the condition of D100's reading R1 (`research/07-architecture-decisions/queue-reexamination-2026-11-13.md`
section 1). Tests: `tools/harness/tests/test_ops_backup.py` (10; the two 0144 tests and the extended
`test_standard_files` fail against the pre-0144 code, `tools/harness/evidence/0144/acceptance.txt`).
~~Not covered: `tools/instances/provision.py verify` does not flag a leftover marker in a pool instance.~~ Covered
since 0151 (2026-11-13), next paragraph.

**Leftover markers in `provision.py verify` (issue 0151, 2026-11-13) [C, offline].** `provision.py verify NAME` now
returns a `warnings` list: one entry per marker of `backup.CRASHLOG_MARKERS` (imported from
`tools/harness/backup.py`, not copied) present in the instance's `crashlogs_dir` (manifest key, default
`install\crashlogs`) while no process runs from the instance folder (`running_in_instance`), e.g.
`crashlogs marker left in an idle instance: install\crashlogs\viewmask.enabled (restore the run's backup, backup.py
restore --remove-created)`. The CLI prints each one to stderr as `warning: ...` after the JSON. Warnings do not make
`ok` false or change the exit code, and verify removes nothing: removal stays the restore's job. The process list is
read only when a marker is present; while the game runs, markers are expected and not reported; if the process list
cannot be read, one warning says so and names the markers. Tests: `tools/instances/tests/test_provision.py`
`test_verify_warns_on_leftover_viewmask_marker` and `test_verify_warns_on_every_marker_only_when_idle` (on temp trees,
mocked process list; both fail against provision.py at `7b8f45f7`, `tools/harness/evidence/0151/acceptance.txt`).
Not verified: a `verify` on a real pool instance with a leftover marker.

### 11.3 Every harness script runs under `python -I` (issue 0331, 2026-11-15) [C, offline]

`python -I` (the project's rule) implies safe-path (`-P`, Python 3.11+): the script's own folder is not put on
`sys.path`. Eleven harness modules had `try: from . import x` / `except ImportError: import x`; run as a script, the
relative import fails and the bare fallback then found nothing: `checklist.py`, `crash.py`, `frontend.py`, `lua.py`
(`No module named 'backup'`), `diplo.py` (`'lua'`), `driver.py`, `shot.py` (`'crash'`), `guard.py`, `input.py`
(`'winapi'`), `launch.py` (`'audio'`), `turns.py` (`'diplo'`). Measured 2026-11-15 with Python 3.11.5 (Anaconda) from
the repository root and from `C:/c5-64bit-work/0331`: exit 1 with `ModuleNotFoundError` in all eleven, both folders.
`cli.py`, `pool.py` and `stop_nolua.py` were already safe (their `if __package__ in (None, "")` branch inserts the
folder); `backup.py` has its fallback inside `_load_instance`, which `--help` never reaches, so it failed with
`--instance` until 0412 (11.7; *corrected 2026-11-16, replaced "has no fallback import and always worked"*). Since 0331 each `except ImportError:` branch first puts
`Path(__file__).resolve().parent` at the front of `sys.path` (when absent), as `cli.py` does; nothing else changed.
After the fix all 25 `tools/harness/*.py` with `--help` give the same exit code from both folders, and none raises
(`dump.py` and `memory.py` print their docstring and exit 2 and 3 by design; neither has the fallback).
Test: `tools/harness/tests/test_isolated_imports.py` finds every script with either pattern by regex and runs each, plus
`backup.py`, as `python -I -X utf8 <script> --help` from the root and from a temp folder (exit 0, no traceback); 31
pass after the fix, 22 fail against main at `ef4654e7` (`tools/harness/evidence/0331/acceptance.txt`, made by
`acceptance.py` there). Full offline suite after the fix: harness 723 passed, 18 skipped; instances 85 ran, OK.
This replaces the workaround in `research/06-tooling-and-workflows/release-driver.md` W5.5 ("harness scripts run without `-I`").

### 11.4 Stale SQLite journals and the front-end check (ticket 0357, 2026-11-15)

**The fault [L].** Until 0357, `turns.frontend_state` answered BUSY for any `*-journal` / `*-wal` file in `cache\`,
the user folder or `ModUserData\`. On 2026-11-15 07:41 EST a read-only scan of all nine instances found exactly two:
`c5h-a` `cache\stats.db-journal` (6,704 B, 2026-11-14 04:12:04, SHA-256 `90482a1f75f4...702f`) and `c5h-c`
`cache\stats.db-journal` (6,704 B, 2026-11-14 03:17:43, `6b59c07d9a09...8567`). Both have a zeroed header (first 8
bytes 0) and no process holds them (Restart Manager, with `c5h-a`'s game pid 71660 running on DLL pin
`i0ship-26234371-ia`). They are what `journal_mode = PERSIST` (D35) leaves after a commit; the pinned DLL writes no
stats.db, so nothing ever clears them, and `turns.py frontend` answered BUSY forever there: menu waits failed and a
harness stop needed a coordinator override (0330's worker 4203, 2026-11-15). The two files were not removed by 0357:
the MC kept 4203's games running (c5h-a holds a stock seed until 2026-11-16) and asked lead-0330 to run `clean` on its
own claim when those games end; until then they are harmless (see below).

**How SQLite keeps journals on Windows [L, Python 3.11 sqlite3 module, 2026-11-15].** Probe
`C:/c5-64bit-work/0357/rmprobe.py` (not committed): with `journal_mode = PERSIST` the journal stays **open** after
every commit while the connection lives (the Windows VFS reports `SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN`, so the pager
does not close it at unlock [I, from SQLite's pager source]); its header is zeroed after each commit. During a write
transaction the header holds the magic `d9 d5 05 f9 20 a1 63 d7` at once with `synchronous = OFF` (D35); with the
default synchronous it stays zero until the journal is synced just before the database pages are written. After
`close()` the file stays, zeroed, held by no process. So: a held journal may be an open transaction (busy); a journal
no process holds cannot be cut short by a stop; a zeroed one is not needed by the database at all; a "hot" one (magic,
not held) is rolled back by SQLite at the next open of its database and must never be deleted by hand.

**The fix [C, offline tests].** `tools/harness/sqlite_journal.py`: holders through the Restart Manager
(`RmStartSession` / `RmRegisterResources` / `RmGetList`, no administrator rights; it never opens the file, so it cannot
disturb the game's handle; about 0.3 s for the first call in a process, mostly loading `rstrtmgr.dll`), header kinds
(`empty`, `zeroed`, `hot`, `wal`, `other`, `unreadable`). Rules:
- `turns.frontend_state`: busy only for a journal a process holds open, or whose holder cannot be told (API failure:
  unknown stays busy). A journal held by no process goes to `FrontendState.stale_journals` and is named in the reason
  (`...; stale journal ignored, held by no process: stats.db-journal (6704 B, 2026-11-14 04:12:04, header zeroed, held
  by no process)`). A PERSIST journal of a DLL that keeps stats.db open stays busy, as before.
- `launch.stop`'s front-end wait logs each stale journal once per stop (`frontend_stale_journal` in `run\harness.log`).
- `provision.py verify` warns (not an error) for each stale journal, with the `clean` command for a zeroed or empty one
  and "left alone" for a hot one; a held journal is not reported.
- `python -I tools/harness/sqlite_journal.py scan --instance NAME` (exit 1 when a stale one exists) and `clean
  --instance NAME --owner WHO --backup-dir DIR [--dry-run]`: refuses (exit 2) unless WHO holds the pool claim and no
  process runs from the instance; backs up by SHA-256 through `backup.py` (copy verified), re-checks each file right
  before the delete, and removes only stale `zeroed` / `empty` journals inside the instance folder; held, hot, WAL and
  unknown files are kept and listed. Backups of the c5h-a / c5h-c files go to `F:/c5-64bit-work/0357/journal-backups`.

Tests: `tools/harness/tests/test_sqlite_journal.py` (21: holders of an own open handle, header kinds, a real SQLite
PERSIST + synchronous OFF transaction, scan folders, CLI exit codes, clean: backup hash, dry run, kept files, unknown
holder, refusals without claim and with a running process, re-check before delete), `test_safety_stop.py` (journal
and WAL cases now hold the file open; a stale journal no longer blocks the stop and is logged once; unknown holder is
busy; a real SQLite PERSIST journal is busy while the connection lives and stale after; `turns.py frontend` exit codes),
`tools/instances/tests/test_provision.py` `test_verify_warns_on_stale_journal`. Live check 2026-11-15 07:44: `scan`
on c5h-a and c5h-c printed `STALE` for the files above (exit 1), c5h-b `no journal` (exit 0). [L] 2026-11-15 07:57,
`tools/harness/evidence/0357/acceptance.txt` (branch commit 9188bfcd plus the acceptance script's fix): `c5h-c`'s
front-end check now answers `IDLE front end idle (activation done, no journal held open, cache quiet 1894 s); stale
journal ignored, held by no process: stats.db-journal (...)`, where it answered BUSY before; `c5h-a` (game loaded) is
busy for that reason only, with its journal listed as stale. Full offline suite (`run_tests.py`, 07:48-07:55): harness
749 passed, 18 skipped; instances 87 ran, OK.

### 11.5 Three harness-suite breaks on main and why the merge checks let them through (ticket 0390, 2026-11-16) [C, offline]

On main `1440a186` the offline harness suite (`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest tools/harness/tests`)
had two red tests holding three breaks (run of 2026-11-16 about 01:10 EST, after the first fix: 1 failed, 875 passed, 24 skipped):

| Break | Merged by | Cause |
|---|---|---|
| `test_framecap.py::test_idle_window_measures_a_timing_instance`, `TypeError` at `idle_window.py:84` | `0420c744` (0350, 2026-11-15 21:56) | 0350 made `frame_counter.lua` return five values and `idle_window.frames()` a dict; the test replaced `frames()` itself with a double returning the old tuples, so the change of shape reached `window()` untested. 0350's hand-back ran its own `proxy/tools/live/test_frame_counter.py`, not the harness suite |
| `test_safety_static.py::test_one_path_to_a_dialog_button`: `steam_launch.py` `foreground`, `send_keys` (`keybd_event`), `op_click` (`mouse_event`), `op_close` (`WM_CLOSE`) | `00a2e638` (Merge 0323, 2026-11-15 12:16) | 0323's session ops on the laptop's Steam install send real input to the game's window (Shift+Tab opened Steam's overlay only through real keys, installer-notes.md 10.4). The scan bans synthetic input anywhere under `tools/`; 0323 ran only `tools/testbed/test_steam_launch.py` |
| the same test: `test_live_escalation.py:164` `press_ok` (`WM_COMMAND` to its stand-in box) | `2663b8ee` (0388, 2026-11-16 01:02) | the same teardown as 0386's `test_live_dialogwatch.py` `press_ok`, not named. 0388 **did** run the suite and reported "2 failed (both 0390's)": its new hit landed inside a test that was already red, so the failure count did not change |

**Fixes (branch `task-0390-harness-suite-fixes`).**
- `test_framecap.py`: the double now sits under `frames()` (the Lua channel's `run_lua`/`values` and the module's
  clock), so `frames()`' parsing of the five values is exercised. Four tests: both maxima are read b's
  (`max_frame_s` 0.032, `max_frame_engine_s` 0.0469), both clock ratios 1.0 over a 2.0 s window; an arming read (-1)
  gives no ratios; an empty window gives no maxima; the old three-value chunk exits 1 with "expected 5 values".
- `test_safety_static.py`: `("harness/tests/test_live_escalation.py", "press_ok")` joins `TEST_STANDIN_CLEANUP`; the
  MC's named exception `REAL_INPUT_OFF_WORKSTATION` (D63; MC's answer on 0390, 2026-11-16) lists `steam_launch.py`'s
  four functions by (file, function), and an entry that no longer hits fails the test.
- The MC's condition, in `steam_launch.py`: `fence_real_input()` at the top of `foreground`, `send_keys`, `op_click`,
  `op_close` and `op_overlay` raises `RealInputRefused` on the workstation, before any `user32` call; `dispatch`
  answers `"rc": "refused"`. The workstation is known by host name: the SHA-256 of the lower-cased `gethostname()` or
  `COMPUTERNAME` is compared with `WORKSTATION_HOST_SHA256`, so no machine name is written in the file; no readable
  host name is refused too. `tools/testbed/test_steam_launch.py` `RealInputFence` proves the refusal of all seven op
  forms with no `user32`/`kernel32` call, the keys going out off the workstation, and (skipped elsewhere) that this
  workstation is recognised. Not covered by the scan: the `uia` op's UI Automation Invoke on Steam's launch-option
  chooser presses a button without any of the scanned calls [C, read].

**Rule for hand-backs (from 0388's miss).** Compare the suite's failure **lines** with main's, not the count of red
tests: one red test can hold several hits (`test_one_path_to_a_dialog_button` lists every site in one assertion), and a
new hit inside an already red test leaves the count unchanged. A ticket whose touches include `tools/` runs the full
harness suite, not only its own test file (0323, 0350).

### 11.6 A run's DLL restore step: `set-dll --previous`, not `--original` (ticket 0396, 2026-11-16)

**Symptom** [L, 2026-11-16 02:24 MC, 02:21 0395, before that 0374's lead (#2124/#2128)]: `provision.py verify c5h-c`
ok false, `DLL MODS\(1) Community Patch\CvGameCore_Expansion2.dll hash e589306e... != manifest 5fdf3432...` (plus
`user: size` of the same file). 0396's quick verify at 02:52 EST gave the same two errors.

**The run that left it: ticket 0380's live driver, restore phase, 2026-11-15 22:42 EST** [C, instance.json history and
0380's evidence]. c5h-c's `dll.history` (read 2026-11-16 02:50) ends:

| at (EST) | source | old -> new | by |
|---|---|---|---|
| 2026-11-15 20:44:00 | `run\proxy-original\` (set-proxy --original) | 49ac48af -> **e589306e** | 0354 |
| 22:22:18 | `c5-i0\dlls\i0ship-26234371-ia` | e589306e -> 3c2f47da | 0380 prep (`set-dll --dll`) |
| 22:27-22:41 | 0380's proxy layouts and their `set-proxy --original` | 3c2f47da <-> 75c40cec / 167fa063 | 0380 arms |
| 22:42:00 | `run\dll-original\` (`set-dll --original`) | 3c2f47da -> **5fdf3432** | 0380 restore |

`research/03-dll-engine-boundary/evidence/0380/live_0380.py` `restore()` runs `set-dll --original`, then
`tools.harness.backup restore <work>/std-backup`. `--original` is the instance's **first** DLL, kept in
`run\dll-original\` at the first swap of 2026-11-11 00:45 (the 1107 build `5fdf3432...`), so the manifest moved to
5fdf3432. The std-backup, taken at prep with `backup --instance` (whose standard set includes the MODS DLL, 11.2), held
the e589306e found at 22:22, so the backup restore wrote e589306e back by hash **without** instance.json (the file's
mtime is still 20:44:00.63, copied with its times). 0380's own `state.json` shows it: `set_dll_original_rc 0`,
`backup_restore_rc 0`, `verify_rc 1`, `dll_back true`. 0380's acceptance item 6 read the failed verify as "as before
this ticket"; the history shows the manifest was e589306e at its 22:22 prep, so that reading is corrected here.

**Fix on c5h-c** [C, 2026-11-16 02:52 EST, claim `impl-B-0396`]: `provision.py set-dll c5h-c --dll
<pins_dir>/dlls/i0ship-bbfacca1-ia/CvGameCore_Expansion2.dll` (SHA-256 `e589306e...5c54`, the build c5h-c carried
from 2026-11-11 14:24 until 0380, every run since found it): old = new = e589306e, the manifest follows. Then
`provision.py verify c5h-c --full`: **ok true**, 0 errors, full pass, 82.3 s (one warning: the stale `stats.db-journal`
of 2026-11-14 03:17, 11.4, not touched). Not moved to the current pin `3c2f47da` (i0ship-26234371-ia): runs set-dll the
pin themselves, and the as-found state is what the next run expects.

**The gap, and who else shares it** [C, read-only census 2026-11-16 02:56]: every driver or recipe that restores with
`set-dll --original` assumes it means "what I found". It means "the first DLL ever": on c5h-a it is `5fdf3432` (1107
build), on c5h-b `262aee88` (the owner's MODS DLL, **no Lua channel**), on c5h-c `5fdf3432`; on the F: instances it is the pin
`3c2f47da`, so harmless there. `tools/harness/RECIPE-team-dll.md` line 195 (`set-dll c5h-b --original`) teaches it.
The second half, a backup restore of the DLL after the manifest moved, is shared by any driver that runs
`backup restore` over a `--instance` standard set without first putting the DLL back through `set-dll`; 9039's
`ls_setup_9039.py` (set-dll to the recorded source first) and 3515's `s13_mods.py` (backup restore, then set-dll) do it
right. All nine instances were consistent at 02:56 (file = manifest).

**Code fix, `tools/instances/provision.py`** [C, tests `test_restore_step_original_vs_previous_0396`,
`test_set_dll_cli_previous`]:
- `set-dll NAME --previous`: puts back the DLL that the newest `set-dll --dll` replaced (history entries of `--original`,
  `--previous` and set-proxy are skipped), from the newest history source with that hash that still exists and hashes
  right, else the kept copy; checked before the write and after; refused with nothing written when no such file exists.
  The history entry carries `previous: true`. This is the restore step for a run.
- `set-dll --original` prints and returns a `note` when the newest `--dll` swap replaced a DLL other than the kept one.
- `verify`: when the DLL file's hash is one the history recorded, the error adds since when, the source, and the
  `set-dll NAME --dll <source>` that records it (the 0380 case reads: "it was put back without set-dll (a backup
  restore?) after the manifest moved").
Filed, not changed (outside 0396's touches): `RECIPE-team-dll.md` line 195 and 0380's `live_0380.py` `restore()` should
use `--previous`.

### 11.7 `backup.py --instance` under `python -I`, and the child-scan test under load (ticket 0412, 2026-11-16) [C, offline]

**(a) `backup.py backup --instance NAME` failed under `-I`.** `backup.py` imports `instance` lazily, inside
`_load_instance` (`try: from .instance import Instance` / `except ImportError: from instance import Instance`), not at
module level, so 0331's scan (11.3) did not see the pattern and its `--help` test never reached the import. Run as a
script under `-I`, the bare import found nothing: `ModuleNotFoundError: No module named 'instance'` (reproduced
2026-11-16 08:05 with main's `backup.py` at `28bc42da`; reported by 0392's lead, #2384). Fix: the fallback branch puts
`Path(__file__).resolve().parent` on `sys.path` first, as the others do. Test
`tools/harness/tests/test_backup_isolated.py` provisions a throw-away `instance.json` under `tmp_path`, sets
`C5_INSTANCES_ROOT` (an environment variable `-I` does not ignore) and runs the real CLI `python -I -X utf8 backup.py
backup --out DIR --instance t0412` from the repository root and from a temp folder: exit 0, the manifest holds the
instance's `config.ini` and install-root DLL. *Corrects 11.3's "backup.py has no fallback import and always worked":
it had one, in a function, and only `--help` worked.* A lazy import inside a function is a place the 0331 regex
does not look; a script with one needs a test that runs the command that reaches it.

**(b) `test_safety_children.py::test_scan_records_child_by_parent_and_creation_time` failed under load.** The failing
line was always the "recorded once, when first seen" check (`assert 3 == 2`, line 100 at `28bc42da`): the test scanned
twice and compared the count of recorded children. Under load a `conhost.exe` that the first scan did not list (its
creation time was before the first scan in run 23, so it was present but not yet visible to `psutil`'s child list, or
still unreadable) appears in the second scan and is recorded then, correctly. The harness was right; the test counted.
Measured 2026-11-16 08:07-08:11 on the workstation (6 `CivilizationV_DX11.exe` + 6 `server.exe` of other agents, CPU
84-100 %, 32 logical processors), one pytest process per run: **26 / 30** with the old test, all four failures at
that line (runs 18, 19, 23, 27). The test now checks identity instead of the count: the first scan's records are
still there unchanged and not repeated, no (pid, creation time) is recorded twice, a record added by the second scan
must be a `conhost.exe` whose parent is the EXE or the child, and the `child_seen` log events match the records one to
one by (pid, creation time). The other checks are unchanged (`pythons(live) == [kid]`, names within
{python.exe, conhost.exe}, the reused-pid check, the status row). **After: 30 / 30** (2026-11-16 08:12-08:16, same
machine state: 6 game clients + 6-7 servers, CPU 84-100 %). Not shown: how many of those 30 runs met a late conhost
(the test passes silently either way; at the before rate about 4 would). Teeth: with `children.record`'s de-duplication
removed, the new test fails at "no process recorded twice".
Evidence: `C:/c5-64bit-work/0412/before/` and `after/` (`summary.txt` per run with the CPU load, `runN.txt`), not kept.

### 11.8 The prepared front end is restored on stop, by the install manifest's hash (ticket 0575, 2026-11-19) [C, offline]

**The gap [C, code read at main `f82b6968b`].** `frontend.py restore()` was a manual step: `cli.py stop` and a failed
`cli.py launch` never called it, and nothing logged a prepare or a restore (prepares could only be dated from the
`run\frontend-backup-*` folder names and `frontend.json`). `provision.py verify` listed a prepared front end under
`prepared` and returned ok, idle or not. Three instances kept a patched `MainMenu.lua` on 2026-11-18/19 (MC-B #6591,
#6583, impl-0573 #6643): c5h-a from the prepare at 21:35:35 to ~21:45; c5h-c (patched hash `8a1ccdfa...`) from before
21:37 to 23:03; c5h-b a 26,840 B `MainMenu.lua` dated 21:02, owner unknown.
**c5h-b, the third case [C for the folder, I for the mechanism].** Every `run\frontend-backup*` on c5h-b, 2026-11-18
14:43 to 2026-11-19 01:26 (read 2026-11-19 02:05), holds the manifest's `MainMenu.lua` (`5c525c54...`, 11,233 B) and
`FrontEnd.lua` (`35dd0987...`, 688 B), so frontend.py's own backup did not take the patched file. The likely path is
the trap of `human-input-testing.md` (4501): a standard-set SHA-256 backup (`backup.py`, front-end Lua included) taken
while the front end is patched, restored after `frontend.py restore`, puts the patch back. Confirming it needs 0573's
driver order (which backup it restored, and when). Not changed here (`backup.py` is outside 0575's touches): a
standard-set backup that refuses a front-end file that is not the manifest's copy would close that path too.
**Census of every instance, 2026-11-19 02:31 [C, read-only: `frontend.json` against the current hashes and
`install.manifest.tsv`].** Three more free (unclaimed) instances held a patched `MainMenu.lua` that nothing had restored:
c5h-d (prepare 2026-11-18 20:57:58, the patched copy `83bacf08...`; found by this ticket's first verify, then
restored by it at 02:29:52), c5h-f (prepare 2026-11-18 19:34:05, launched 19:34:33, 26,828 B) and c5h-g (prepare
20:57:51, 26,673 B); both claimed by impl-0575, restored by 0575's `frontend.py restore` (02:35:50, 02:36:05: the
manifest's `MainMenu.lua` from their own `frontend-backup-20261118-045920` / `-194359`), released. c5h-e was patched by its running game (0573's claim), as expected. c5h-a/b/c/h/i and c5h-mpk held
the manifest's copies. Who prepared c5h-d/f/g is not in any log (the code before 0575 wrote no prepare line). c5h-d
also carries an installer layout of 0509 (proxy files and `vp64helper.exe` in the CP mod folder, a DLL put back without
`set-dll`): verify lists 14 errors besides the front end; not changed here, reported to the dispatcher.

**What 0575 changed** (`tools/harness/frontend.py`, `cli.py`, `tools/instances/provision.py`):
- **Back up only the manifest's copy.** `prepare()` backs a front-end file up only when its SHA-256 equals its row in
  `install.manifest.tsv`; a missing row or another hash is refused with the file and its full hash, nothing written.
  A later prepare on a state whose recorded backup is not the manifest's copy is refused too. `prepare
  --restore-first` (`restore_first=True`) first puts the manifest's copy back by hash, then backs it up.
- **Restore to the manifest's hash.** `restore()` targets the manifest's SHA-256, not "whatever the backup holds":
  the copy comes from the prepare's own backup when it hashes to it, else from the newest other
  `run\frontend-backup*\` whose manifest entry and content both hash to it (`frontend.manifest_copies`). Written
  through a temporary file in the same folder, checked before the rename and after. With no such copy the file is
  left as it is, `ok` false, no `restored_at`. A front end patched by something that left no state is put back too.
- **The stop path.** `cli.py stop`, once the game has ended (stopped, or "not running"), and every failed `cli.py
  launch` (a refusal before the start, the process ending by itself, a start-up timeout) call
  `frontend.restore_after_stop()`; `frontend.json` gets `restored_at` and `restored_by` (owner, why). A process still
  running (start-up timeout) makes the restore refuse; it is logged and the stop restores later. Exit 9 of `stop`:
  the game ended but the restore failed. `--keep-frontend` on either leaves it patched on purpose (logged as
  `frontend_kept`). Consequence: **after a stop or a refused launch the next auto-load launch needs a new `frontend.py
  prepare`** (it needed one already for a new load token, E9). Callers of `launch.stop()` from Python (`driver.py`,
  the evidence drivers) are not covered; verify catches what they leave.
- **harness.log lines.** `frontend_prepare` (loadToken, load, backup, patched hashes, restore_first) and
  `frontend_restore` (why, ok, loadToken, prepared_at, per-file action and hash, failures), each with `pool_owner`
  (`C5_POOL_OWNER`), `job_owner` (`C5_JOB_OWNER`) and `at` (local time with zone), through `Instance.log`.
- **verify.** `provision.py verify` reports `front end left patched: install\... is the prepared copy of
  run\frontend.json, which has no restored_at (prepared_at, loadToken, backup) while no game runs` as an **error**
  (exit non-zero) when a front-end file's current hash is the recorded patched hash and no process runs from the
  instance (`frontend_left_patched`). While the game runs it stays under `prepared`.
- **A comparison after one's own prepare proves nothing.** W3 (0465) reported its guarded files unchanged against a copy
  taken after its own prepare while c5h-c stayed patched. Compare against `install.manifest.tsv` (`provision.py
  verify`) or the pre-prepare backup.

**Tests** [C, 2026-11-19]: `tools/harness/tests/test_frontend_restore.py`, 17 tests on a synthetic instance folder:
15 fail on main's three files (run with `git show main:<file>` in place, 2026-11-19 02:03), all 17 pass after.
Suites run after the change: the front-end, CLI exit-code, stop, backup and safety-static suites and
`tools/instances/tests/test_provision.py` (315 passed), whose `test_verify_lists_prepared_frontend` now needs a
running game for `ok` and checks the idle error.

**Live cycle [L, 2026-11-19 02:33:33-02:39:27, c5h-h on F:, claimed by impl-0575].** F:'s read latency first
(`diskwatch.py run --drive F:`, 12 records 02:22:59-02:23:54: `rlat_ms` mean 1.07, max 4.88, idle >= 97.2 %). DLL pin
`i0ship-e96457fd-ia` in by `set-dll` (the instance carried `3c2f47da...`), back by `set-dll --previous` at the end.
`frontend.py prepare --mods cp,vp,eui` (menu only, loadToken 1795073630) -> `verify --quick` idle: exit 1 with the one
"front end left patched" error -> `cli.py launch` rc 0 (pid 50596, start-up watch ok) -> `frontend.py menu-ready` rc 0
after 3 min (02:34:25-02:37:28) -> no dialog box -> `verify --quick` while running: rc 0, no such error -> `cli.py stop
--human-turn-hook tools.harness.turns:stop_readiness` rc 0, the main menu stopped, then "front end restored by the
install manifest's hash ... restored 1, already equal 1" (`frontend.json` `restored_at` 02:37:59, status: all three
files "original") -> `harness.log` held `frontend_prepare` 02:33:49 and `frontend_restore` 02:37:59 (`why` "cli.py
stop", owners impl-0575, the token) -> plain `verify c5h-h` rc 0, no errors, no warnings. No screenshot was taken.

### 11.9 Four red or flaky tests on main and their causes (ticket 0602, 2026-11-20) [C, L offline]

Found by the MC's runs of 2026-11-20 05:48-07:53 EST (main and epic 0590 stage 2 merged). None was a product fault;
item (c) shows a small harness labelling fault, drafted as `tracker/issues/NEW-0602-1-boxless-crash-default-process-name.md`
(numbered 0608; fixed 2026-11-20, see (e)).
No harness code was changed. Acceptance runs: `research/06-tooling-and-workflows/evidence/0602/acceptance_0602.txt`.

**(a) `test_hi_coverage.py::test_the_repository_model_is_matched_whole`: the proxy model one pin behind [C].** The test
compares `proxy/model/interfaces.json` `source.commit` with `deps/dll.lock` `commit`. Pin batch 5 (0551, 2026-11-18)
ran `regen_model.py` without `--write` because the model came out EQUAL (`upstream-fix-batch-5.md` 4, "nothing
regenerated"); EQUAL masks `source.commit`, `generated_at`, `fork_path` and the AST counts, so the committed model kept
c29442e8c. Fix: 0551's own `regen_model` output at 1fd51353e (the validated take's model stage, source fingerprint
matched, report EQUAL, `C:/c5-64bit-work/0551/run/model/model/`) written into `proxy/model/` as `--write` does (a plain
copy of the five files). Diff by method: 1,615 = 1,615, 0 only-old, 0 only-new, 0 changed; `policy-table.tsv`
identical; only the masked fields differ. The pin-move step is now in `new-vp-release-checklist.md` step 10 ("write the
model even when it comes out EQUAL"); the MC adds the same line to `deps/dll.lock`'s Re-pinning comment.

**(b) `test_safety_children.py::test_child_left_alive_is_an_orphan_and_never_terminated`: the test's child ended by
itself before it was set up [L].** The test's stand-in child ("live 60 s, ignore the parent") first ran
`psutil.Process(os.getppid())` and only then slept. `start_exe` returned as soon as the parent printed the child's pid,
before the child's interpreter had imported psutil. On a loaded machine `stop` (settle 0.2 s) terminated the parent (the
recorded EXE, as designed) before the child reached that line; `getppid()` then named a dead pid, the child raised
`psutil.NoSuchProcess` and exited rc 1 by itself; `stop` rightly found every recorded child exited and returned 0, not 7.
Reproduction (2026-11-20 09:30 EST, workstation, `C:/c5-64bit-work/0602/repro_orphan.py`, not kept; the parent killed
by the script `delay` s after it printed the child's pid, the child's stderr captured): delay 0 and 0.05 s: 5/5 children
dead within 5 s, each `rc=1 psutil.NoSuchProcess: process PID not found`; 0.5 s: 2-3 of 5 dead, same error; 2.0 s: 0 of 5.
The harness did not end the child: `stop` and `children.wait_exit` never terminate a child (the only terminate in
`launch.stop` is `_terminate_after_gates` on the recorded EXE's pid and creation time) [C]. Fix (test only): the child
opens its parent only in "follow" mode, writes a ready file once set up, and `start_exe` waits for that file (up to 60 s,
failing by name otherwise) before the test goes on.

**(c) `test_dialogwatch_boxless_crash.py::test_real_watcher_records_a_boxless_crash`: a late watcher, not another
game's crash [L].** The 05:50 failure (`C:/c5-64bit-work/mc-m0590s2-logs/harness.log`): `m["dialogs"][0]["process"] ==
'CivilizationV_DX11.exe'`, the three asserts before it passed. That name was `crash_box`'s literal default when the
watcher could not read the image (removed by 0608, see (e)); nothing machine-wide fills the box (the MC's suspicion, c5h-g's crash at 05:45, is not
the cause). The stand-in ended 2.5 s after its start; on a loaded machine the starter plus the detached `pythonw`
watcher took longer, so the watcher's `ExitCodeReader` opened the already-ended stand-in: `OpenProcess` succeeds while
the test holds its Popen handle and the exit code reads 0xC0000005, but `QueryFullProcessImageNameW` fails on an ended
process, so `image` is None. Reproduction (09:47 EST, `C:/c5-64bit-work/0602/repro_late_watcher.py`, not kept): opened
while alive `image='...python.exe'`; opened after the end `ok=True image=None code=0xC0000005`; the real watcher started
after the end logged `polls: 0` and wrote a marker naming `'CivilizationV_DX11.exe'` for the python stand-in. Fix (test
only): the stand-in ends 1 s after a go file, which the test writes only once the watcher's `dialog_watch_start` line for
the stand-in's pid is in the log (written after its handle was opened); the test now also requires every watcher record
to name the stand-in's pid, `dialog_watch_start` to carry the stand-in's creation time and the watcher's pid, at least one
poll before the end, and the marker's `game_pid` and box `pid` to be the stand-in's. The literal default is the drafted
low-severity fault (a game that ends before its watcher opens it, or a DX9/Tablet EXE, gets a name nobody read).

**(d) `perf/quietlock/tests/test_quietlock.py::test_take_overlap_records_the_job_as_disturbance`: a 5 s wall-clock
bound [C].** The bound stood for "`--overlap` does not enter the job wait" (`JOB_WAIT_MIN` 20 min). It is now asserted
directly (`_wait_for_jobs` replaced by a recorder in the test: never called), and the clock bound is a sanity check at
60 s; the disturbance record checks are unchanged.

**(e) A boxless crash's process name says where it came from (ticket 0608, 2026-11-20) [C, L offline].** Item (c)'s
literal default is gone. `dialogwatch.crash_box` names the process by the image the watcher's `ExitCodeReader` read
(`process_source` "image", `process_read` true); when there is no image, by `launch_exe(inst, pid, create_time)`
(`process_source` "launch record"): first the state file's `process_exe` (`harness_state.json`, read from the process by
`launch()` while it ran) when the state names this pid and creation time (within 1 s), else the instance's exe
(`instance.json` `exe`; when the manifest names none, `instance.py`'s default, which is the EXE `launch()` starts, and
the text says "instance.json names none"); when neither exists, `process` is `"unknown image"` (`process_source`
"unknown"). Without an image the box text adds why and from where, e.g. `(image not read: the process had ended when
the watcher opened it; named from the launch record, instance.json exe)`. `ExitCodeReader.image_why` gives the reason:
`UNREAD_ENDED` when the process had ended at the open (exit code readable, not `STILL_ACTIVE`), else
`QueryFullProcessImageNameW`'s error on a running process. The signature string is unchanged on purpose (it carries no
process name and is the ledger's key: one crash counts once whatever the watcher's timing; MC, thread 0608 #8292). The
marker and the `dialog_crash_exit` log line carry `process`, `process_source` (and the marker `process_read`);
`marker_dialog()`'s dialogs carry `process_source` and `process_read` when the box has them. A watcher whose game was
already gone at its first `alive()` check (0 polls) logs one `dialog_watch_late_start` line (`handle`, `image_read`,
`why`), but only a watcher with its own `ExitCodeReader`: with injected doubles (the offline tests' `exit_code=`) there is
no process to be late for, and the existing tests that end a Fakes watcher before its first poll keep their exact event
lists (the first version logged it there too and broke
`test_dialogwatch_werfault_record.py::test_exit_255_without_a_werfault_box_is_as_today`). What each caller now gets:

| Caller | What it reads | What it gets now |
|---|---|---|
| known-bug table (`known_bug`) and the ledger (`ledger_count`) | the signature and kind; the ledger keeps the box text as `example_text` | unchanged signatures and ids; the example text carries the note when the name was not read |
| `crash.collect` (the watcher's `_collect`) | the instance, the pid and a since-time; never the box | unchanged |
| the queue entry (`notify`, `entry_text`) | the box text | the launch-record name or "unknown image" plus the note |
| the HI runner (`actions.box_check`, `judge`, `phase`, `sweep`, through `marker_dialog`) and the waits (`check`, `turns`, `lua`, `wait_for`) | `summary` (the box text) and `dialogs[].process` | the same name and note, plus `process_source` / `process_read` |

Tests (`tools/harness/tests/test_dialogwatch_boxless_crash.py`): offline `crash_box` in its three forms, `launch_exe`'s
order (state for this pid and creation time, then the instance; a reused pid falls through), a Fakes watcher with no
handle and no launch record (box, marker and `marker_dialog` say "unknown image", no "CivilizationV" in the box) and with
a state file, the late-start line with a stand-in reader before the first poll and none after a poll; real: `_late_watch` starts the real detached watcher after its python
stand-in has ended (the test's Popen keeps the process object), once with `instance.json` `exe`
`CivilizationV_Tablet.exe` (the box names it, from instance.json) and once with a state file naming the stand-in's own
image (the box names `python.exe`); both require `dialog_watch_late_start` with `handle` true, `image_read` false and
`why` `UNREAD_ENDED`, 0 polls, and the unchanged signature. Acceptance runs:
`research/06-tooling-and-workflows/evidence/0608/acceptance_0608.txt`.

### 11.10 A terminated EXE that has not ended: the bounded end wait (ticket 0700, 2026-11-23)

**The fault [C, code; L, logs].** Until 0700, `launch._terminate_after_gates` sent TerminateProcess, waited 15 s
(`psutil.Process.wait`), logged `stop_kill` "still alive 15 s after TerminateProcess", called psutil's `kill` (on
Windows the same `TerminateProcess` again: psutil 5.9.0 `_pswindows.Process.kill` and `send_signal(SIGTERM)` both call
`cext.proc_kill`) and waited 15 s more; a process not ended by then raised `psutil.TimeoutExpired` out of `stop()`, so
the CLI ended with a traceback (exit 1), the state file kept the pid, the children were not waited for, and a driver's
next step (`lay.py restore`) ran into files still in use (0642's live smoke, c5h-c, 2026-11-23 06:20; 4984 on c5h-a).

**What "still alive" was [L, harness logs and server.log].** The EXE was not exiting by itself: exit code 15 is
psutil's TerminateProcess code (also in `research/03-dll-engine-boundary/error-report-exe-crash.md`), set at once,
while the process object stays unsignalled until the process's teardown ends. The server's vpipc waits on that object,
so its log dates the real end: c5h-new-b pid 30908, TerminateProcess 06:25:42, server.log `[414.706] vpipc ... client
pid 30908 exited (code 15)` with the server started 06:19:59.9, so the EXE ended at 06:26:54.6, 72 s later; the
server then ended itself (exit 3, PeerDied) and was gone at 06:27:09. c5h-c pid 30200 (0642): TerminateProcess
06:20:05, the dialog watcher saw the end at 06:25:26 (321 s), the server gone 06:25:51.
Scan of every harness log of the pool (the default instances root (`C5_DEFAULT_INSTANCES_ROOT`) and `F:/c5-instances`, script
`C:/c5-64bit-work/0700/scan_stops.py`, run 2026-11-23 07:00): 774 stops reached TerminateProcess since 2026-11-17;
the EXE end as the watcher saw it: median 1 s, p90 2 s, p99 26 s, maximum 321 s; 37 took over 5 s, 17 took the kill
path. The slow ones cluster in loaded periods: 2026-11-18 15:29-18:08 (c5h-d..g, 17-34 s) and 2026-11-23 06:16-06:52
(seven stops on c5h-a, -b, -c, -new-a, -new-b, 18-321 s), with five or six games and builds on the workstation. The
server's own end after the EXE's took up to 35 s (c5h-d 2026-11-18 17:37: EXE +26 s, all gone +61 s).

**Why the end takes that long: not known [I].** Unlikely [I, Windows' documented semantics and the logs]: handles held
by other processes (a process object is signalled when its last thread is gone, whoever holds handles; the dialog
watcher and the server hold one each the whole run, also in the 737 fast stops); the server's pipe, the proxy and the composite's threads are user
mode and are stopped by the terminate. What is left is the kernel side of the teardown: a thread that cannot leave a
kernel wait (a driver call, an uncancellable I/O) until it completes, or the release of a large GPU process's video
memory and address space. Both EXE (32-bit, D3D11, the composite's shared textures) and server (64-bit, D3D11) are
GPU processes, both took long on the same loaded mornings, and the stand-in below shows that the handle table is
already closed (0 handles) while the address space (and so every mapped image and DLL, the composite included)
stays until the end [L]. To tell the candidates apart, `stop()` now logs a sample every 15 s while it waits
(`stop_end_wait`: the EXE's and its children's thread count, thread states with wait reasons, handles, private and
working-set MB, from one `NtQuerySystemInformation(SystemProcessInformation)`, no handle opened); the next slow stop
names the waiting threads' reasons. A kernel stack (ETW, a kernel debugger) needs administrator rights and was not
taken.

**The fix (launch.py, CONTRACT.md 5).** One TerminateProcess, then a wait until the process object is signalled
(`dialogwatch.ExitCodeReader`'s handle, `launch.process_ended`), up to `TERMINATE_WAIT_S` = 600 s from the terminate,
a `stop_end_wait` line every `END_WAIT_LOG_S` = 15 s, `stop_terminated` with `exe_end_s` and `exit_code`; only then the
children's wait (`child_wait_s`, 60 s, unchanged). At the bound: `stop_lingering` logged, `StopLingering` (a
`StopTimeout`, CLI exit 4) with one line starting `LOUD:`, no second terminate and no kill; the state file keeps the
pid and `terminate_sent` {pid, create_time, at, reason}, written before the terminate. `stop()` run again on that
pid and creation time resumes the wait without the gates (`stop_resume_terminated`; the threads were stopped, nothing
can write a save any more). `launch.still_running(inst)` = `children.leftovers` plus the recorded EXE, the last stopped
EXE and every recorded child while `process_ended` says not ended; `server/evidence/0600/lay.py` prep and restore call
it first and refuse with exit 4 and one line naming each process ("... have not ended ... Nothing was changed. Wait
until they have ended (`cli.py -i NAME stop` returns only then, or check `cli.py -i NAME leftovers`), then run restore
again."). psutil 5.9.0 agrees with the kernel on a lingering process [L, probe 2026-11-23 07:10]: `pid_exists`,
`Process()`, `exe()` and `process_matches` say alive until the object is signalled, so the other `process_matches`
checks of `stop()` needed no change.

**The stand-in reproduction [L, 2026-11-23 07:05-07:17].** `tools/harness/tests/lingering_standin.py`: a windowless
sleeping python (the target) and a second python that attaches to it as a debugger (`DebugActiveProcess`), lets the
attach events through and then stops answering for `--hold` seconds; a debugged process's exiting threads wait for
their debugger, so TerminateProcess sets exit code 15 at once and the process ends only when the holder answers. Old
step (`--old`, the pre-0700 code verbatim), hold 40 s: raised `TimeoutExpired: timeout after 15 seconds` after 30.0 s,
the target not ended when it returned, ended at 40.1 s. New `stop()`, hold 40 s: returned after 40.2 s with `stopped`
true, `exe_end_s` 39.64, `exit_code` 15, the target ended when it returned; two `stop_end_wait` samples showed 3
threads `Waiting:Executive`, 0 handles, 11.8 MB working set. Tests: `tools/harness/tests/test_core_stop_lingering.py`
(the stand-in lingers; stop waits and logs; the bound raises LOUD with the state kept, `still_running` names it, a
second stop resumes without running the gates; `process_samples` against psutil; `process_ended` for gone and reused
pids; `lay.py restore` and `prep` refuse with exit 4, one line, `lay.json` unchanged, for both lay.py copies; and do not
refuse once the EXE has ended).

**Live pass [L, 2026-11-23 07:53-08:03, c5h-c, EXE 4390913, the pinned layout `x64-c29442e8-p58724e0` laid by
`server/evidence/0600/lay.py`, Tiny save, turn 18].** `cli.py stop` exit 0 after 29.6 s: `stop_terminate` 08:02:10,
`stop_terminated` `exe_end_s` 2.63, `exit_code` 15, no sample needed; server.exe ended inside the children wait, no
orphans; `lay.py restore` run right after the stop: `restore ok`. The machine was quiet then (after a hold), so no slow
end; driver `research/06-tooling-and-workflows/evidence/0700/live_stop_0700.sh`.

**What a driver chain does with the new exit codes [C, the drivers' code].** `cli.py stop` exit 4 (StopLingering, or
the old StopTimeout) and `lay.py` exit 4 ("refused", nothing written) leave the instance as it was: the claim stays,
nothing in `pool.py` marks an instance broken (it has no such state), and the restore is not skipped but deferred: the
smoke drivers' `end` steps (`server/evidence/0455/4983/smoke_4983.sh`, `0455/4985/smoke_4985.sh`,
`0615/4914/smoke_4914.sh`, which call `research/02-exe-internals/evidence/4967/lay.py`) log "lay.py restore exit 4
(... run restore again.)" and end with exit 5, and running `end` again after the EXE has ended does the whole
restore. Before 0700 the same chain wrote part of the restore (the composite unlink failed on a mapped DLL; once
`set-proxy --original` refused after the backup step had put the original DLL back: W84 on c5h-b, 2026-11-23,
MC-B #11348). Acceptance record:
`research/06-tooling-and-workflows/evidence/0700/acceptance_0700.txt`.

## 12. First live Goldberg instance (1108, 2026-11-10)

First launch of the real game through the harness: instance `c5h-a` (re-provisioned 03:22-03:25 with 1107's
instance-aware DLL `5fdf3432...`, 17,452,544 B, in the copy's `MODS\(1) Community Patch`; gbe = pure_vp's debug
`steam_api.dll` `bd897b3c...`), save `C5H-VP8P-HUGE_0350` (VP 5.4.8 mods form, Huge 128x80, turn 350), launched
with `launch.launch(inst, wait_window_s=240)` = `bg_helper32 launch --show 4 --inject focus_guard.dll --early`,
cloak + off-screen park, binaries built from this branch's `native/` at 03:29. Steam client running (steam.exe pid
4972 since 2026-11-08), untouched. Run 03:36:34-03:46:27 EST. Evidence: `tools/harness/evidence/1108/`
(`run-result.json`, `run-log.txt`, `ceg-*.json`, `shim.log`, `shim-summary.txt`, `memory.csv`, `watch.jsonl`,
`steam-log-excerpt.txt`, `lua-log-stamped.txt`, shots at 0.25; the driver `run_1108.py` and the read-only
handle observer `ceg_observe.py` are there too). Answers the "not verified" list at the end of section 11.

**`--early` with the CEG EXE works [L].** Helper exit 0, `RESUMED`, `redirect_state = isolate_state = 1`,
`mods_attached 38/69` at guard install (not yet attached: the EXE, `WS2_32`, `PSAPI`, `COMDLG32`, `d3dx9_42`,
`WINHTTP`, `zlib1`, `CvLocalization...`, ...). No Steam relaunch: `SteamAPI_RestartAppIfNecessary(8930)` returned
to the game (gbe), PID 7096 stayed the same from creation to stop; no new Steam processes (same PIDs before and after).

**Who answers `STEAM_DRM_IPC`: the real Steam client (steam.exe) [L].** Seen without elevation by enumerating
`\Sessions\5\BaseNamedObjects` (`NtQueryDirectoryObject`, query only) and the system handle table
(`NtQuerySystemInformation(64)`), naming candidate handles by duplicating them with **zero access** into the
observer and calling `NtQueryObject(ObjectNameInformation)`; nothing was opened by name, created or signalled:
- Before any game ran (03:35), `STEAM_DRM_IPC` (Section), `STEAM_DIPC_CONSUME` and `SREAM_DIPC_PRODUCE`
  (Semaphores) already existed and **steam.exe (4972) held all three** (access 0xF0007 / 0x1F0003 / 0x1F0003).
  So in this session the EXE opens Steam's existing objects rather than creating them (1102 section 4.6 reads the
  `CreateFileMapping`/`CreateSemaphore` calls as creation; they return the existing objects here).
- During start-up steam.exe also held `STEAM_TERM_EVENT_7096_9075701`, the per-PID event the game creates
  (FUN_0061F110); both steam.exe and the game held it. gbe (inside the game) holds none of these names.
- `steamservice.exe` (24300): `OpenProcess(PROCESS_DUP_HANDLE)` denied (error 5), not visible without elevation.
- Not seen: the game's own handles to the three handshake objects (held only briefly; the observer loop took ~4 s
  per round over the game's handle table) and the request/ack traffic itself.
Consequence (CONTRACT 7): instances depend on the Steam client running; launches stay serialised.

**Start-up timing [L]** (gate_acquired = 03:36:35.30 = 0 s): created +0.69 s; guard installed and self-tests passed
03:36:37.27-.29 (+2.0 s); helper returned / main thread resumed +2.56 s; gbe `SteamAPI_RestartAppIfNecessary` at
gbe clock 29,623 ms (aligned through gbe's `SHGetFolderPathW(CSIDL_APPDATA)` at 03:37:07.199 in the shim log: about
03:37:07.17, +31.9 s); `CreateMutexA("Local\Civ5App")` -> `Local\Civ5App_c5h-a`, lasterror 0, at 03:37:07.367
(+32.1 s). So **the CEG handshake (between RestartApp and the mutex) took at most ~0.2 s; the ~29 s before it pass
before the EXE's main is entered** (gbe logs nothing between resume and RestartApp; cause not identified [I]; a past
pure_vp run on 2026-10-14 logged 13.3 s for the same span). Documents asked 03:37:07.733, LocalAppData 03:37:12.647;
main window +38.2 s (03:37:13.5, gate released); first front-end Lua.log lines +114.4 s (03:38:29.7); mods enabled
and activation started at once; the DLL logged its instance names at 03:38:52; mods active (MainMenu re-executed)
+179.5 s (03:39:34.8). Load (see below) clicked 03:42:22, **first Lua answer 03:43:26.5 (64 s for the huge save)**;
stop called +569.3 s, process gone +591.9 s.

**Isolation [L].**
- Shim log (231 lines): the EXE asked `SHGetSpecialFolderPathW` CSIDL 5 once (caller `CivilizationV_DX11.exe+0x41b1df`,
  the return address after 0x0081B1D9 of 1102) and CSIDL 0x1C once (`+0x41b2f9`), both redirected; its
  `SHGetFolderLocation` PIDL calls (`+0x27a4e4`, CSIDL 0x1A/0x24/0x26/0x2A and 5 "NOT REDIRECTED", 2 each) are the
  system-info report of 1102. **`steam_api.dll` asked only once, `SHGetFolderPathW(CSIDL_APPDATA)` from
  `+0x14d207` (passed: `%USERPROFILE%\AppData\Roaming` [path scrubbed 2026-11-17]), not in its DllMain** (03:37:07.199, during `Steam_Client`
  construction); gbe then used `local_save_path` (its log: "global settings will be ignored since local save is
  being used"). NVIDIA's driver DLLs asked `FOLDERID_LocalAppData` 168 times (`NvMemMapStorage.dll` 89,
  `nvwgf2um.dll` 79; redirected; nothing was written there), `nvapi`/`nvwgf2um` CSIDL 0x24, `nvspcap` 0x8023 (passed).
- gbe accepted the absolute `crash_printer_location` (`...\c5h-a\run\gbe\crashes.txt`) and `GseSavePath` with its
  trailing `\`; `<inst>\gse\` received `settings\configs.user.ini` (148 B, 03:37:07); `%APPDATA%\GSE Saves\8930\
  playtime.txt` kept its 2026-10-14 20:15:41 mtime; nothing else in `GSE Saves` changed.
- `provision.py snapshot` before (03:20:54) and after (03:47) the run: **no difference** in the owner's top level, MODS
  (1)/(2)/(3a), `ModdedSaves\single`, all of pure_vp and the Steam top level, nor in the hashes of the owner's config.ini,
  GraphicsSettingsDX11.ini and CP DLL. `HKCU\Software\Firaxis\Civilization5` export before and after:
  byte-identical (`LastKnownPath` = Steam install, `LastKnownExe` = DX11). The owner's `%LOCALAPPDATA%\My Games\Sid Meier's
  Civilization 5` unchanged (mtime 02:03:50, empty); the instance's own `LocalAppData\My Games\...` appeared 03:37:12.
- **The EXE writes three shell shortcuts into its install folder at every start** (`Config.ini.lnk`, `Logs.lnk`,
  `Saves.lnk`, 03:37:12.98-13.03), pointing into the user folder it was given (here the instance's). That is where
  pure_vp's shortcuts into the owner's tree came from (headless-and-parallel-testing.md 3.6). They are ordinary files,
  not file-system links; `provision.py verify` reports them as "excluded file present" after a run.
- Kernel objects: `Local\Civ5App_c5h-a`, `Local\VPLuaExec_c5h-a`, `Local\VPLuaExecDone_c5h-a` existed during the
  run; the legacy `Local\VPLuaExec` did not. (Legacy `VPMemSnapshot`/`VPMemSnapshotDone` events existed in the
  session before the launch, from some other process.)

**The DLL's instance names [L].** `install\crashlogs\instruments.log`: one line,
`2026-11-10 03:38:52  pid 7096  instance names: VP_INSTANCE=c5h-a; Local\VPLuaExec_c5h-a Local\VPLuaExecDone_c5h-a
Local\VPMemSnapshot_c5h-a Local\VPMemSnapshotDone_c5h-a TurnByTurn_c5h-a` (one DLL load, at mods activation).
`queueguard.log`: "UI message queue guard active". The channel answered on `Local\VPLuaExec_c5h-a` through
`tools/harness/lua.py`: `return Game.GetGameTurn(), Game.GetActivePlayer()` = 350, 0; round trips 0.10-0.17 s;
`Map.GetGridSize()` 128x80, `Map.GetNumPlots()` 10,240.

**Front end in a fresh instance [L].** The instance's mods database is empty, so `tools/harness/frontend.py`
(1108) put the vp-dll-dev skill's `MainMenu.lua`/`FrontEnd.lua` into the copy's `Assets\UI\FrontEnd\` (pure_vp's
copy has a different, Vox Deorum-patched `MainMenu.lua`, `5c525c54...`; its `FrontEnd.lua` equals the skill's,
`35dd0987...`; its `LoadScreen.lua` equals Steam's, `b55f4563...`; the skill has no LoadScreen asset). Enabling and
activating CP/VP/EUI worked unattended (65 s). **The auto-load did not (fixed in 1114, section 12.1: `frontend.prepare` falls back to the save's absolute path; the empty list's cause is still open): at every MainMenu execution
`UI.SaveFileList` (both lists) returned 0 files ("no save files found"), although the save was in
`ModdedSaves\single`**; the Load Game screen, opened by posted clicks (harness `input.click`, cloaked, no focus),
listed it at once and loaded it. Cause not established [I: the owner's `Saves\single` is never empty, so the skill never
met this; possibly the list MainMenu sees is the plain `Saves\` one]. Not tried: seeding the save into
`Saves\single` as well. `frontend.take_begin_click` then cleared the load-screen pause (paused by player 0; unpaused
after one check). `frontend.restore` put both files back (hashes = originals = install manifest).

**Focus [L].** 0 foreground and 0 visible polls: harness `guard.watch` in 30 s chunks (18 chunks, 25,162 polls) plus
the driver's own 50 ms poller from before the launch (11,015 polls; it also checked every top-level window of the
PID). The main window stayed cloaked at (-4680, 0), client 2554x1411. No assert or crash dialog (2 s poll of
`crash.dialog_state`).

**Memory [L]** (`memory.csv`, 31 rows every 5 s, 03:43:29-03:46:04, idle at the human turn): private 2,083-2,129 MB,
working set 1,794-1,835 MB, committed 2,419-2,465 MB, largest free block 1,091-1,143 MB, free below 2 GB 31 MB.

**gbe log growth [L].** One `STEAM_LOG_1643608527.log` in `install\`: 103,186,471 B at the first Lua answer
(+6.9 min, mostly `GetISteamApps`/`RunCallbacks` lines), 113,027,863 B 156 s later (63 KB/s idle in game),
114,507,211 B at stop; `clean-logs` removed it. With this debug gbe build one start writes ~100 MB; 1112's release
build (D16) removes it.

**Stop [L].** `launch.stop(inst, human_turn_active=turns.stop_readiness)`: newest save unchanged 11.3 s with the human
turn active (turn 350), then TerminateProcess; the load wrote no autosave; no turn was played.

Not verified: the cause of the ~29 s before main; the game's own handles to the handshake objects and the direction
of the request/ack (only handle ownership was seen); `steamservice.exe`'s role; the unattended auto-load (failed,
above; works since 1114 via the absolute path, section 12.1); two instances at once (1109); a start with the Steam client closed (not tried: Steam is never closed).

### 12.1 Wrapper route, source-built gbe, unattended load (1114, 2026-11-10)

Instance `c5h-a` (1107 DLL `5fdf3432...`, `dll_names` = instance-aware by provision.py's `lua_channel_names`), save
`C5H-VP8P-HUGE_0350`, Steam client running (steam.exe pid 4972, untouched, same PIDs before and after). Binaries
built from this branch's `native/` at 04:00. Run 04:09-04:23 EST. Evidence: `tools/harness/evidence/1114/`
(`run-result.json` and `run-log.txt` = attempt 3, `*-attempt1/2*` = the two aborted attempts, `shim.log`,
`attempt1-shim.log`, `ceg-startup.json`, `lua-log-stamped.txt`, `memory.csv`, `watch.jsonl`, shots at 0.25, pre/post
listings, registry exports, snapshot diff, verify before/after; driver `run_1114.py`).

**Preparation [L].** `native/wrapper.py install %USERPROFILE%\c5-instances\c5h-a\install` (25 s; built the wrapper
for pure_vp's debug gbe `bd897b3c...`: 1,265 exports, 1,264 stubs, 1 data export; wrapper `0af8de50...`), then
`provision.py set-gbe c5h-a --gbe-dir %USERPROFILE%\c5-instances\_tools\gbe-release-x32` (behind the wrapper:
`steam_api_gbe.dll` `bd897b3c...` -> `665bb50b...`, `steamclient.dll` `ad6838d4...` -> `5ff5222d...`,
`c5_wrapper.json` real_sha256 updated), then `verify` ok (47 s; the wrapper listed under `wrapped`).

**Import-time route on the real CEG EXE: accepted [L].** `launch.launch(inst, route="import")` =
`bg_helper32 launch --show 4 --import-time -- install\CivilizationV_DX11.exe` (no `--inject`, no PEB write, no
ntdll hook). Helper exit 0, `IMPORT wrapper=...\install\steam_api.dll import_descriptor_index=9` (the helper counts
from 0 over all descriptors as it reads them; 1113's "8th" is the same entry), `RESUMED`, `redirect_state =
isolate_state = 1`, self-test `docs 15/15 local 15/15 ... Local\Civ5App->Local\Civ5App_c5h-a: mapped`. At guard
install 48 of 63 modules had attached; not yet attached: the EXE, `steam_api.dll` (the wrapper itself), `COMDLG32`,
`gdiplus`, `MSIMG32`, `COMCTL32`, `dbghelp`, `d3d9`, ... (descriptors after steam_api, as 1113 predicted from the
import order) [L]. Shim log: `handshake: go received` 04:13:09.224, `wrapper: ... loading steam_api_gbe.dll`, `1264
exports resolved` 04:13:09.233. gbe's only folder request is `SHGetFolderPathW(CSIDL_APPDATA)` from
`steam_api_gbe.dll+0x4e5e6` (passed), so gbe runs under its new file name. Two launches on this route (attempt 1
pid 53948, attempt 3 pid 57388), both accepted. **After this run `launch.launch(route=None)` defaults to `"import"` for an instance with the wrapper
(`install\c5_wrapper.json`, `launch.default_route`); `"early"` stays the documented fallback and the default
without it (MC accepted the route, 2026-11-10).**

| Start-up after gate_acquired (attempt 3, 04:13:08.196) | s |
|---|---|
| created / helper returned (resume) / guard confirmed | +0.93 / +1.23 / +1.43 |
| wrapper loaded gbe (shim clock) | +1.03 |
| EXE's system-info `SHGetFolderLocation` round 1 / round 2 (caller `+0x27a4e4`) | +12.1 / +24.7 |
| gbe `SHGetFolderPathW(APPDATA)` (Steam_Client construction) | +24.84 |
| `CreateMutexA("Local\Civ5App")` -> `Local\Civ5App_c5h-a`, lasterror 0 | +24.85 |
| main window (gate released) | +36.7 (attempt 1: +28.3) |
| first front-end Lua line / mods activation started | +106.6 (attempt 1: ~+70) |
| mods active (activation 91 s) and load issued | +197.7 |
| first Lua answer (load 69 s) | +266.4 |

So the span between the resume and gbe's Steam_Client construction is ~22-24 s on this route (1108, `--early`, debug
gbe: ~29 s); the EXE's PIDL system-info report runs twice in it, ~12.5 s apart [L]; its cause is still not
identified [I]. CEG [L]: before the launch steam.exe (4972) already held `STEAM_DRM_IPC`, `STEAM_DIPC_CONSUME`,
`SREAM_DIPC_PRODUCE`; during start-up both steam.exe and the game held `STEAM_TERM_EVENT_57388_12024821`; the game
held none of the three handshake objects at the poller's resolution (one round can take several seconds; the
mutex was first seen by the poller in a round that began 6 s before the shim's timestamp). Observe-only, as 1108.

**Source-built gbe (1112's release build) [L].** No `STEAM_LOG_*.log` was written in `install\` (0 files before,
during and after; `clean-logs` deleted 0 B). gbe still used the instance's settings: it asked `CSIDL_APPDATA` once and
then wrote nothing under `%APPDATA%\GSE Saves` (`8930\playtime.txt` kept 2026-10-14 20:15:41; listing in
`postflight.txt`), which it does only with `local_save_path` from `install\steam_settings\configs.user.ini` [I: the
release build logs nothing, so the read itself is inferred from that behaviour]. `<inst>\gse\` was not written this
run (still `settings\configs.user.ini`, 148 B, from 1108 at 03:37:07): **"saves in gse" is not demonstrated**,
because nothing in a Civ 5 load and one turn asks gbe to store anything (no cloud save, no stats written; the game's
own saves go to the instance's `ModdedSaves\`) [I].

**Unattended save load: works [L], cause of the empty list still open.** `frontend.prepare` (1114) adds a harness
block to the skill's patched MainMenu.lua (after `getSaveToLoad`): it prints what each list call returns and, when
the skill's search finds nothing, loads the save the harness picked by its **absolute path**. Lua.log, attempt 3,
after mods activation (3 mods active):

```
MainMenu: c5h: SaveFileList(SP, autos=false, true) = 0      (also autos=false/false, true/true, true/false = 0)
MainMenu: c5h: header(abs)=true CanLoadSavedGame(abs)=0
MainMenu: vp-dll-dev: no save files found
MainMenu: vp-dll-dev: selected save %USERPROFILE%\c5-instances\c5h-a\Documents\My Games\Sid Meier's Civilization 5\ModdedSaves\single\C5H-VP8P-HUGE_0350.Civ5Save (c5h absolute path) [path scrubbed 2026-11-17]
MainMenu: vp-dll-dev: loading C:\...\C5H-VP8P-HUGE_0350.Civ5Save
MainMenu: vp-dll-dev: this launch already issued its load - not issuing a second one     (3 s later, re-execution)
```

`PreGame.GetFileHeader` and `Modding.CanLoadSavedGame` accept the absolute path, and
`Events.PlayerChoseToLoadGame(<absolute path>, false)` loads it (69 s to the first Lua answer; turn 350, player 0,
128x80, 10,240 plots). No click was needed. What the 1108 question asked: `UI.SaveFileList` from MainMenu returns 0
for every argument combination (game type single player; third argument autosaves true/false; fourth argument
true/false) while 3 mods are active and the file is in `ModdedSaves\single`; the stock `LoadMenu.lua` makes the same
call (`UI.SaveFileList(g_FileList, g_GameType, g_ShowAutoSaves, true)`, line 545) and listed the save in 1108. So the
arguments are not the cause [L]; the MainMenu context's list is empty at that moment in a fresh instance, and why
(a cache the Load screen refreshes, or a list that only the Load screen's context fills) is not established [I].
Seeding the save into `Saves\single` was not tried (not needed).

**One end turn [L].** `turns.end_turn` forced the end after 6 tries (5.4 s).
`turns.wait_turn(turn_from=350, stall=90, answer=3, answer_fn=input.answer_dialog)`: turn 351 active after 314.1 s,
with 3 answered stalls: Montezuma's peace offer (the click at client (0.49, 0.977) hit REFUSE), Bismarck's missionary
warning (bottom button "go where they please"), Bismarck's follow-up ("Your religious fervor is unsettling", single button). Most of the 314 s is the 90 s stall
threshold times three; the AI turn itself is shorter [I]. After the turn: a tech popup (Refrigeration) in
`after-turn-scale025.png`. Autosaves in the instance: `auto\AutoSave_Post_0350 AD-1930` (04:17:45) and
`auto\AutoSave_0350 AD-1930` (04:22:53).

**Stop, isolation, focus [L].** Attempt 3 stopped by `launch.stop(inst, human_turn_active=turns.stop_readiness)`:
newest save (`AutoSave_0350 AD-1930`) unchanged 11.6 s with turn 351 active, then TerminateProcess (23.5 s after the
call). `frontend.restore` put MainMenu.lua back by hash. `provision.py snapshot` before (04:04) and after (04:24):
**no differences**; `HKCU\Software\Firaxis\Civilization5` export byte-identical; `verify` after: ok, no errors, no
changed mutable files. Focus: 0 foreground and 0 visible polls (harness watch, 19 chunks, 26,056 polls; driver
poller 11,223 polls); no assert or crash dialog. Memory (`memory.csv`, 70 rows, 04:17-04:23, turn 350 -> 351):
private 2,020-2,179 MB, working set 681-1,455 MB, committed 2,346-2,506 MB, largest free block 1,046-1,143 MB.
The DLL logged its instance names once per start (`instruments.log`, 04:10:48 pid 53948, 04:15:36 pid 57388).

**Incident: a forced stop in the front end (attempt 2, 04:11:21) [L].** Time line: attempt 1 (driver pid 29456)
launched pid 53948 at 04:09:15; its Lua.log tailer read 1108's still-present Lua.log (the game truncates it only at
~+28 s) and set "nothing to load", which would have fired the click fallback; I stopped the driver (not the game) at
04:09:47. Attempt 2 (an attach driver) failed at once: `crash.baseline` raised `PermissionError` on
`install\CvAssert.log`, which the running game holds open (`tools/harness/crash.py` line 114 in `baseline`,
`_sha(p)` -> `backup.sha256_file`, `tools/harness/backup.py` line 113); its `finally` path called `launch.stop`, which
refused (Lua channel silent), and then **`launch.stop(force_unsafe=True)`, which terminated pid 53948 at 04:11:21
while the front end was activating mods** (Lua.log last line 04:11:03, `stopwatch.log` 04:11:13; `cache\*.db` written
until 04:11:02). No save was being written (no load had been issued; newest save unchanged). Consequence: the
instance's mods/core databases may have been left mid-rebuild; attempt 3's activation rebuilt them (91 s, 3 mods
active, load and turn normal), so no lasting effect was seen. This is against the stop rule (stop only with
`stop_readiness`). The driver no longer has any forced-stop path; the coordinator ruled (2026-11-10) that no code
path may call `force_unsafe=True` automatically and that a game in the front end is escalated, not forced; the
stop-safety rules become task 1115.

Not verified: why MainMenu's `UI.SaveFileList` is empty; gbe writing into `<inst>\gse\` (nothing asked it to); the
cause of the ~22-24 s before Steam_Client construction; the route on a second instance at the same time (1109);
`frontend.prepare`'s block with a MainMenu other than the skill's (it is skipped when there is no `getSaveToLoad`).

### 12.2 Answering AI diplomacy screens (1116, 2026-11-10)

**Result [L] (c5h-a, 2026-11-10 06:21-06:37 EST).** With the in-game handler armed, AI turns on `C5H-VP8P-HUGE_0350`
took **37.9 / 36.8 / 29.2 s (mean 34.6 s)**, against **336.1 / 126.8 / 28.2 s (mean 163.7 s)** with the stall path
alone (stall 90 s, then the located click). The turn with screens went from 336 s to 38 s. All 4 screens in phase B
were answered in game, each on the first try, 2.0 s after the message (1 s delay + one frame); no stall and no click
in phase B. Code: `tools/harness/diplo.py`, integration in `turns.wait_turn` and `checklist.py`, tests
`tools/harness/tests/test_diplo.py`; usage `tools/harness/README.md` "Diplomacy handler (1116)". Evidence:
`tools/harness/evidence/1116/` (`run-result.json`, `run-log.txt`, `lua-log-stamped.txt`, `watch.jsonl`, shots at 0.25,
pre/post snapshot, registry exports, verify before/after; driver `run_1116.py`).
**Why the channel cannot see the screen, and where the screen comes from [C].** In single player the DLL sends an AI's
diplomacy message to the human at once, inside the AI's diplomacy pass: `CvDiplomacyRequests::SendRequest`
(`CvDiplomacyRequests.cpp` 507-540, instance-aware fork) calls `Send` when the target is the active player and the game
is not network multiplayer, and `Send` (424-429) calls `gDLL->GameplayDiplomacyAILeaderMessage`, which fires the Lua
event `Events.AILeaderMessage(iPlayer, iDiploUIState, szLeaderMessage, iAnimationAction, iData1)`. The request stays
active (`m_bRequestActive`) until the EXE reports the leader view left (`CvDllPlayer::ActiveDiplomacyRequestComplete`
-> `ActiveRequestComplete`, 659-663). `CvDiplomacyRequests::DoAIDiplomacy` (611-656, "don't process while a modal
dialog ... or another diplo or popup is up") runs only for hot seat (`CvGame::updateTimers`, CvGame.cpp 9273-9288).
So the 1114 stalls are the AI turn waiting for the human to leave the leader view; the Lua channel (served from
`CvGame::update`) is silent meanwhile (1110, 1114).

**Which screen handles which state, and its own close/refuse function [C]** (the files the instance runs, c5h-a MODS
and install copies, read only 2026-11-10):

| Context (Lua state) | File in effect | Takes AILeaderMessage states | Close / refuse function |
|---|---|---|---|
| `LeaderHeadRoot` | EUI compat `(3a) .../LUA/LeaderHeadRoot.lua` | `DEFAULT_ROOT`, `WAR_DECLARED_BY_HUMAN`, `PEACE_MADE_BY_HUMAN`; it also queues itself (`PopupPriority.LeaderHead`, line 348) for every other state with its options hidden | `OnClose()` 357-363: `DequeuePopup`, `SetLeaderHeadRootUp(false)`, `RequestLeaveLeader`; dequeued by `Events.LeavingLeaderViewMode` (367-378) |
| `DiscussionDialog` | VP `(2) Vox Populi/Core Files/Overrides/DiscussionDialog.lua` (overrides CP's) | `DISCUSS_*`, `BLANK_DISCUSSION*`, `AI_DECLARED_WAR`, spy, `STOP_CONVERSIONS`, `STOP_DIGGING`, `DISCUSS_AI_REVOKE_VASSALAGE` (190-255) | `OnBack(bForce)` 783-794: with `bForce` it closes even where the screen hides Back (`bHideBackButton`, the AI forces an answer): `DequeuePopup`, `LeaderPanel` hidden, `RequestLeaveLeader`. No `SetUpdate` in the file |
| `DiploTrade` | `Assets/DLC/UI_bc1/Improvements/DiploTrade.lua` including EUI compat `TradeLogic.lua` | `TRADE*`, `HUMAN_DEMAND` (TradeLogic 277-286) | `OnBack(iType)` 517-614 (Cancel button): for `TRADE_AI_MAKES_DEMAND` / `_REQUEST` / `_GENEROUS_OFFER` sends `FROM_UI_DIPLO_EVENT_DEMAND_HUMAN_REFUSAL` / `REQUEST_HUMAN_REFUSAL` and stays open in plain `TRADE` state; called again (or for `TRADE_AI_MAKES_OFFER`) `DoTradeScreenClosed`, `DequeuePopup`, `RequestLeaveLeader`. Accepting would be `OnPropose` -> `UI.DoFinalizePlayerDeal(..., true)` (1123-1175), never called |

**The luaexec environment facts the handler relies on [C]** (`CvLuaSupport.cpp`, `LUAEXEC_RUNNER`): `vp` is ONE table
in the Lua registry (`VP_LUAEXEC_PERSIST`, lines 499-500, set into every chunk's environment at 627), so it is shared by
all states and survives as long as the Lua VM; a chunk's environment is `setmetatable({}, {__index = base,
__newindex = base})` (615) over the target state's environment found in `G.Threads` (502-546), so a global assigned in a
chunk lands in that state and functions defined in a chunk resolve the state's globals (`OnBack`, `ContextPtr`)
through it. Whether the Lua VM (and `vp`) survives a game load is not known [I]; the handler is built for both cases.

**Design.** `diplo.arm(inst)`: one chunk in `LeaderHeadRoot` (an `AILeaderMessage` listener that logs and marks a screen
pending, and an `Events.LocalMachineAppUpdate` per-frame ticker that answers 1 s later through the visible screen's own
function: `DiploTrade` `OnBack()` first, then `DiscussionDialog` `OnBack(true)`, then `LeaderHeadRoot` `OnClose()`,
retried every 1 s up to 4 times, then given up to the stall path), and one chunk each in `DiscussionDialog` and
`DiploTrade` registering their `OnBack`. Answers are chosen by which context is visible, not by the state number, so an
unexpected state still gets a close, never an accept. Generations (`vp.c5h_diplo.gen`) retire old handlers; a per-state
marker `C5H_DIPLO_GEN` makes `is_armed` false after a load; a handler touches a screen only if that screen's state is
still in `G.Threads`. The checklist pauses it around its own `leader_screen` step.

**The live run [L]** (driver `run_1116.py`, two launches of the same save on c5h-a: 1107 DLL, import route with the
1118 guard rebuilt at 06:18, wrapper `e312067d...` over the release gbe `665bb50b...`; each phase: `frontend.prepare`,
`launch.launch`, unattended load, `frontend.ensure_in_game` (dismissed the load screen once), 3 x `turns.end_turn` +
`turns.wait_turn(stall=90, answer=8, answer_fn=input.answer_dialog)`, `launch.stop(human_turn_active=stop_readiness)`):

| AI turn | A: stall path only (s) | screens in A (located click, 1117) | B: handler armed (s) | screens in B (in game) |
|---|---|---|---|---|
| 350 -> 351 | 336.1 (end_turn 5.8 + wait 330.3) | Montezuma peace offer: REFUSE; Bismarck missionaries: "Our missionaries and prophets go where they please."; Bismarck's reply: Back (3 stalls at 102 / 215 / 317 s) | 37.9 (5.6 + 32.2) | Montezuma `TRADE_AI_MAKES_OFFER`: `OnBack()` (refused); Bismarck `STOP_CONVERSIONS`: `OnBack(true)` |
| 351 -> 352 | 126.8 (0.2 + 126.5) | Bismarck denouncement: "Very well." (1 stall at 113 s) | 36.8 (0.2 + 36.5) | Bismarck `STOP_CONVERSIONS` again: `OnBack(true)` |
| 352 -> 353 | 28.2 | none | 29.2 | Bismarck `STOP_CONVERSIONS` again: `OnBack(true)` |

- **`Events.LocalMachineAppUpdate` fires while an AI leader screen holds `CvGame::update` [L]**: the ticker answered every
  screen 1.0 s after the message (the configured delay) and closed it within the next second (`seconds_to_done` 2.003 -
  2.007); 10,357 ticks over the phase. Its `print` lines in Lua.log carry the prefix `VP_MEMPROBE:` (the memory probe's
  thread), while the `AILeaderMessage` listener's carry `LeaderHeadRoot:`; so the per-frame event runs this handler on
  another Lua thread than the context's [L], and globals captured at arm time are what it uses (as designed).
- The Lua state names `LeaderHeadRoot`, `DiscussionDialog`, `DiploTrade` exist in the 1107-DLL instance with CP/VP/EUI
  [L]; all three arm chunks returned ok in 1.0 s together.
- **The AI turn continues after a forced `DiscussionDialog` close [L]**, without the human's answer: no stall followed.
  **But the game diverges from a refusal**: with the forced close Bismarck repeated `STOP_CONVERSIONS` every turn
  (350, 351, 352) and neither his reply screen nor his turn-351 denouncement came, while in phase A the located click's
  refusal brought the reply and, a turn later, the denouncement. So the close records no answer; the AI asks again
  next turn [I: from the two runs; the DLL's handling of an unanswered STOP_CONVERSIONS was not read]. For throughput
  this costs one more 2-s screen per turn; for tests that need the AI's real reaction, answer with the screen's
  refusing button instead (not done here: the handler never presses an answer button).
- The located click (1117) answered all 4 phase-A screens with the right refusing / neutral / close button
  (`refuse_deal`, `missionaries_refuse`, `back`, `denounce_neutral`; scores 0.986-1.0). Those turns still cost 90 s per
  screen because the click waits for the stall threshold; a shorter stall would cut phase A but not to phase B's level.
- Not exercised live: `pause`/resume (no checklist run), the re-arm after an in-game load, the give-up path, the
  missed-message scan, `Events.X.Remove` inside a dispatch (no generation change happened while armed).
- Isolation and safety [L]: focus poller 19,116 polls with 0 foreground and 0 visible hits; harness watch 32 chunks of 30
  s, all ok; guard counters (1118, offsets 1240-1264) at each phase end: `win_hooks` 271 (0x10F), `win_created` 4,
  `win_cloaked_create` 4, `win_cloaked_show` 0, `win_cloaked_watchdog` 0, `win_show_rewritten` 0, `win_fg_blocked` 0; no
  application dialog; both stops by `stop_readiness` (turn 353 active, newest save `AutoSave_0352 AD-1932` settled
  11.1 / 11.3 s); `frontend.restore` put MainMenu.lua back by hash; `provision.py snapshot` before (06:20) and after
  (06:37): no differences; `HKCU\Software\Firaxis\Civilization5` export byte-identical; `verify c5h-a` ok before and
  after. Load timing this run: launch to in game 2.5 min (A) and 2.3 min (B).
- **Two gaps seen in long runs (one-off 9043, 2026-11-13, c5h-b, huge save) [L].** (1) `turns.wait_turn` arms the
  handler only after `turns.py end`; right after a load the first AI turn can show a screen before it is armed ("diplo:
  core not armed: timeout"), and the game stays held when the click finder scores no button above 0.85 (run
  `endgame-418`, Spain's independence demand at turn 419; wait exit 6). `tools/validation/proxy_long.py` now runs `diplo
  arm` at load. One-off 9040 saw the same on the proxy route (c5h-a, huge t350: arming timed out at 10.02 s, three
  screens waited 78/154/223 s for the stall fallback; bug 0122). **Changed 2026-11-13 (bug 0122):**
  `turns.end_turn` now arms the handler before the end of turn, while the human's turn holds the game (`diplo=None`:
  on the real channel only; `EndTurnResult.diplo`), and `turns.wait_turn` retries a failed arming every 15 s (at most 8,
  only when the channel answers, timeout at least 10 s). Live 2026-11-13 08:10-08:17 (c5h-a, proxy route, huge t350, no
  pre-arm): the first `end` armed in 0.44 s, AI turns 24-31 s, no stall [L]; no leader screen came in those 3 turns.
  `proxy_long.py --no-pre-arm` runs without its own `diplo arm` at load. (2) On the stock route at turn 412, after about 120 screens answered in 62 AI turns, the handler left Isabella's
  `DIPLO_UI_STATE_DISCUSS_AI_REVOKE_VASSALAGE` `pending` (tries 1) and every later screen of that turn `not_shown`; the
  game was held (wait exit 6, scores 0.60-0.78). The same screen had been answered at turns 410 and 411. 9043 closed
  the five screens by hand (refuse, Back, Back, Refuse; the refusing answer each time) and the handler answered again
  from turn 413 (`diplo status`: armed gen 4). **Cause (bug 0126, read 2026-11-13 from
  `D:/c5-64bit-work/9043/runs/stock-350/run.log` lines 12829-12833) [L]:** `turns.py wait 412` began with
  `diplo: partly armed: DiscussionDialog timeout ..., DiploTrade timeout ... (gen 3, 15.11 s)`, where every earlier
  wait printed `already armed (gen 2)`. The wait's `is_armed` call timed out (5 s; a screen held the game), so
  `arm()` ran the core again; the core answered in a gap and made gen 3, and the two answerer chunks then timed out
  because the next screen held the game again. Gen 2's handler retired itself with Isabella's screen still `pending`
  (tries 1); gen 3 had only the root answerer, so `shown("discuss")`/`shown("trade")` were false and every later screen
  ended `not_shown`. A re-run of the handler's own Lua in a fake game reproduces exactly that log (`['pending',
  'not_shown']`, test below). **The Lua channel does not answer while a leader screen holds the game** [C]+[L]:
  `LuaSupport::PollExternalLuaRequest` is called at the top of `CvGame::update` (instance-aware fork `CvGame.cpp`
  1630, ahead of the `IsWaitingForBlockingInput` branch), and `CvGame::update` does not run while the screen is up:
  the two answerer chunks timed out here, the checklist's leader step saw the channel silent for its whole 12-s hold
  (12.5, `leader_screen` row), and `turn_state` stayed silent for the 60-s stall. So a close sent by the turn driver through the
  channel cannot reach the game during a hold; only the in-game ticker (`Events.LocalMachineAppUpdate`, which does run,
  1116) can close a screen then. **Fix (0126, 2026-11-13, offline only):** the core now finds the `DiscussionDialog`
  and `DiploTrade` answerers itself, in game: the environment in `G.Threads` whose `StateName` is the screen's (the
  same lookup as the runner's `findState`), its `OnBack` and `ContextPtr`; it returns their names as a 5th value and
  `arm()` sends no answerer chunk for them (`ArmResult.resolved`); the ticker looks again every 2 s for a missing or
  dead answerer, with the channel silent, only while its own `LeaderHeadRoot` state lives. A re-arm marks the older
  generation's pending screen `rearmed` (new log status; the new scan finds the screen again if it is still up).
  `turns.wait_turn` also retries a *partly* armed handler (core without an answerer), not only a failed arming (0122's
  retry rule). Still refuse/close only: `DiploTrade` `OnBack()`, `DiscussionDialog` `OnBack(true)`, `LeaderHeadRoot`
  `OnClose()`. Tests: `tools/harness/tests/test_diplo_rearm.py` runs `diplo.py`'s real chunks in a Lua 5.1 VM (`lupa`
  2.8, `lupa.lua51` with `getfenv`/`setfenv`, installed in the owner's user site-packages
  `%APPDATA%\Python\Python311\site-packages\lupa`, not visible under `python -I`; the file is skipped without it; this
  qualifies section 5's "no Lua interpreter ... checked only live" for offline checks) inside a fake game
  (`G.Threads` states, `Events`, a test-driven `os.clock`, accept traps). Not run live: no save shows the case on demand.
  Not done: an escalation when a screen's own close fails (the handler still gives up after 4 tries, then re-detects
  after its 30-s cooldown; the stall click stays the last resort).
- **A driver's stop under a leader screen (ticket 0444, 2026-11-16) [C, offline tests].** The harness stop refuses a
  silent channel, so a driver whose game sits at a leader screen cannot stop it. Pattern (run_3308's `stop_game`): arm
  the handler right after each load (`python -m tools.harness.diplo arm`); before the stop, unless the D155 marker shows
  a box, `turns.py wait --instance NAME -1 --stall 20 --answer 8` (FROM -1 = any active human turn: the handler or,
  after 20 s of silence, the located refusing click answers the screen), then the harness stop as usual. Details and
  tests: `research/05-graphics-composition/unit-server-scene.md` 3.5.

### 12.3 In game vs. the load screen; end-turn exit codes; the refusing button from a screenshot (1117, 2026-11-10)

**Status: offline only (code and tests); nothing below has run in the game through this code yet.** Code:
`tools/harness/frontend.py` (`in_game`, `probe_in_game`, `dismiss_load_screen`, `ensure_in_game`), `turns.py`,
`checklist.py`, `dialogs.py`, `input.answer_dialog`, `tools/instances/provision.py` (verify); usage in
`tools/harness/README.md` "In game, load screen, end-turn codes, dialogs (1117)".

**What epic 1300 saw on c5h-b (2026-11-10 05:11-05:36, evidence `<pins_dir>\evidence\1305\live\R1\`) [L]:**
after the huge save loaded the Lua channel answered while the load screen still showed "Continue Your Journey"
(`blocked.png`); `turns.py end` returned rc 1 after 40 tries of "blocked 2 1" (2 = `ENDTURN_BLOCKING_PRODUCTION`) and
the driver went on to `wait`, which timed out after 945 s. A posted click at client (0.867, 0.695) closed the screen
(05:29:21) and the next end turn forced after 5 tries. `frontend.take_begin_click` (1108) acts only when
`Game.IsPaused()`; it did not cover this case [I: its pause check came back false]. c5h-b's client was 2554x1411
(harness.log: the click at fraction (0.5, 0.977) landed on client (1277, 1379)); its screenshots are scale 0.4,
1021x564.

**The load screen in effect [C]** (read only: c5h-a's `MODS\(3a) VP - EUI Compatibility Files\LUA\LoadScreen.lua`,
5,069 B, and `install\Assets\DLC\VPUI\GameSetup\LoadScreen.lua`, 5,629 B; the mod file wins): its show handler calls
`UI.SetDontShowPopups(true)`; `Events.SequenceGameInitComplete` sets the load-complete flag, calls
`Game.SetPausePlayer(Game.GetActivePlayer())` (single player) and shows `Controls.ActivateButton` with
`TXT_KEY_BEGIN_GAME_BUTTON_CONTINUE` for a loaded game; `OnActivateButtonClicked` (EUI: `local function`, VPUI: a
global) does `Events.LoadScreenClose()`, `Game.SetPausePlayer(-1)` unless multiplayer or hot seat, and
`UI.SetDontShowPopups(false)`. Esc / Enter do the same once loading is complete. So "in game" = the `LoadScreen`
context hidden or absent, the `InGame` Lua state present, and not paused; closing the screen before the button shows
would let `SequenceGameInitComplete` pause the game behind a hidden screen [I], so the harness waits for the button.

**Probe design [C/I].** One Main-state chunk finds the `LoadScreen` and `InGame` states through `G.Threads` (as
`CvLuaSupport.cpp`'s runner does, lines 502-546; an unknown `--state` name answers `ok=0` "no Lua state named X",
line 608) and returns `ContextPtr:IsHidden()`, `Controls.ActivateButton:IsHidden()`, the InGame context's
`IsHidden()` (reported, not gated: whether it is hidden under the load screen is unknown [I]) and
`Game.IsPaused()`/`GetPausePlayer()`; every read is pcall'ed; nothing is passed to C++. Not verified live: that the
LoadScreen context is hidden (rather than destroyed) after the click, and that `Controls` resolves from another
state's environment (diplo.py relies on the same for `ContextPtr`). *Update 2026-11-10: the probe, the channel dismissal and the in-game wait ran live in 1109 and 1110 (sections 12.4 and 12.5) [L].*

**End-turn blocking names [C]** (`CvGameCoreDLLUtil/include/CvEnums.h` 2872-2908, instance-aware fork): 0 POLICY,
1 RESEARCH, 2 PRODUCTION, 3 UNITS, 4 DIPLO_VOTE, 5 MINOR_QUEST, 6 FREE_TECH, 7 STACKED_UNITS, 8 UNIT_NEEDS_ORDERS,
9 UNIT_PROMOTION, 10 CITY_RANGE_ATTACK, 11 FREE_POLICY, 12 FREE_ITEMS, 13 FOUND_PANTHEON, 14 FOUND_RELIGION,
15 ENHANCE_RELIGION, 16 STEAL_TECH, 17 MAYA_LONG_COUNT, 18 FAITH_GREAT_PERSON, 19 ADD_REFORMATION_BELIEF,
20 LEAGUE_CALL_FOR_PROPOSALS, 21 CHOOSE_ARCHAEOLOGY, 22 LEAGUE_CALL_FOR_VOTES, 23 CHOOSE_IDEOLOGY, 24 CITY_TILE,
25 PENDING_DEAL, 26 EVENT_CHOICE, 27 CHOOSE_CITY_FATE (all `ENDTURN_BLOCKING_*`). Epic 1300's later "blocked 10 6 /
10 11 / 10 13 / 26 15" were CITY_RANGE_ATTACK and EVENT_CHOICE. `turns.py end` now exits 5 with
`blocked <name> <type> <index>`, 7 when not in game; `wait` exits 6 when a stalled screen needs a human.

**Locating the refusing button [L for the pictures, offline for the matching].** Epic 1300's stall screenshots and
the answers it chose by hand (`screens.txt`): Montezuma's peace deal (REFUSE at client fraction 0.458, 0.977; ACCEPT
right of it), Bismarck's missionary demand (two full-width discussion buttons; the refusal "Our missionaries and
prophets go where they please." is the bottom one), Bismarck's reply (one button "Back"), Bismarck's denouncement
("You'll pay for this in time." / "Very well."; the neutral one is the bottom). Button crops at scale 0.4 are in
`tools/harness/assets/dialogs/` with source, box and SHA-256 in `templates.json`. Normalised cross-correlation on the
0.4-scale picture (numpy FFT, ~1.2 s per screenshot for 7 templates x 3 scales on the owner's machine, 2026-11-10) found
each target at score 0.97-0.99 after a bicubic re-sampling to 2554x1411 and a 1-2 px shift, with the label
(row-detrended inner area) score 0.96-0.98, while the competing label on the same screen scored 0.27-0.55 against
it (REFUSE vs ACCEPT 0.53-0.55; missionaries 0.27-0.28; denouncement 0.31-0.32). On non-dialog shots (the map, the
load screen, popups) no template passed (best button scores 0.56-0.84, label scores <= 0.38). Thresholds: button
>= 0.85, label >= 0.80, label margin 0.15 over every same-size template, unique. Only templates of kind refuse /
neutral / close are clickable; ACCEPT, the conceding missionary answer and the threat are never clicked. Civ 5 lays
the UI out in client pixels (section 4), so the same crops serve any client size [I: true for these screens at
2554x1411; another resolution not tried]. Not covered: screens whose labels are not in the set (other AI requests,
deal screens with other button text): no click, `needs_human`.

**Application dialogs in the turn driver (follow-up, 2026-11-10 06:07-06:20, after 1118) [offline].** A message box,
assert or crash box in the game's process (1118's `crash.dialog_state`, c5h-b's NULL-owner MessageBoxA ~05:39-05:47)
holds the game and silences the channel exactly as a leader screen does, so the stall-answer click must not run for
it. `wait_turn` now reads the dialog state at the stall (before any answer click) and at its timeout, `end_turn` after
a timed-out try and when it cannot proceed, `probe_in_game` whenever it is not in game; an open dialog stops the
driver with exit 8 and the dialog's title, text and buttons, and nothing is pressed. The checklist records the dialog
state on every failed step and no longer records a timed-out shot (`result` "timeout", `path` None) as a screenshot.
Not verified live.

### 12.4 Two instances at once (1109, 2026-11-10)

**Result [L]: two Goldberg instances run the huge save at the same time, each isolated, neither visible nor focused.**
Run 2026-11-10 06:55:15-07:00:36 EST, driver `tools/harness/evidence/1109/run_1109.py` (adapted from 1116's), evidence
`tools/harness/evidence/1109/` (`run-result.json`, `run-log.txt`, `memory-c5h-{a,b}.csv`, `gpu.jsonl`, `objdir.jsonl`,
`watch.jsonl`, `resources.jsonl`, `lua-log-stamped-c5h-{a,b}.txt`, shots at 0.25, `snapshot-diff.txt`, `reg-{before,after}.reg`,
`verify-{before,after}-c5h-{a,b}.txt`, `collect-b-live\`, `epic1300-R1-crashlogs\`). Both instances: the 1107 DLL
`5fdf3432...` (`dll.names` instance-aware), the import route (1113 wrapper with the 1118 guard; c5h-a wrapper `e312067d...`
built 06:18, c5h-b wrapper `68cdc42d...` rebuilt 06:47 by `wrapper.py uninstall` + `install`) over the release gbe
`665bb50b...`, save copy `C5H-VP8P-HUGE_0350` (turn 350, Huge 128x80), CP/VP/EUI through `frontend.prepare`, cloak +
off-screen park. Steam client running (steam.exe pid 4972), untouched. Machine: RTX 4080 16 GB (driver 610.88), 32 GB RAM;
other sessions' DLL builds were running during the start (commit charge 34.5 of 43.1 GB at 06:55).

| Proof | Result | Evidence |
|---|---|---|
| both PIDs alive at once | c5h-a pid 39848 (created 06:55:18.285), c5h-b pid 31400 (06:55:43.652), each `exe` and `cwd` in its own `install\` | `run-result.json` `proofs.processes` |
| single-instance gate renamed per instance | `\Sessions\5\BaseNamedObjects` (query only) held `Civ5App_c5h-a` and `Civ5App_c5h-b`; no `Civ5App` and no legacy `VPLuaExec`; per-instance `VPLuaExec_c5h-a` / `_c5h-b` appear while a request is pending | `proofs.objects`, `objdir.jsonl` |
| each Lua channel answers only its own game | `vp.c5h_mark = "c5h-a-8594"` set in a; b then read `nil`; `"c5h-b-8594"` set in b; a read `c5h-a-8594`, b read `c5h-b-8594`. `os.getenv("VP_INSTANCE")` inside the game's Lua answered `c5h-a` / `c5h-b` (the `os` library is available to luaexec chunks [L]) | `proofs.lua_marks` |
| separate folders | each shim log names only its own instance (`mentions_other_instance` false); 29 files changed under each instance's own `Documents` (Logs, cache, autosaves, config.ini); `LocalAppData` and `gse` unchanged (nothing written there this run); autosaves `AutoSave_Post_0350` / `AutoSave_0350 AD-1930` in each instance's own `ModdedSaves\single\auto` | `folders` |
| one AI turn in each, at the same time, handler armed | a 350->351 in 43.3 s (end_turn 6 tries 5.7 s + wait 37.4 s), b in 42.4 s (5.7 + 36.6); in each, Montezuma `TRADE_AI_MAKES_OFFER` (DiploTrade OnBack) and Bismarck `STOP_CONVERSIONS` (DiscussionDialog OnBack(true)) answered in game 2.00 s after the message; 0 stall clicks | `inst.*.turn`, `inst.*.diplo_log` |
| focus | 0 foreground / 0 visible: harness `guard.watch` over both PIDs, 9 chunks of 30 s (~1,430 polls each, ~21 ms per poll for two), driver poller 6,225 polls at 50 ms over every top-level window of both PIDs; no application dialog | `watch.jsonl`, `focus_poller` |
| guard counters (1118), both at the end | `win_hooks` 271, `win_created` 4, `win_cloaked_create` 4, `win_cloaked_show` 0, **`win_cloaked_watchdog` 0**, `win_show_rewritten` 0, `win_fg_blocked` 0. c5h-b's self-test line says `class WinClass_FXS: 1 foreign` (c5h-a's window existed; informational) | `inst.*.guard_end` |
| stops | a by `launch.stop(human_turn_active=turns.stop_readiness)` (turn 351, newest autosave settled 11.18 s); b by `cli.py -i c5h-b stop --human-turn-hook tools.harness.turns:stop_readiness` (rc 0, settled 11.33 s) | `stop` |
| nothing outside the instances | `provision.py snapshot` before 06:53 / after 07:01: no differences; `HKCU\Software\Firaxis\Civilization5` export byte-identical; `frontend.restore` ok on both (hashes = install manifest); `clean-logs` 0 files; `verify` ok on both before and after | `snapshot-diff.txt`, `reg-*.reg`, `verify-*` |

**Start-up, serialised by the launch gate [L]** (gate = `Local\C5HarnessLaunchGate`, held from the helper call to the
main window; times after each instance's own gate_acquired; shim-log clock):

| | c5h-a | c5h-b |
|---|---|---|
| launch call / gate acquired | 06:55:18.100 / 06:55:18.188 (wait 0.0 s) | 06:55:21.101 / 06:55:43.578 (**wait 22.46 s**) |
| created / guard confirmed | +0.10 / +0.20 | +0.07 / +0.16 |
| gbe `SHGetFolderPathW(APPDATA)` (Steam_Client construction) | +19.87 (06:55:38.056) | +15.83 (06:55:59.405) |
| `CreateMutexA("Local\Civ5App")` -> `Civ5App_<name>`, lasterror 0 | +20.30 (06:55:38.491) | +15.84 (06:55:59.415) |
| CEG handshake (between the two lines above) | <= 0.44 s | <= 0.01 s |
| `STEAM_TERM_EVENT_<pid>_*` first seen (objdir, 0.5 s poll) | 06:55:38.7 | 06:55:59.5 |
| main window (gate released) | +25.4 | +18.2 |
| load issued (mods active) / first Lua answer / in game | 06:56:59.6 / 06:57:59.8 / 06:58:00.5 | 06:57:16.1 / 06:58:16.4 / 06:58:17.1 |

The gate serialised the two starts end to end: b's helper ran only after a's window existed, so the two CEG handshakes
were 21 s apart and never overlapped (no PRODUCE-semaphore wait could occur). Both handshakes were answered by the Steam
client as in 1108 (steam.exe held `STEAM_DRM_IPC` and the two semaphores before either start; `objdir.jsonl`). Observe
only: nothing was opened by name, created or signalled. The ~16-20 s between resume and Steam_Client construction is
the EXE's pre-main span seen in 1108/1114 (shorter here; its two `SHGetFolderLocation` system-info rounds at +9.5 and
+19.1 s for a, +8.1 and +15.8 s for b) [L]. The two saves loaded in parallel in 60 s each (load issued -> first
Lua answer), the same as one instance alone (63 s, section 12.5) [L].

**Costs per instance [L]** (both in game at the human turn, then one AI turn each; `memory-*.csv` every 5 s):
private 2,143-2,172 MB (peak), working set 1,865-1,897 MB, committed 2,468-2,502 MB, largest free block 1,050-1,062 MB
at the end (the 32-bit address space, as in 1108/1114: two instances do not change one another's). System commit
charge went 34.5 -> 39.9 GB of a 43.1 GB limit during the two starts (with other sessions' builds running): **commit
headroom, not physical RAM (available 12.0-18.8 GB), was the tightest resource**. GPU (`nvidia-smi`, read only,
`gpu.jsonl`): VRAM used 2,243 MiB before -> 5,465 MiB with both in game, 3,930 MiB after c5h-a stopped, i.e. **~1.5-1.6 GB
VRAM per instance**; whole-GPU utilisation 11-29% before -> 32-46% with both in game; per process (pmon `sm`) 5-32%;
board power 21 W -> 84-119 W. **Frame rate of each cloaked, off-screen window** (count of `Events.LocalMachineAppUpdate`
ticks from 1116's handler, `diplo.status().ticks`, over 20 s, both instances idle at the human turn at the same time):
155.4 fps (a) and 113.4 fps (b) at 06:58:18-38; 119.8 and 121.2 fps after the turn (06:59:28-48) [L; ticks are counted
per app update, assumed one per frame [I]]. Not measured: the frame rate of one instance alone with this counter;
per-process GPU memory (pmon reports `fb` 0 under WDDM).

**RECIPE-team-dll.md steps exercised on c5h-b (none of these ran in epic 1300's run, which predated 1116-1118) [L]:**
`cli.py -i c5h-a -i c5h-b status --json` (rc 0), `frontend.py -i c5h-b ingame` (rc 0, "IN GAME"), `lua.py --instance
c5h-b "return Game.GetGameTurn(), Game.GetActivePlayer()"` (rc 0, `[1] 350 [2] 0`), `diplo.py --instance c5h-b arm`
(rc 0, "already armed gen=1") and `status` (rc 0), `provision.py collect c5h-b --what logs,crashlogs --live` while
running (rc 0, 11 files), `cli.py -i c5h-b stop --human-turn-hook tools.harness.turns:stop_readiness` (rc 0), then
`frontend.restore`, `clean-logs`, `verify`, `snapshot` + `diff`. RECIPE step 2 (`set-dll`, `wrapper.py install`) ran in
preparation (06:46-06:47). Not exercised: step 3a (markers, mod options), `set-dll --original` (c5h-b keeps the 1107
DLL), `frontend.py dismiss` from the CLI (the driver's `ensure_in_game` dismissed the load screen through the channel).
Section 12.3's in-game probe and channel dismissal ran live here on both instances (`ensure_in_game` dismissed once,
`['closed']`, no click), so its "not verified live" for those two items is answered [L].

**What isolates two instances, and what they still share (Q8) [L unless marked].**
- Isolated: the install copy (`<inst>\install`, 7.98 GB, 13,311 files each); the user folder (Documents redirected by
  the shim for CSIDL 5 / FOLDERID_Documents), LocalAppData (CSIDL 0x1C), the single-instance mutex (`Local\Civ5App` ->
  `Local\Civ5App_<name>`), the DLL's kernel objects (`VP_INSTANCE`: `VPLuaExec_<name>`, `VPMemSnapshot_<name>`,
  `TurnByTurn_<name>`), gbe's save path (`GseSavePath` / `local_save_path` = `<inst>\gse\`), the harness's per-instance
  `run\` (state, logs, shim log), the Lua `vp` table (one per process).
- Shared: the **Steam client as the CEG responder** (both handshakes answered by steam.exe 4972; Steam must run);
  **start serialisation** (the launch gate: the second start waits for the first window, 22.5 s here, 13.8 s in 12.5);
  the **GPU** (one RTX 4080; ~1.6 GB VRAM and 5-32% SM per instance); CPU, RAM and the system commit limit; the read-only
  `HKCU\Software\Firaxis\Civilization5` key (unchanged); gbe's `SHGetFolderPathW(CSIDL_APPDATA)` passes through to
  the owner's Roaming folder (nothing written there; 1114); the `WinClass_FXS` window class name (two windows of that class
  exist; the DX11 start-up never searches for it, headless-and-parallel-testing.md 4.5).

Not verified: three or more instances; an instance next to the owner's own Steam game (the owner's game was not running, so
coexistence was not shown); a CEG handshake under real contention (the gate never let two overlap); per-process GPU
memory; frame rate of one instance alone with the tick counter.

### 12.5 Huge-save checklist live (1110, 2026-11-10)

**Result [L]: the checklist passed all 11 steps in three unattended passes, one on c5h-a alone and one in each of two
concurrent instances**, on the first live run of `tools/harness/checklist.py` (no code change needed; chunk statuses
updated to `[L]`, below). Driver `tools/harness/evidence/1110/run_1110.py` (one process per instance: `frontend.prepare`,
`launch.launch`, wait for the load, `frontend.ensure_in_game`, then `python -m tools.harness.checklist --instance NAME
--turns 2 --out ... --evidence-dir ...` as a subprocess, then `launch.stop(human_turn_active=turns.stop_readiness)`, all in
`driver.DriverRun`). Concurrent pass: two driver processes started 4 s apart. Same instances, DLL, route and save as
12.4. Evidence `tools/harness/evidence/1110/{single-c5h-a,concurrent-c5h-a,concurrent-c5h-b}/` (`result.json` = the
checklist's JSON, `run-result.json`, `run-log.txt`, `checklist-stdout.txt`, `checklist/memory.csv`,
`checklist/shots/*.png` (reduced to 0.25 and 256 colours after the run), `watch.jsonl`, `resources.jsonl`), plus
`snapshot-diff.txt`, `reg-*.reg`, `verify-after-*.txt` at the top.

| Step | single c5h-a (07:05:50-07:07:32) | concurrent c5h-a (07:10:54-07:12:44) | concurrent c5h-b (07:11:19-07:13:02) |
|---|---|---|---|
| status | pass 1.3 s | pass 1.4 s | pass 1.8 s |
| ui_states | pass 0.3 s | pass 0.4 s | pass 0.2 s |
| ai_turns (2) | pass 70.7 s: 350->351 37.2 s (end turn 6 tries; 2 screens answered in game), 351->352 32.9 s (1 screen) | pass 77.7 s: 43.9 s / 33.0 s | pass 71.2 s: 38.3 s / 32.1 s |
| city_screen | pass 1.6 s (Thebes, id 1102) | pass 1.6 s | pass 1.6 s |
| tech_tree | pass 1.7 s | pass 1.9 s | pass 1.8 s |
| diplo_overview | pass 1.8 s | pass 2.0 s | pass 1.9 s |
| leader_screen | pass 13.2 s (Isabella, player 2; held 12.001 s, closed by the timer, channel silent during the hold) | pass 13.4 s | pass 13.4 s |
| save_game | pass 7.2 s (`c5h-c5h-a-t0352-070550`, 2,236,556 B, main thread) | pass 7.5 s | pass 7.6 s |
| unit_select | pass 0.8 s | pass 0.6 s | pass 0.6 s |
| hex_pick | pass 2.4 s: (85,22) (83,23) (86,21) | pass 2.3 s, same hexes | pass 2.3 s, same hexes |
| status_end | pass 0.9 s | pass 0.8 s | pass 0.9 s |
| whole checklist | **pass**, 102.1 s | **pass**, 109.6 s | **pass**, 103.4 s |

- Chunks now run as written [L] (status in `checklist.py` is the constant `LIVE_1110`): `status`, `city_open`,
  `diplo_open`, `diplo_close`, `leader_pick`, `leader_arm`, `leader_open`, `leader_after`, `main_thread_arm`,
  `main_thread_result`, `save_body`, `unit_select`. **`UI.SaveGame(name)` from a one-shot `Events.LocalMachineAppUpdate`
  handler works** (cookbook G9's call, never run before): the handler ran on the next frame, the file appeared in the
  instance's own `ModdedSaves\single` and settled in 7.1-7.5 s; one save per pass (the `rec.ran` guard held). Whether
  `Events.LocalMachineAppUpdate.Remove` inside the handler's own dispatch actually removed it is not recorded (the call
  is pcall'ed and its result discarded) [I]. Still `[I]`:
  `main_thread_cancel`, `save_direct`, `leader_disarm` (failure paths, not reached).
- 1116's `diplo.pause`/resume ran live for the first time (the checklist pauses the handler around `leader_screen`; both
  calls returned True in all three passes, and the handler did not close the checklist's own leader screen: it stayed up
  12.001 s until the timer closed it).
- Timings: single pass launch call -> in game 2 min 38 s (mods activation 95 s, load 63 s); concurrent a / b launch ->
  in game 2 min 34 s / 2 min 52 s; b's launch gate wait 13.76 s; windows at +18.4 s (a) and +17.7 s (b) after their
  gates. Loads in parallel took 62 s / 66 s. The first AI turn is 3-18% slower with two instances running (43.9 / 38.3 s
  against 37.2 s alone; 12.4: 43.3 / 42.4 s, +14-16%); the second turn is unchanged (32-33 s). One sample each [L].
- Memory per instance during the checklist (`checklist/memory.csv`): private 2,098-2,291 MB, working set 1,792-2,013 MB,
  committed 2,426-2,619 MB, largest free block down to 891-916 MB (private peaks ~120 MB above 12.4's figures, which
  had no leader screen or save). System commit 28.8 -> 39.8 GB of 42.65 GB at the peak of the concurrent pass.
- Safety [L]: 0 foreground / 0 visible in all three passes (driver pollers 5,657 / 5,700 / 6,004 polls; harness watch 8 / 8
  / 9 chunks, all ok); guard counters at each end as in 12.4 (`win_cloaked_watchdog` 0, `win_cloaked_show` 0,
  `win_fg_blocked` 0); no application dialog; every stop by `stop_readiness` (newest save settled ~11.2 s; for the
  checklist runs the newest save is the checklist's own named save); `frontend.restore` ok; snapshot before 07:02 /
  after 07:14: no differences; registry export byte-identical; `verify` ok on both.
- Left in the instances: the three named saves `c5h-c5h-a-t0352-070550`, `c5h-c5h-a-t0352-071054`,
  `c5h-c5h-b-t0352-071119` in their own `ModdedSaves\single` (verify does not flag them).

## Live proof of the window guard against a real DLL dialog (2026-11-10 08:14 EST, epic 1900) [L]

On instance c5h-a with the base instrumented DLL 5fdf3432, the DLL's precondition MessageBox of bug 0080
opened in AI turn 354 to 355 at about 07:43:35. Under the 1118 guard it was cloaked from creation: 0 of 1,129
polls at 0.1 s saw it visible to the user, 0 of 10,483 focus polls saw it or any window of the instance in the
foreground; guard counters win_cloaked_create 2 to 5, win_cloaked_watchdog 0, win_fg_blocked 0 to 1 (the box's
own foreground request was refused). The harness reported title 'Error', the text with "Expression: e > -1 ...
CvGlobals.cpp ... Line: 4020" and buttons ['OK']; `wait_turn` returned code 8 and `launch.stop` raised
DialogOpen. The run was ended, on the MC's authorisation for this reproduction only, by WM_COMMAND with
control id 2 to the dialog after the saves had been settled for 212 s; the DLL then stopped deliberately.
This closes the "not verified" item of the 05:39 safety note above. Evidence: `tools/i0b/evidence/guard/`.

## 13. Running the patch (proxy + x64 server) on an instance (epic 2500, 2026-11-10) [L]

The recipe is `tools/harness/RECIPE-proxy.md`; the results are in `research/03-dll-engine-boundary/proxy-design.md`
section 15. Facts for anyone driving a proxied instance:

- **Guard route.** All epic 2500 launches used `launch.launch(inst, route="early")` (the CLI has no route flag).
  *Corrected 2026-11-10 (issue 9009):* both routes are safe again, and neither route was safe from about 14:28 to the
  fix. The guard's activation veto failed with error 126 on both routes (section 7.1), the real EXE's early-route
  launches of 14:43-15:39 included. The stand-in's early route passed only because its DLL path was already
  registered in the table of hook modules. The default route is unchanged. Rebuild `tools/harness/native` and run
  `wrapper.py install` on each instance. *Replaced:* "the import route lets a box take the foreground; the early route
  passes; launch with the early route until fixed".
- **The server is a child of the EXE** and has its own `conhost.exe`; `cli.py status` lists both with the server's
  guard counters; `stop` ends the EXE only and waits for both (they ended 0.02-0.3 s after it in every run);
  `cli.py leftovers` must say clean after a run.
- **`watch` prints only at the end of its time** (or at a hit): run it in slices (`proxy/tools/live/watch_loop.py`,
  120 s; *2026-11-10, issue 2704: replaces `server/evidence/2500/watch_loop.sh`, which hard-coded a worktree path*)
  so that a run that ends early still leaves a result. *Corrected 2026-11-10 (issue 9010, section 7.2):* the
  epic's watch slices covered the EXE's main window and the children, not the EXE's other windows, and began 14 to
  20 s after process creation. The watch now covers every top-level window, and `launch()` records a `startup_watch`.
- **The proxy's `vpproxy.log` in the mod folder** (`mode=off` writes it next to `vpproxy.ini`) is run output.
  `set-proxy --original` moves it out since issue 9010, and `verify` flags it on an instance that is not proxied.
- **An application dialog in the EXE** (CRT "Runtime Error!" after an abort, 2026-11-10 15:08): `process_state`
  dialog, cloaked, not foreground, not seen; its OK button has control id 2, so a posted `WM_COMMAND IDOK` (1) does not
  press it. The coordinator pressed it with the button's own id after a settled-save check, using an improvised script
  (`server/evidence/2500/press_ok.py`). *Since 2026-11-10 (issue 9009, rule D29) this is a harness command:*
  `cli.py -i <inst> dismiss --button OK --requested-by WHO` (section 7.1).
- **A front end without a game** (main menu only): the Lua channel is silent (it is polled in `Update`); EXIT (client
  fraction 0.4996, 0.6454) [C, measured 2026-11-10 by issue 2704 from `server/evidence/2500/verification/live/S1-menu.png`],
  then the game's own confirm popup ("Yes" at client fraction 0.4995, 0.5106 on c5h-a's 2554x1411 client; the same on
  c5h-b), ends the EXE through the orderly path (`SetDLLIFace(NULL)`, server exit 0). Both clicks are `cli.py click`
  (posted messages, no focus).
- **Frames from Lua**, the same in both modes: `Events.LocalMachineAppUpdate` counter (`server/evidence/2500/fps_arm.lua`);
  it agreed with the proxy's own frame marker within 10% (311 against 338 fps on the Tiny game).
  *2026-11-10 (issue 2704): kept as `proxy/tools/live/frame_counter.lua`, read by `proxy/tools/live/idle_window.py`,
  which times the window with Python's wall clock. The epic's fps numbers have no saved output (proxy-design.md 15.5
  correction).* (*Corrected 2026-11-15 by issue 0350 [L]: `os.clock()` in the game's Lua is wall time since the
  process started, in 1 ms steps, not process CPU time; it advanced 23.899 s over 23.890 s of uptime at 10 fps, where
  the EXE uses well under one processor. Was: "`os.clock()` ... is process CPU time, not wall time [I: standard Lua
  5.1 `clock()`], so it must not be the divisor". Python's wall clock stays the divisor; it is the caller's own.*)
  *2026-11-15 (issue 0350) [L]: the handler gets two numbers: the machine's uptime in milliseconds (`GetTickCount64`)
  and the frame delta in seconds, both on the 15.6 ms tick. The chunks took the first as a frame delta, so the largest
  frame time they kept (`maxdt`, `idle_window.py`'s `max_frame_s`) was the uptime itself in every `idle.json` written
  before the fix; `fps` and the counts were never affected. Fixed: `max_frame_s` is now the largest `os.clock()` step
  between frames and `max_frame_engine_s` the largest engine delta, with `arg_clock_ratio` and `os_clock_ratio` as
  checks of both clocks. Live check, the affected evidence files and the test:
  `research/08-performance/idle-frame-rate.md` section 0.*
- **Live helpers without worktree paths (issue 2704, 2026-11-10).** Epic 2500's `server/evidence/2500/launch_inst.py`,
  `watch_loop.sh`, `idle_window.sh`, `wait_game.sh` and the verification's copies `cd` into or import from another
  agent's worktree (deleted later) and cannot be rerun. Their replacements are `proxy/tools/live/` (repository root
  from `__file__`), listed in `tools/harness/RECIPE-proxy.md` section 9. Two traps met there [C]: Git Bash's GNU `tar
  -x -f C:/...` fails because `tar` reads `host:file` as a remote archive (use a relative name from the folder, or
  `python -m tarfile -e <file> <dir>`); `vpproxy.ini` is installed with CRLF and its SHA-256 is recorded in
  `instance.json` `proxy.files`, so a mode switch that rewrites line endings breaks `provision.py verify` and
  `set-proxy --original`: switch keys with `proxy/tools/live/set_ini.py` (bytes kept, hashes printed; the round trip
  mode=off and back, with appended keys removed by `--unset`, gives the original hash:
  `server/evidence/2700/live-helpers/offline-checks.txt`).
  *Corrected 2026-11-11 (bug 0106, one-off 9023) [C: unit tests, not run on a real instance]:* a changed
  `vpproxy.ini` no longer blocks `set-proxy --original`: the tool saves the changed file as
  `<inst>\vpproxy.changed-<stamp>.ini` beside `instance.json`, logs both hashes and removes the layout. A changed proxy
  DLL, fallback or `VP64\` binary still refuses (nothing changed). `verify` while proxied still flags a changed ini.
  Replaced: "a mode switch that rewrites line endings breaks ... `set-proxy --original`" (it breaks only `verify` now).
- **The EXE's guard log as the check of the activation veto (issue 2704).** `proxy/tools/live/guard_check.py <pid>`
  reads `%TEMP%/civbg_guard_<pid>.log` from the last `FocusGuardInstall pid=<pid>` line (the file is appended to when
  a pid is reused) and passes when `guard self-test (issue 9009): ok` and every `activation veto (WH_CBT) on thread N:
  ok (err 0)`; the 2500 verification's EXE pid 2616 (c5h-b, 16:50) passes with 3 veto lines [C, offline read].

## 14. A full dump of a live game that goes on running (one-off 9036, 2026-11-11) [L]

Why: one-off 9034's huge-save game showed the C runtime abort dialog at turn 420 and the harness had no way to keep
the evidence (its `EVIDENCE.txt`: "Dumps: NOT taken"). The process state at an abort is the best evidence there is.

**The command** (Windows SDK Debugging Tools, installed on the workstation under
`C:\Program Files (x86)\Windows Kits\10\Debuggers\`; the x86 cdb for the 32-bit EXE, the x64 cdb for `server.exe`):

```
cdb.exe -pv -p <pid> -c ".dump /ma <path>; qd"
```

`-pv` is a non-invasive attach (cdb suspends the threads, reads memory, resumes them; it never becomes the process's
debugger) and `qd` detaches. Both are required: an invasive attach followed by `q` ends the target. Proven first on a
throwaway `python -c "import time; time.sleep(600)"` (dump 28 MB, opens with `cdb -z <dump> -c "k; q"`, process alive
afterwards), then used on 9034's EXE (pid 40432, 3,580,281,668 B, about 60 s) and server (pid 19184, 383,930,665 B):
both processes went on with the dialog open and `AutoSave_Post_0420` did not change (hash before and after)
[L, 2026-11-11 16:42-16:46 EST]. Dumps go to `D:/c5-64bit-work/<epic>/dumps/` (rule 15), never into the repository.

**In the harness since 9036:** `python tools/harness/cli.py -i <inst> dump --out D:/c5-64bit-work/<epic>/dumps
[--child server.exe]` (`tools/harness/dump.py`: picks the debugger by `IsWow64Process`, refuses a folder inside the
repository and a process that is not running, opens the target only with `PROCESS_QUERY_LIMITED_INFORMATION`, prints
path, bytes, SHA-256, seconds, `alive_after`). `tools/harness/tests/test_dump.py` checks statically that the command is
exactly `-pv` + one `.dump /ma` + `qd` and that `dump.py` names no terminating API or right (each check with a mutated
positive control), and dumps a throwaway process live (alive afterwards). 31 passed with `test_safety_static.py`
(`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`; with plugin autoload a `dash` plugin of the system Python fails at import).

**Never dump a game a driver is playing.** The EXE is frozen while cdb writes (124.8 s for a 2.7 GB proxy-route EXE
at turn 361 on c5h-a, 2026-11-11 17:28:58): `turns.py wait` took the frozen window for a hung one (exit 6: "PrintWindow
... did not return within 10.0 s"), and `tools/validation/proxy_long.py` stopped the game (harness stop, newest save
settled, nothing lost). Dump a game left at a dialog or between driven turns only.

**Reading the dumps without symbol downloads.** The EXE has no symbols and ntdll's are not on disk, so `!heap` and
`!address` do not work (`No symbols for ntdll`); the proxy's PDB (`<pin>/proxy/bin`) gives its frames and globals
(`-y <pin>/proxy/bin`). The debugger's `size=` for the proxy's VC9 containers comes from modern-STL visualizers and is
right for maps but missing for vectors: read the raw words (VC9 x86 `std::vector` = {first, last, end}; `std::map` =
{allocator, head, size}). Address-space totals and allocations by size come from the dump's own MemoryInfoList stream
(stream 16; WOW64 addresses above 2 GB are sign-extended to 64 bits, mask them); scratch scripts of 9036 did this in
Python with `numpy`. The EXE's load address varies per run (0x00940000 and 0x00D30000 in two dumps): VA at ImageBase
0x400000 = address - base + 0x400000.

## 15. Frame cap of every instance that does not measure (issue 0157, decision D106 A; 2026-11-13/14)

**The switch.** Every harness launch runs frame-capped at 30 frames per second; only a declared timing launch runs
uncapped: `cli.py -i NAME launch --timing`, `launch.launch(inst, timing=True)`, or `C5_TIMING=1` in the environment of
the shell that starts it (for drivers that call the harness through another script, such as
`proxy/tools/live/launch.py`). The record goes into the state file under `frame_cap` (`capped`, `timing`, `fps`,
`declared_by`, the launch's `pid`, the guard's report when the window exists, the instance's `ThrottleOnLossOfFocus`
value) and into `run\harness.log`; `cli.py -i NAME framecap` prints the record and the guard's live counters. The
frame-rate tools refuse a capped instance: `proxy/tools/live/idle_window.py` exits 4 with the reason before it counts a
frame (`tools/harness/framecap.py` `require_uncapped`: no record for the running pid, a capped record, a rate held at
run time, `ThrottleOnLossOfFocus = 1`, or a game DLL of the `fast` configuration by ticket 0159's
`tools/x64-dll/build.py timing_refusal` all refuse), and `proxy/tools/live/fps_table.py` refuses every arm whose
`idle.json` does not show a timing launch (exit 4; `--pre-0157` accepts arms without any record, measured before the
cap existed). Tests: `tools/harness/tests/test_framecap.py` (20, offline; corrected 2026-11-14 by 0176 from 0157's review, was "17").

**The means: a wait in the focus guard's Present hook** [C, live 2026-11-13]. `launch()` passes the rate in
`C5_FRAME_CAP_FPS` (30, or 0 for a timing launch: then no hook at all). The guard (`tools/harness/native/src/
focus_guard.cpp`, in the game through the import wrapper or the early injection) makes a dummy D3D11 device and swap
chain on a hidden, never-shown window in a thread of its own, writes its function into the swap chain vtable's Present
slot in `dxgi.dll` (a data write in the test process; no code byte of the EXE changes; nothing of it ships) and waits
there (high-resolution waitable timer) until one frame interval has passed since the previous frame's slot. Details
and the shared-memory block (offsets 1544-1568): `tools/harness/native/README.md`, "Frame cap". The rate is writable at
run time (`framecap.set_fps(pid, fps)`). **An instance gets the cap only with a current import wrapper**: after
`tools/harness/native/build.bat`, `python tools/harness/native/wrapper.py install <instance>\install`; a launch whose
guard does not report the cap installed is logged as a warning (`frame cap requested but the guard reports ...`).

**Stock route, measured** [L, indication only: the workstation was not quiet (COMMON 23), 7 to 32 of its 32 logical
processors busy with other work]. c5h-a (C:), save `C5H-VP8P-HUGE_0350` at turn 350, 32-bit DLL `i0ship-26234371-ia`
(SHA-256 3c2f47da...), DX11 EXE through the import route, cloaked; idle windows of 30 s at the capital after a 20-s
settle, frames from Lua (`Events.LocalMachineAppUpdate` count over Python wall time), processor time of the EXE from
`psutil`; driver `tools/harness/evidence/0157/run_0157.py`, results `tools/harness/evidence/0157/results.md`.

| Arm (start, EST) | Frames/s idle | EXE processors busy idle | Lua call | AI turn 350->351 (end_turn + wait) | Launch to Lua |
|---|---|---|---|---|---|
| uncapped (22:19) | 122-155 | 4.48-5.36 | 0.11 s | 41.3 s | 193 s |
| uncapped (22:45, machine 30 of 32 busy) | 88-113 | 3.83-4.10 | 0.11 s | 47.6 s | 260 s |
| uncapped, `--timing` (23:42) | 124-139 | 4.29-4.72 | 0.11 s | 35.9 s; 351->352 38.9 s | 162 s |
| **guard cap 30 (23:33)** | **29.2-29.7** | **1.06-1.12** | 0.12 s | 46.8 s; 351->352 44.9 s | 188 s |
| guard cap 30, cap lifted during odd turns (23:53) | 29.1 | 1.19 | - | capped 47.7 s, **lifted 38.7 s**, capped 40.2 s, lifted 30.9 s | 182 s |
| `ThrottleOnLossOfFocus = 1` (22:35) | 4.3-4.5 | 0.18-0.44 | 0.23 s | 172.4 s (end_turn 26 tries) | 224 s |
| `ThrottleOnLossOfFocus = 1` (22:53) | 4.1-4.6 | 0.19-0.45 | - | **turn never ended**: end_turn blocked 40 tries (`ENDTURN_BLOCKING_PRODUCTION`), wait_turn timed out at 900 s | 202 s |

So the cap takes an idle game from about 4.5 to about 1.1 processors (a quarter). Held during the AI turn it makes the
turn about 15-30 % longer (46.8 / 47.7 s against 35.9 s for 350->351 within 20 minutes; 44.9 against 38.9 s for
351->352): the EXE's turn processing is paced by frames. Lifting the cap for the AI turn at run time gives the uncapped
time back (351->352: 38.7 s lifted against 38.9 s uncapped). **So the harness lifts it**: `turns.wait_turn` sets the
guard's rate to 0 while it waits for the AI turn and sets the launch's rate again on every exit (try/finally, the early
not-in-game and dialog returns included); `turns.end_turn` sets the launch's rate again on every exit (real channel
only, a no-op for a timing launch, never raises). *(Changed 2026-11-14 by ticket 0158 from 0157's review: until then
`end_turn` lifted the cap when it forced the end of turn and only `wait_turn` set it back, so an `end_turn` without a
`wait_turn`, or `wait_turn`'s early NOT_IN_GAME return, left the instance uncapped; tests
`tools/harness/tests/test_turns_defaults.py`. Live with several instances: the guard read 30 after every turn and 0 while
`wait_turn` ran, `capacity-series.md` sections 7 and 8.)* **Turn-wait defaults (0158, same date):** the in-game wait of
`end_turn`/`wait_turn` is 900 s (`turns.IN_GAME_TIMEOUT_S`; was 300 s, which 4 games at once on D: exceeded), and
`wait_turn`'s stall is max(60 s, 2 x the longest AI turn of the launch) when none is given (`turns.stall_for`, kept in
the state file under `ai_turns`; `--stall` and `stall=` still override; `checklist.py` passes its own 45 s). Live with the 0157 change (MC 2026-11-14 00:12),
the guard's rate sampled once a second during each turn reading 30 during `end_turn` and 0 for the AI turn: turns
350/351/352 took 36.1 / 34.7 / 28.7 s capped (01:33, machine 8 of 32 busy) against 33.9 / 36.0 / 36.2 s in the timing
launch just before (01:27, 15 busy), sum 99.5 against 106.1 s. An earlier pair under heavier load (00:24-00:39, 22-26
busy, before the sampler existed) gave 54.4 / 45.3 / 52.6 against 48.8 / 41.2 / 45.7 s (+12 %). The load time does not
change measurably (162-272 s in all arms, dominated by the machine's load).

**`ThrottleOnLossOfFocus` is not for turn-playing runs** [L]. The EXE's own config.ini setting (0 in every instance copy)
is the only cap the EXE offers (no rate setting among its config keys or strings; `WaitForVSync = 1`, already set in
every instance copy, does not cap a cloaked window: 113-338 fps measured since 2026-11-10). With it on, a harness game
(never foreground) runs at about 4.5 frames per second, but AI turns take 3.4 times as long and the harness's end of
turn can block for good (table). The guard's `pretend-active` switch does not lift it (4.60 / 4.57 fps on / off); a
posted `WM_ACTIVATEAPP` does not either (4.83 / 4.82); a posted `WM_ACTIVATE` does, at run time and without the focus
moving: `WA_ACTIVE` 148.5 fps and 5.34 processors, `WA_INACTIVE` back to 4.24 fps (22:24-23:25, foreground never the
game). The harness logs a launch with it on as a warning and refuses a frame-rate number from it.

**Animation follows the clock, not the frame count** [L]. (1) The argument the engine passes to
`Events.LocalMachineAppUpdate` is a wall-clock time in milliseconds (the clock of the Lua log's stamps): its change per
wall second was 999.6-1000.0 at 4.4, 29 and 100-155 frames per second (*2026-11-15, issue 0350 [L]: it is the
machine's uptime, `GetTickCount64`, and a second argument follows it, the frame delta in s; both on the 15.6 ms tick;
`research/08-performance/idle-frame-rate.md` section 0*). (2) Two in-memory captures of the capital taken
0.25, 1 and 2 s apart, no input, 3 pairs each: the median mean absolute difference was 0.40 / 0.72 / 0.90 at 4.4 fps,
0.51 / 0.82 / 0.93 at about 100 fps, 0.30 / 0.90 / 0.89 at 29.5 fps and 0.39 / 0.81 / 0.91 at about 130 fps (pairs with
a whole-screen change above 5, 3 of 9 in each of the two later arms, capped and uncapped alike, left out: grey frames
from PrintWindow, below): the picture changes as much per second at 4.4 frames per second as at 130. A picture taken under the cap shows the same animation phase as one taken
uncapped at the same wall time; the parity tools need no change of the cap, only a fixed point to capture at (a
settled, paused state), as the Architect's D106 note expected. Screenshots and the Visual Checks feed work at 4.4 and
at 30 fps (full pictures; entries of issue 0157, 2026-11-13 22:40-23:49).

**D33 product path, measured** [L, indication only; 2026-11-14 01:08-01:24]. c5h-a set up by
`tools/harness/evidence/0157/product_0157.py` (3311's prep: proxy + server + `shell_composite`, `[Renderer]
ServerRenderer = 1`, D33 with Overlay kept on), the save copy `C5H-0157-HUGE_0350`; x64 DLL 74588135 (D96 pin), server
B4A54FF5 and proxy 691728BE built from this branch at 0d6300df (`server/build_server.py --deps` on the 9045 inputs
copied to C:), composite 9C88B881 from this branch.

| Arm | Frames/s idle | Processors idle: EXE / server | AI turns 350, 351, 352 | Lua call |
|---|---|---|---|---|
| timing launch (01:08) | 149-176 | 3.33-3.68 / 0.42 | 37.4, 27.9, 26.9 s | 0.10 s |
| guard cap 30, lifted for the AI turns by turns.py (01:17) | 25-30 | 0.55-0.69 / 0.09 | 30.7, 24.3, 25.9 s | 0.13 s |

The cap takes the idle product path from about 3.9 to about 0.75 processors, the server's render loop included (it
renders per EXE frame: 0.42 -> 0.02-0.09). With the lift the AI turns were not slower than the timing launch's (the
machine was lighter during the capped arm: 9-21 busy processors against 23-25). The guard's rate sampled once a second
during each turn read 30 during `end_turn` and 0 for the AI turn (`guard_fps_seen` [0, 30] in every turn). The pictures
of both arms show the same composited D33 frame (the composite's Present work runs under the cap).

Two findings on the way [L]: (1) the first version of the cap wrote the vtable slot at start, and the composite, which
reads the Present target from the vtable, refused it ("target ... is in steam_api.dll ... NOT hooked") and never saw a
Present: hence `C5_FRAME_CAP_AFTER` (the slot is written after the composite's hook; guard log: "shell_composite.dll
loaded; Present implementation entry e9 after 203 ms"). (2) The D96 pin layout `x64-04b1211b-pa803776` has a proxy
from a803776 that does not know `[Renderer] ServerRenderer` ("unknown option ... (ignored)"), so it loads no composite;
with D33's config the EXE then faulted with 0xC0000005 at the first `Update` in both a capped and a timing launch
(addresses 0x0142D492 and 0x005ED492, the same offset from two load bases; module not identified). A product-path run
needs server and proxy built from `main` until a pin carries 3305.

*Added 2026-11-15 (task 4102, `terrain-decals.md` 9.3):* `framecap.composite_of` finds a composite only under
`MODS\*\shell_composite.dll`; a composite loaded through `crashlogs\shell.helper` from another folder gets the slot written at
once and refuses Present again: such a launch sets `C5_FRAME_CAP_AFTER=shell_composite.dll` itself [L].
*Added 2026-11-20 (ticket 0620):* the same holds for VP's modpack form: its composite is under
`<game>\Assets\DLC\<pack>\Mods\*\`, which `composite_of` does not search, so a modpack launch without
`C5_FRAME_CAP_AFTER=shell_composite.dll` gives a grey world with every turn otherwise working [L, c5h-new-mpk,
`research/02-exe-internals/new-exe-build-26577625.md` C.7.3].

PrintWindow sometimes returns a uniformly grey frame (one of the two captures of a pair; seen capped and uncapped, on
both routes): the animation pairs above 5 left out above are such frames. The harness's own shots in the feed were
complete in every arm.

**Rolling it out (for the MC).** An instance is capped only once its import wrapper carries the 0157 guard: rebuild
`tools/harness/native` (`build.bat`) on `main`, then `python tools/harness/native/wrapper.py install
<instance>\install` for each instance (the wrapper is replaced, the real DLL untouched); until then a launch logs
"frame cap requested but the guard reports ...". Instances should keep `ThrottleOnLossOfFocus = 0`.

**Not verified:** the cap on another machine (oldpc, laptop) and in the early-injection route (`focus_guard.dll`; built,
not launched); several capped instances at once (ticket 0158's series); the turn cost on other saves; the 120-s limit of
the composite wait against a composite that loads later than 120 s after the module (not seen: 0.2 s).

## 16. Stopping a game without the Lua channel: `cli.py stop` detects it (issue 0176, 2026-11-14)

**What it does.** `cli.py -i NAME stop` takes `tools/harness/stop_nolua.py`'s stop (3508, epic 3500: the save folders
still for at least 60 s, no save in progress, then `launch.stop()`'s other gates and a terminate) by itself when the
running game has no Lua channel; no caller flag. Rule and exit codes: `tools/harness/README.md`, "Stopping a game
without the Lua channel". The decision is `stop_nolua.lua_channel(inst)`, from the game DLLs mapped in the recorded
process (`psutil` `memory_maps`: VirtualQueryEx and GetMappedFileName, which list the modules of the 32-bit EXE from
the 64-bit Python; nothing is read from the process's memory).

**Measured facts it rests on.**
- [L, 2026-11-14 13:00 EST] At the main menu of a mods-form instance (`c5h-c`, pid 55000, launched by the harness with
  a proxy in `MODS\(1) Community Patch` whose `dll.names` is `instance-aware`), the only `CvGameCore_Expansion2*.dll`
  mapped in the EXE was the base game's own `install\Assets\DLC\Expansion2\CvGameCore_Expansion2.dll` (4,061,696
  bytes, no `VPLuaExec`). Read-only check: `C:/c5-64bit-work/0176/check_markers.py` (scratch). So a rule on "the mapped
  DLL has no channel" alone would send every front-end stop to `stop_nolua`; the base game's DLL is therefore not
  counted, and alone it means the usual stop, which handles the front end. This agrees with
  `research/03-dll-engine-boundary/proxy-design.md` 21.2 [C]: Exit to Main Menu unloads the CP DLL and loads the base
  game's DLL at the same address; in game the mod's DLL replaces it.
  *Qualified 2026-11-16 (ticket 0411, [L], section 16.1): the base game's DLL is alone only until the mods are activated.
  On c5h-c with the harness's prepared front end (`frontend.py prepare --mods cp,vp`) the CP DLL replaced it during the
  front end's mod activation, before any load, and stayed mapped in game and after Exit to Main Menu (the mods stay
  active; MainMenu printed "requested mods are already active"). 21.2's unload belongs to an exit that deactivates the
  mods (a mod set chosen by clicks in the Mods menu). The table below is unchanged: a CP DLL with the channel names
  means the usual stop, which is right at that main menu too.*
- [C, bytes, 2026-11-14] `provision.py lua_channel_names` on the DLLs present: `c5h-a`/`c5h-b` `MODS\(1) Community
  Patch` DLL `instance-aware`; the base game's DLL `none`; `c5h-b`'s kept original (the tag DLL `262aee88...`,
  16,837,632 bytes) `none`, and it has no `vpproxy.ini`. The built proxy `ee5d59fe...` (912,384 bytes; 0153's
  `D:/c5-64bit-work/0153/b1/proxy/bin`, also in `c5h-c` today) has both `vpproxy.ini` and `VPLuaExec`.
- [C, source] The proxy's log lines carry the string `vpproxy.ini` (`proxy/src/proxy_rt.cpp`), which no game DLL has;
  `lua_channel` uses it to tell the proxy from a DLL. In fallback (mode=off, failed=1, a failed 64-bit boot) the proxy
  loads `fallback_dll` with `LoadLibraryExA` (`proxy_rt.cpp`), so both are mapped and the fallback decides; on the
  64-bit path only the proxy is mapped and its own names decide (2505's `proxy_luaexec.cpp`).
  Not checked live: the module list of a game in fallback, a game in game on the tag DLL (the decision was not run
  against a real channel-less game; the tests' fake game is a Python process), and a modpack-form instance's front end.

**Decision table** (`tools/harness/tests/test_stop_nolua.py`, 28 tests, offline; five start a fake game that really
loads copies of `python3.dll` carrying the channel names, the proxy marker or neither, so the module listing is real):

| Mapped game DLLs | Channel | `cli.py stop` |
|---|---|---|
| base game's DLL only (front end) | not known | usual stop |
| a DLL without the names (tag DLL in place) | no | `stop_nolua` |
| a DLL with the names | yes | usual stop |
| the proxy only (64-bit path) | the proxy's names | usual stop |
| the proxy + a fallback without the names | no | `stop_nolua` |
| none mapped, or not readable | the launch's `dll_names` (`none` = no, if the manifest has a DLL) | `stop_nolua` or usual |

`--coordinator-override` keeps the usual stop (no override added: D60). Not covered: a stock game without the mods (the
base game's DLL in game) still ends in the usual stop's "needs a decision".

### 16.1 The main menu after a played game (ticket 0411, 2026-11-16)

**The problem.** The usual stop's front-end check (`turns.frontend_state`) counted any `LoadScreen:` or `InGame:` line
of this run's Lua.log as "a game was loaded or is loading" for good, so after Exit to Main Menu `cli.py stop` refused
with exit 6 ("not a front end") and needed a coordinator's decision: 0392's HI trace on c5h-c (#2381, 2026-11-16 06:46).
The Lua channel is silent at the main menu (it is pumped by `CvGame::update`), so the human-turn question cannot answer.

**The rule (tools/harness/turns.py, `frontend_state`; `FrontendState.menu_after_game`).** The main menu after a game is
idle when all of these hold, and then `cli.py stop` stops it as a fresh front end (mode `frontend`, terminate after
every gate; no override, no new kill path):
- in Lua.log, after the last game marker, the Community Patch's `InGame: Shutting down InGame.lua` (CP's
  `Core Files/Overrides/InGame.lua`, `ContextPtr:SetShutdown`) and then a front-end `MainMenu:` line, with no game marker
  after them (a new `vp-dll-dev: loading`, `LoadScreen:` or `InGame:` line makes it a game again);
- nothing in `Logs\` written for `FE_MENU_QUIET_S` = 30 s (Lua.log, stopwatch.log, net_message_debug.log, the DLL's
  CSV logs): a load clicked at that main menu prints no Lua.log line until its LoadScreen context; the patch's own
  logs written on a timer are left out (`FE_PATCH_PERIODIC_LOGS`, ticket 0506, below);
- the fresh front end's conditions: no journal held open, no mod activation in progress, cache\ quiet 10 s; and, in
  `launch.stop`, the newest save settled for `--settle`, the dialog refusal, the PID and creation-time checks.
Without CP's shutdown line (a stock game without the mods) nothing changes: the stop still needs a decision.

**Measured [L, c5h-c, 2026-11-16 07:13-07:23 EST].** DX11 EXE, Goldberg, 32-bit pin `i0ship-26234371-ia` (3C2F47DA...)
set with `provision.py set-dll` for the run, front end prepared with cp+vp and `--load C5H-3600-TINY_0008`; probe
`C:/c5-64bit-work/0411/live_probe.py` (scratch; evidence `F:/c5-64bit-work/0411/arm-a/probe.jsonl`, `stop.json`):
- Game DLLs mapped (`stop_nolua.game_dlls`): none for ~25 s after the start, then the base game's
  `Assets\DLC\Expansion2\CvGameCore_Expansion2.dll`; at the mod activation (MainMenu "activating 2 mods") the CP DLL
  replaced it; the CP DLL stayed mapped in game and after the exit. So the module list cannot tell the main menu from a
  game in this set-up (a first draft of this ticket used it; dropped).
- Lua.log order: MainMenu `loading` 145520.109, MainMenu re-executes 145523.968 and 145525.125, `LoadScreen:` 145528.437
  (3.3 s after the last MainMenu line; 12.5 s in 1114), `InGame:` 145534.062; `Events.ExitToMainMenu()` from an InGame
  handler (`shell/tools/mapchange.py exit`) at 07:16:53; `InGame: Shutting down InGame.lua` 145539.062, `EUI_context:
  Shutdown` 145546.453, the front-end contexts (LoadMenu, SelectCivilization, GameSetupScreen, InstalledPanel), then
  `MainMenu: vp-dll-dev: requested mods are already active` 145549.656 (about 10.6 s after the shutdown line). The same
  lines in the same order are in 0392's Lua.log (HI trace, exit by the game's menu).
- At the idle main menu nothing in `Logs\` was written after 07:17:04 (checked at 07:17:48 and again by the stop at
  07:23: Logs\ quiet 384.6 s). The game's post-load autosave (`AutoSave_Post_0008 BC-3680`, 07:16:53) was settled.
- Before the change `turns.py frontend` said BUSY "a game was loaded or is loading" for the whole 120 s after the exit;
  after it, IDLE "the main menu after a game". `cli.py -i c5h-c stop --human-turn-hook
  tools.harness.turns:stop_readiness` at 07:23:09: **exit 0** in 24 s, `settled.mode` `frontend`, no override, method
  `terminate` (no exit hook at the front end), no children, no orphans. Picture: Visual Checks, issue 0411 (DIAGNOSTIC,
  the main menu before the stop).
- **The 64-bit path [L, c5h-c, 07:35-07:42 EST].** The same run with the development proxy layout of the pin
  (`provision.py set-proxy` from `x64-04b1211b-pfdee656`: proxy and `VP64\server.exe`; fallback the plain
  `i0ship-26234371`; no shell composite). The proxy in the CP folder is the only game DLL mapped from the mod activation
  on, as the CP DLL above. Exit at 07:38:24; the shutdown line 146829.781, MainMenu 146841.890 (12.1 s after it);
  `frontend_state` busy "Logs\ written ... a load may be starting" until 07:39:07 and idle from 07:39:10 (36 s after the
  exit). server.exe (pid 59604, started at the activation, 07:37:09) stayed alive at the main menu and wrote nothing to
  `Logs\`. `cli.py stop --human-turn-hook tools.harness.turns:stop_readiness` at 07:41:22: **exit 0** in 22 s, mode
  `frontend`, method `terminate`; server.exe and its conhost ended by themselves 0.31 s after the EXE, no orphans.
  Afterwards `set-proxy --original`, the instance's own DLL `i0ship-bbfacca1-ia` (e589306e...) back with `set-dll`,
  `frontend.py restore`, `provision.py verify c5h-c` ok.

**Tests.** `tools/harness/tests/test_stop_main_menu.py` (offline, the Lua.log lines of the live run; 25 tests): idle after
the exit; busy for a load issued, a load screen, a game, the shutdown line without MainMenu, MainMenu without the
shutdown line, a new load or LoadScreen or InGame after the return, a second shutdown, a mod activation at that main
menu, Logs\ written within 30 s (Lua.log or another log), cache written, a journal held, an old Lua.log; `launch.stop`
stops after the exit without an override and after waiting for Logs\ to be quiet, refuses a game or load screen
(NeedsDecision), a box (DialogOpen) and a changing save (StopTimeout); `cli.py stop` exit 0 after the exit and 6 in a
frozen game.

**The product path's own logs (ticket 0506, 2026-11-17).** Found by 4758 [L, c5h-c, build a43d163fd, 15:25]: after
both HI journeys passed on the product route, `cli.py -i c5h-c stop --human-turn-hook tools.harness.turns:stop_readiness`
refused with exit 6, "main menu after a game, but Logs written 0.83 s ago (< 30 s)". Cause [C, source]: the proxy sets
`VPSHELL_OUT_DIR=<user>\Logs` for the product composite (0148, `proxy/src/proxy_shell.cpp`), and the composite appends
its `lockstep 1s:` and unitfilter lines to `Logs\shell_composite.log` every second, also at the idle main menu, so the
30-s rule could never pass there. The rule now leaves out the names in `turns.FE_PATCH_PERIODIC_LOGS` (fnmatch, any
case): `shell_composite.*` (every file the composite writes into its out folder carries the prefix: the log, its
per-process rotations `shell_composite.<time>.log` / `.<pid>.log` of 0385, the opt-in `.lockstep.csv`, `.lsprobe.csv`,
`.overlaytap.csv` records and dumps) and `vpproxy.log`. vpproxy.log is not in `Logs\` itself [C, source]: it goes to
the boot's run folder `<user>\Logs\VP64\run\<boot>\` (fallback `<user>\Logs\VP64\`), and server.exe writes server.log,
render.log and the DLL's logs only into that run folder; the check reads files directly in `Logs\` only, so those never
counted; vpproxy.log is listed for a `vpproxy.ini` `log=` that points into `Logs\`. A real load still blocks: MainMenu
re-executes when a load starts and writes Lua.log, and the game's own logs and `cache\` are still read. The stock route
(no composite) is unchanged. Tests (`test_stop_main_menu.py`, 0506 block, 2026-11-17: 115 passed with the other stop
suites; with the list emptied the new tests fail): a Logs folder whose only fresh files are the patch's passes; a fresh
Lua.log, a fresh game log (stopwatch.log, net_message_debug.log, a DLL CSV, Database.log, look-alike names) or cache
write beside them refuses; a stock-route folder gives the same age as before; `launch.stop` stops at that main menu while
shell_composite.log is rewritten at every poll. Not verified live on a product build (offline only).

**Not verified.** The full shipped system with the shell composite (D33 product path); the run above used the
development proxy layout. An exit that deactivates the mods (clicked mod set): the rebuild writes cache\ and Logs\,
which the rule waits out, but it was not run live. A load from the in-game menu (whether MainMenu prints between
the shutdown and the next LoadScreen): not run; the 30 s Logs\ wait covers a gap of 3-12.5 s as measured for loads from
the main menu.

## 17. The dialog watcher and the early return of every wait (ticket 0386, decision D155, 2026-11-15)

The owner's order D155 (through the Architect): no test game client sits on a crash or assert box. This section is parts 1
and 2 of `research/07-architecture-decisions/architect-2026-11-15-crash-box-watch.md` section 4 as built, and answers
two items of that file's section 6 (the poll cost, WerFault). Parts 3 (a close path for fatal boxes) and 4 (claims with
a reachable owner, escalation) are tickets 0387 and 0388. Code: `tools/harness/dialogwatch.py`; tests
`tools/harness/tests/test_dialogwatch.py` (41, offline) and `test_live_dialogwatch.py` (1, stand-in on a hidden
desktop). Branch `task-0386-dialog-watcher` (account B).

**The watcher [C, tests; L 2026-11-15].** `launch()` starts one per game process right after it records the process,
next to the audio mute watch: `pythonw -I tools/harness/dialogwatch.py watch --instance-dir DIR --pid P --create-time T
--period 15`, detached and windowless (`DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`, stdio closed). State file key
`dialog_watch` (watcher pid, period, marker path, log), harness.log event `dialog_watch`, its own log
`run\dialogwatch.log`. `launch(dialog_watch=False)` starts none. A marker an earlier process left (a watcher that died
before renaming it) is moved to `run\evidence\dialog_stale_<stamp>.json` before the new start (harness.log
`dialog_marker_stale`). Every 15 s it reads `crash.dialog_state(pid)`: the EXE's boxes and its descendants' (the server
the proxy starts, and WerFault.exe, see below). On a new box it:
- writes `run\dialog_open.json` (atomic replace): instance, game pid and creation time, first seen, the worst kind and
  its signature, and per box pid, process, child, hwnd, class, title, full text, buttons, kind, signature, a picture
  (`run\evidence\dialog_<stamp>_<hwnd>.png`, PrintWindow of the box; evidence, not put in the Visual Checks feed), and
  the queue entries posted;
- runs `crash.collect` once per marker (since the process's creation) into `run\crash\<stamp>_pid<pid>`;
- logs `dialog_open` (and `dialog_notify`) in `run\harness.log`;
- counts the signature in a machine-wide ledger, one file per signature: `<pool dir>\dialog_signatures\<sha1[:12]>.json`
  (signature, kind, count, first and last seen, instances, `bug`: null until someone files the tracker bug, an example
  text). Ten instances with the same assert make one file with count 10;
- posts one queue entry (kind note, `--by dialogwatch`) through `notify()`, routed by `route()`. (Corrected 2026-11-16 by
  ticket 0388: the routing is now section 17.2's, with the escalation to the lead after 15 min and the MC after 60 min.
  Was: the claim's ticket's holder or the claim's address when the ticket is open on the queue, else the MC's desk,
  with no escalation.) The MC's desk is issue 0136 `--to MC` (MC #2068, 2026-11-15; `C5_DIALOGWATCH_DESK`
  overrides the desk). A queue that cannot be reached (a test machine: the store is on the workstation) is recorded in
  the marker's `notified` list and in harness.log; nothing raises.

When the box is gone it renames the marker to `run\evidence\dialog_closed_<stamp>.json` (with `closed_why` "box gone" or
"process ended" and `open_s`) and logs `dialog_closed`. A failed window read (`dialog_state` "error") keeps the marker.
It presses nothing and stops nothing (no `PostMessage`, `SendMessage`, terminate or stop call in the module; checked by
a test and by `test_safety_static.py`'s scan). It ends with the process (pid + creation time) and outlives the session
that launched the game; it runs wherever the harness launches, the test machines included (it only reads windows). (Corrected 2026-11-15 by ticket 0387: the watcher now presses one thing, OK on a continuable assert of a run that declared
the assert policy record-and-continue, through `launch.dismiss_dialog`; section 17.1. Was: "It presses nothing".)

**Kinds, by the exact table [C].** `fatal`: the only button is OK and the box is one of: title "Error" whose text has
"the game will now crash" (the DLL's `CvPreconditionDlg`, `CvGameCoreUtils.cpp`), title "Render Error", title "Game
Crash" (the crash handler's box, which closes with OK), or a C runtime abort box (title "Microsoft Visual C++ Runtime
Library", text with "Runtime Error!"). `continuable`: title "Assertion Failed" with exactly OK and Cancel (the DLL's
release `CvAssertDlg`: `MB_OKCANCEL | MB_SYSTEMMODAL`; OK continues and silences that assert for the session, Cancel
traps). `patch`: class `VP64HelperPopup` (D118). (Corrected 2026-11-17 by ticket 0472: until then the watcher never
saw that window, because `crash.dialog_state` lists only `#32770` windows and titles with "crash"; the kind existed in
the table only. Since 0472 the watcher reads it separately; section 17.4.) `unknown`: everything else, WerFault's box included (not on the
table; part 3 (0387) left it off the fatal list: its button "Close program" is not OK, so it stays under the
coordinator's override; since 2026-11-18, ticket 0531, it becomes `crash` at the game's end when the proxy's
`exe-crash.txt` of that pid proves the crash, section 17.5). A marker with several boxes takes the worst kind (fatal, continuable, patch,
unknown, in that order) and that box's signature.

**Signature [C].** The DLL's boxes carry `Expression: X`, `File: Y`, `Line: N` lines (`FormatErrorDetail` in
`CvGameCoreUtils.cpp`): signature `Y:N X` with the file's base name (`CvPlayer.cpp:21771 m_fHappinessPolicies >= 0`).
The older one-line form `Detailed information: CvGlobals.cpp:7402 ...` is read too. Anything else: `<title>: <first
text line>`, or the title alone when the text is empty; a child's box is prefixed with its process (`WerFault.exe: Sid
Meier's Civilization V`). (Extended 2026-11-18 by ticket 0540, section 17.6: a box with VP's crash details ends in
` [<exception> at <module>+<Location (in file)> in file]`, the patch's popup in ` [<stage>: <code>]`, and the ledger's
`bug` of a known signature is filled from a table. Was: the forms above for every box, `bug` only by hand.)

**Part 2: every named wait returns at once [C, tests; L stand-in].** `dialogwatch.marker_dialog(inst)` reads the
marker of the instance's current launch (a marker of another game pid is ignored; one `stat` when there is none) and
returns a `crash.dialog_state`-like dict. It is read on every poll by `turns.end_turn` (before each try; status
`dialog`, exit 8, `DialogOpenError` with `check`), `turns.wait_turn` (each poll, after the process check; `WAIT_DIALOG`
8, before any stall wait or click), `frontend.ensure_in_game` (each iteration, before the probe; kind `dialog`) and
`frontend.app_dialog` (the marker first, the window read after), `lua.wait_ingame` (raises `lua.DialogOpen`,
`exit_code` 8), `driver.DriverRun` (`run.dialog()`, `run.check_dialog()`, `run.sleep(s)` raise `dialogwatch.DialogOpen`;
a failure report carries `dialog_marker`, `status` "dialog", `exit_code` 8), and `tools/wait/wait_for.py --instance
NAME` (repeatable; a name or an instance folder; new status `DIALOG`, exit 8, checked first at every poll; the box's
summary is printed). The message gives the box's kind, title, text, buttons, signature, marker path and first-seen time.
The latency becomes the watcher's period (15 s) instead of a wait's stall time.

**Live checks.**
- Stand-in, hidden desktop, 2026-11-15 22:55 and 23:02 EST [L] (`test_live_dialogwatch.py`, period 1 s by
  `C5_DIALOGWATCH_PERIOD_S`, `C5_DIALOGWATCH_NOTIFY=0`): the stand-in's NULL-owner box "Render Error"/OK was marked
  `fatal` within 1.3 s of its appearance; `wait_for.py --instance`, `ensure_in_game`, `end_turn` and `lua.wait_ingame`
  returned with it in 0.03-0.05 s for the four together; the marker was renamed "box gone" after the test code closed
  the box; the watcher ended with the stand-in (`dialog_watch_end`, "process ended"). The watcher takes `--desktop` so
  that its window reads see the hidden desktop; a game on the owner's desktop needs none.
- Real game, c5h-b (C:), 2026-11-15 22:57:39-23:00:11 EST [L]: front end only (no save loaded), the pin's
  `i0ship-26234371-ia` DLL (`3C2F47DA...`, already installed in the instance's MODS; unchanged, checked by hash after),
  launched by `cli.py launch` (frame cap 30, start-up watch ok, guard log read). The watcher (pid 61512) started with the
  launch, outlived the `cli.py launch` process (which ended at once), and ended "process ended" at its first poll after
  `cli.py stop --human-turn-hook tools.harness.turns:stop_readiness` (150.7 s, 10 polls); no marker was written (no
  box). `cli.py leftovers` clean.

**Cost of one poll [L, indication: the workstation with 8 registered heavy jobs, not a quiet measurement].**
`dialogwatch.py cost --pid P --n N` (wall and process CPU time of `crash.dialog_state` with the children scan):
- the real EXE at its front end (4 top-level windows, no child), n = 50: wall median 21.5 ms, p95 30.6, max 63.8; CPU
  mean 23.1 ms (Windows' process clock ticks by 15.6 ms). At one poll per 15 s: 0.15 % of one core per running game.
- the stand-in with its box open (the text read through `WM_GETTEXT`), n = 20: wall median 26.7 ms, p95 50.2; CPU mean
  24.2 ms; 0.16 % of a core.
The 15 s period stands; the guard-event wake (the plan's optional item) is not needed for cost.

**WerFault.exe [L, record; C].** Seen, as a child of the game's EXE: on the laptop on 2026-11-14 02:39 EST (ticket
3516's arm a, job `20261114-024300-01d3`; `D:/c5-64bit-work/3500/evidence/3516/laptop-eb2a-runtime.json`) the harness's
`crash.dialog_state` reported `message in child WerFault.exe (pid 14480): "Sid Meier's Civilization V" '' buttons
['Close program']` after the EXE's access violation at VA 0x60D80D, and the driver's wait ended with exit 8
(`release-driver.md` W3b.3). Windows starts `WerFault.exe -u -p <pid>` as a child of the faulting process, which waits
for it, so `dialog_state`'s descendants scan (psutil, by parent pid) finds it [I: the mechanism; L: the record]; the
watcher calls the same function and so marks it: kind `unknown`, signature `WerFault.exe: Sid Meier's Civilization V`
(while it is open; at the end it is counted as a crash when the proxy's record proves one: 17.5, ticket 0531, 2026-11-18).
Its text reads empty (a task dialog without Static children). No matching by WerFault's command line is needed. The
workstation would show WER's box too: `HKLM\Software\Microsoft\Windows\Windows Error Reporting` has no `DontShowUI` and
HKCU no override (read 2026-11-15 22:36). Not reproduced on the workstation for this ticket: making the real EXE fault
on purpose was not attempted (no safe trigger; a stand-in fault would put WER's box on the owner's desktop).

Not verified: a real DLL assert or precondition box marked live (the stand-in's box stands in; the classifier is tested
on the DLL's exact text); the queue post from a live watcher (offline tests with a fake queue; the live runs had
`C5_DIALOGWATCH_NOTIFY=0`); the watcher on oldpc or the laptop (it starts from `launch()`, which runs there; pythonw and
psutil there [I]); the box picture on a hidden desktop (PrintWindow from another desktop may give none; the marker then
has `picture: null`). `cli.py launch` does not print the watcher's pid (cli.py is outside this ticket; the state file
and harness.log have it).

### 17.1 The close path for fatal boxes and the declared assert policy (ticket 0387, D155 part 3, 2026-11-15)

Part 3 of `research/07-architecture-decisions/architect-2026-11-15-crash-box-watch.md` section 4, as built. It amends
D29 (only `launch.dismiss_dialog` presses a dialog's button) and D60 (the coordinator's override waives the human-turn
check) with two narrow waivers; COMMON rule 29 states the rule and the owner's duty. Branch `task-0387-fatal-box-close`
(account B). Code: `tools/harness/launch.py` (`dismiss_dialog`, `stop`, `fatal_dismissed`, `BOX_WAIVERS`),
`cli.py` (`dismiss --fatal`, `stop --after-fatal`, `launch --assert-policy`), `dialogwatch.py` (`assert_policy`,
`_policy_press`, `Watcher._continue`), `pool.py` (`claim --assert-policy`); stand-in `--msgbox-okcancel`. Tests:
`tools/harness/tests/test_safety_fatal_box.py` (offline), `test_safety_live_fatal_box.py` (stand-ins on a hidden
desktop), `test_safety_static.py` (the scan, extended).

**What the waivers waive, and what stays [C, tests].** Keyword `box_waiver` on `dismiss_dialog` and `stop`, off by
default, keyword only. Waived: the human-turn condition (and the front-end condition a silent Lua channel hands over
to) of the settled-save check, because a process on a fatal box, or blocked in a continuable assert, cannot answer it
(the 9071 case on 2026-11-15 20:52: `dismiss` refused "the settled-save check needs a human-turn check"). Kept: the
newest save unchanged for `settle_s` (10 s; `settled.mode` reads `fatal_box` / `assert_policy`), the recorded
process (pid + creation time + exe path), the dialog's owner re-read (the EXE or a recorded child), one dialog or an
`hwnd`, the exact button label, the re-read of the same box right before the press, and every refusal of `stop`
(a dialog still open refuses it). A waiver with `--coordinator-override` is refused: one decision at a time.

- **`dismiss --fatal`** (`box_waiver="fatal"`): the box must be of kind `fatal` by `dialogwatch.classify` (section 17's
  exact table) when first read and again right before the press (a box that changes kind during the settle is not
  pressed). If the instance has a pool claim, `--requested-by` must contain the claim's owner (case ignored; an
  unreadable claim refuses). An identity by name, not an authentication: it stops a session from closing a box on an
  instance someone else holds. Before the settle and the press, a record `outcome: "evidence"` (the box, kind,
  signature, claim) goes to `run\evidence\dialog_dismiss.jsonl` and `crash.collect` runs into the run folder. Refused:
  a continuable assert, the patch's popup (`VP64HelperPopup`), an unknown box: those stay under the coordinator's
  override. (Corrected 2026-11-18, ticket 0532: WerFault's box is no longer refused when the proxy's `exe-crash.txt`
  proves the game's crash; see the row below and 17.5. Was: "an unknown box (WerFault's included)".)
- **WER's "has stopped working" box with `--fatal` (ticket 0532, 2026-11-18) [C, tests]**: `cli.py -i NAME dismiss
  --button "Close program" --fatal --requested-by OWNER` closes it when all of `launch._wer_fatal` hold, checked when
  first read and again right before the press: the box is WerFault.exe's in a descendant of the game
  (`dialogwatch.is_werfault_box`); that process is a live child the harness recorded (`children.scan`: psutil's
  children by parent pid and creation order; pid + creation time), its recorded parent pid is the game's own pid (a
  WER box of `server.exe` is not the EXE's crash), it was created after the game and its image is `WerFault.exe`
  (never the image name alone); the game's run folder holds the proxy's `exe-crash.txt` naming the game's pid, not
  older than the game (0531's `game_crash_record`); the button is "Close program" (or Windows 7's "Close the
  program": `launch.WER_CLOSE_LABELS`), never "Debug" or "Check online for a solution and close the program". The
  box's kind stays `unknown` (the exact table is unchanged); the evidence records carry `fatal_proof: "werfault box +
  exe-crash.txt"`, `crash_record` and `crash_signature` (`exe-crash: <code> <name> at <module>+<RVA>`), and the
  harness.log `box_waiver` reason reads `fatal (werfault box + exe-crash.txt) box ...`. The claim owner, the evidence
  and `crash.collect` first, the save settle and the PID checks are as for the other fatal boxes. `stop --after-fatal`
  then works unchanged (the closed record has `box_waiver: fatal` and the game's pid and creation time; after "Close
  program" the EXE ends by itself with 255, so stop usually finds it gone and runs `crash_after`). Without the record,
  with a stale one, with any other button, for a WerFault.exe the game did not start, or for any other unknown box,
  `--fatal` refuses with the reason and the coordinator's override stays the route. A stop refused by WER's open box
  names this route in its message. Code `tools/harness/launch.py` (`_wer_fatal`, `WER_CLOSE_LABELS`,
  `dismiss_dialog`, `_dialog_refusal`), `cli.py` (help); tests `tools/harness/tests/test_safety_dismiss_fatal_werfault.py`
  (24, below 17.5).
- **`stop --after-fatal`** (`stop(box_waiver="fatal")`): only when `dialog_dismiss.jsonl` has a record with
  `box_waiver: fatal`, `outcome: closed` and this process's pid and creation time (`exe_create_time`, new in every
  record; `launch.fatal_dismissed`). Skips `stop_nolua` as the override does. After the process has ended (or when it
  ended by itself after the OK), `crash.collect` runs again (`crash_after`): the crash handler writes its dump after
  the box. A second box (the handler's "Game Crash") refuses the stop with a message naming `dismiss --fatal`.
- **The assert policy**: `hold` (default) or `record-and-continue`, declared at `pool.py claim --assert-policy` (claim
  field `assert_policy`) or `cli.py launch --assert-policy` (state file `assert_policy: {policy, declared_by: launch,
  pid}`; the launch's declaration first, for the recorded pid only; anything else counts as hold;
  `dialogwatch.assert_policy`). Under `record-and-continue` the watcher, on a new continuable box, calls
  `launch.dismiss_dialog(inst, "OK", hwnd=..., box_waiver="record-and-continue", settle_s=10)`; `dismiss_dialog`
  re-checks the kind, the declared policy and the button OK and writes the evidence first. On `closed` the watcher logs
  `dialog_continued`, counts the signature in the ledger, writes no marker and posts no queue entry, so no wait ends
  on it. A refused or failed press (`dialog_continue_failed`) is tried once; the box is then held as under `hold`
  (marker, entry). A box already held when the policy is declared is not pressed: the policy is declared before the
  run, not used to close a box after the fact.

**The one presser [C].** `tests/test_safety_static.py`: `box_waiver=` is passed only from `cli.py` `cmd_dismiss` /
`cmd_stop` (value `args.box_waiver`), `dialogwatch._policy_press` (the constant `"record-and-continue"`) and the
`test_safety_*` tests; never inside an except handler, a finally block, an exit/cleanup registration, a fixture or a
teardown (allowed files included); a parameter of that name exists only on `stop` / `dismiss_dialog`; the whole
strings `box_waiver`, `--fatal`, `--after-fatal` (an argv element, a `**{...}` key) appear only in `launch.py`,
`cli.py` and the refusal tests; and the list of callers is checked against the tree both ways. The existing
dialog-press scan still finds `PostMessageW` only in `dismiss_dialog`. `test_dialogwatch.py`'s source check no longer
forbids `dismiss_dialog` in the watcher; `test_safety_fatal_box.py` checks it is called once, from `_policy_press`.

**Live checks: stand-ins on a hidden desktop, 2026-11-15 23:47 EST [L]** (`test_safety_live_fatal_box.py`, 3 passed in
28.4 s; watcher period 1 s by `C5_DIALOGWATCH_PERIOD_S`, `C5_DIALOGWATCH_NOTIFY=0`, the test's own signature ledger;
`C5_HARNESS_TEST_ROOT=C:\c5-64bit-work\0387\lt`; stand-in rebuilt with `--msgbox-okcancel`):
- "Render Error"/OK (kind fatal): the real watcher wrote the marker; `cli.py dismiss --button OK --fatal --requested-by
  ... --settle 1` returned 0 in 1.2 s with the records `evidence` then `closed` (mode `fatal_box`), the stand-in logged
  `MSGBOX returned 1`, the watcher renamed the marker; `cli.py stop --after-fatal` returned 0 (method `terminate`: a
  stand-in, unlike the game, goes on after its box; `crash_after` present).
- "Assertion Failed" with OK and Cancel (kind continuable), policy hold: marker written, `dismiss --fatal` exit 2 ("of
  kind 'continuable', not 'fatal'"), nothing pressed over three more watcher periods, no `dialog_continued`.
- The same box with `launch(assert_policy="record-and-continue")`: the watcher answered OK by itself (stand-in log
  `MSGBOX returned 1`, i.e. OK, not Cancel), harness.log `dialog_continued`, no marker left, evidence `evidence` then
  `closed` (mode `assert_policy`, policy source `launch`), the stand-in still running afterwards.
The 0386 and D29 live tests (`test_live_dialogwatch.py`, `test_live_dismiss.py`, 4) pass on this branch.

**A trap met on the way [L].** `tests/conftest.py` gives live tests (real watcher, real lock and pool) only to files
named `test_live_*`; any other file gets a recorder in place of the dialog watcher, so a live test there never sees a
marker (the first run of this file failed "no marker" three times). This file must be named `test_safety_*` for the
static scan, so conftest now treats `test_safety_live_*` as live too (`_live()`).

**Offline [C].** `test_safety_fatal_box.py`: 42 tests (each fatal kind closed with `--fatal`, evidence before the press;
continuable, patch and unknown refused by `--fatal` and closed by the override; `--fatal` with the override refused;
an unknown waiver refused; the settle with a changing save; a foreign box; a process that is not the recorded one; the
claim owner; a box that changes kind during the settle; `stop --after-fatal` without, with another process's, and with
the right evidence; a second box after the OK; the CLI both ways; the policy's order launch > claim > hold and its
refusals; the watcher under both policies, other kinds, a failed press tried once). Full harness suite: see the
ticket's log for the count; the two failures there are ticket 0390's (on `main` before this branch).

**Not verified.** A real game's fatal box closed with `--fatal` (no safe way to make the game show one on purpose; the
stand-in's box is the same `#32770` MessageBox class, and the classifier is tested on the DLL's exact text); whether
the DLL's `CvAssertDlg` really does not show an assert again after OK (the Architect's statement, from the DLL source
`[I]`); the crash handler's dump arriving in `crash_after` after a real fatal OK; the watcher's press on oldpc or the
laptop (the queue-free path is the same; no live run there).

### 17.2 Claims with a ticket and a live address, escalation, no done ticket with a live claim (ticket 0388, D155 part 4, 2026-11-16)

Part 4 of `research/07-architecture-decisions/architect-2026-11-15-crash-box-watch.md` section 4, as built. It closes
F1 of that file ("no reachable owner": a claim's owner was a free string, the ticket closed while its games ran, nobody
was told). Branch `task-0388-claims-owner-escalation` (account B). Code: `tools/harness/pool.py` (`claim --ticket
--address`, `resolve`, `transfer`, `claims`, `claims_under`, `done_refusal`, `audit`, `loud_no_ticket`),
`tools/queue/owners.py` (`ticket_facts`, `tracker_front`, `finished_why`, `known_why`, `address_problem`, `lead_of`,
`norm_ticket`), `tools/harness/dialogwatch.py` (`route`, `post`, `notify`, `Watcher._escalate`, `escalate_text`),
`tools/queue/ticketq.py` (`live_claims_refusal` in `handback` and `accept`), `tracker/build.py` (`claim_report` in
`--check`). Tests: `tools/harness/tests/test_pool_tickets.py`, `test_dialogwatch_escalation.py` (offline),
`test_live_escalation.py` (stand-in on a hidden desktop, throw-away queue store and pool), `tools/queue/tests/
test_ticketq.py` class `LiveClaimsHoldDone`. The JD's loop item is `docs/briefs/jira-demon.md` section 3 item 7; the
MC's merge line is in `docs/MASTER-COORDINATOR-HANDOFF.md` section 4.4 step 6.

**The claim [C, tests].** `pool.py claim --owner O [--name N] --ticket ID [--address ADDR]` writes `ticket`, `address`,
`address_from` and `ticket_owner` into the claim file. Before any instance is taken, `resolve()` reads the ticket
(`owners.ticket_facts`: the queue row read-only, the tracker file on `main`, else `origin/main`, of this checkout, and
the board's seen map) and refuses (exit 2, nothing written): an id that is not 1-4 digits; a ticket neither on the
queue nor a tracker file on main; a finished ticket (`done` or `withdrawn` on the queue, `done` or `dropped` in its
tracker file); no address (none given, and the ticket has no owner); an address that is not one queue address, or a
broadcast (`dispatchers`, `mcs`, `ticketq`); an address that is not live by `owners.problem` (unknown on the board, or
last seen more than 8 h ago; `MC`, `MC-B`, `JD` are standing); and a store that cannot be read (an address that cannot
be checked is not accepted). The default address is the ticket's owner: the queue's effective owner while the ticket
is open there (`ticketq.py owner`: the holder of a claimed task, `lead-<epic>` of an epic once it acted, the reviewer
in review), else the tracker file's `owner`. For an account B task that is the holder, i.e. `dispatcher-B`.

**Without a ticket [C].** `claim` without `--ticket` still works (the Python API and every pre-0388 claim file
unchanged), with a LOUD line on stderr in `claim`, `renew` and `list` (`!! NO TICKET` in the row), a `no-ticket`
finding in `audit`, and its boxes going to the MC's desk with the same LOUD line in the entry. It is re-claimed in
place with `transfer NAME --by OWNER --ticket ID`, also while its game runs. For the done check and the audit, a claim
without a ticket field is read as working for the first 4-digit id in its owner (`impl-B-0121` -> 0121, `4203` ->
4203); the note is not read (the watcher's routing still reads it, as in 0386).

**`transfer` [C, tests].** `pool.py transfer NAME --by WHO [--to OWNER] [--ticket ID] [--address ADDR] [--hours H]
[--why TEXT]`. Allowed to the claim's owner (`--by` equal to it) and to a coordinator (`MC`, `MC-B`; `--why`
required). The ticket defaults to the claim's own (refused when it has none), the address to the ticket's owner; both
pass `resolve()` as at a claim. The lease is kept unless `--hours`. The claim file is replaced atomically only if it
still holds the same owner and claim time; every transfer is kept in its `transfers` list (from/to owner, ticket,
address, by, why, time). A refusal leaves the file byte for byte unchanged (tested). It never checks the game: that is
the point (a run that outlives its ticket is handed over).

**Routing and escalation of a box [C, tests; L 2026-11-16].** `route()` (dialogwatch) returns the entry's thread, its
addressee and the lead:
- the claim's ticket open on the queue: to the claim's `address` (else the ticket's owner, holder or answers) on the
  ticket's thread; the lead is `owners.lead_of(address, owner, holder, kind, account)`: the first of the ticket's
  owner, its holder, the account's dispatcher (a task), the account's MC (`MC` for A, `MC-B` for B), `MC` that is not
  the address itself. When the address is not live (`ticketq.py addresses`: known and seen within 8 h), the lead is
  told at once (`lead_now`); when the board cannot be read, no guess (15 min as usual);
- the claim names an address but its ticket is not open on the queue (done, withdrawn, not published): to the address
  on the MC's desk, and the MC at once as the lead;
- no claim, or no address at all: the MC's desk, to the MC (as in 0386).
The watcher (`Watcher._escalate`, after each poll that still sees the box) posts one entry to the lead when the box is
older than `LEAD_AFTER_S` = 900 s (at once with `lead_now`), and one to the MC on its desk when older than
`MC_AFTER_S` = 3600 s; nobody who already had an entry for the box is told again (a lead equal to the address, or the
MC as the lead, is skipped). Each is recorded in the marker (`escalations`: level, issue, to, id, age, time) and in
harness.log (`dialog_escalate`). A box closed before 15 min is not escalated. `C5_DIALOGWATCH_LEAD_S` /
`C5_DIALOGWATCH_MC_S` replace the delays (the live test's 4 s and 8 s; nothing else sets them).

**No done ticket with a live claim [C, tests; L 2026-11-16].** `pool.done_refusal(ID)`: the claims under the ticket
(its `ticket` field, or the id in a pre-0388 owner) whose instance has a live process (`pool.alive`). The queue's
`handback` and `accept` refuse with it (exit 2) before their write transaction; a pool that cannot be read warns and
passes (a test machine). `tracker/build.py --check` fails (exit 1, `error:`) on a `done` or `dropped` issue with a live
claim whose `ticket` field names it, and warns when the id is only read from the owner (so an old claim does not stop
every check on main). A run that goes on after its ticket closes is transferred first.

**Cost of a check [L 2026-11-16 00:2x EST, workstation busy].** One `psutil` process listing with image paths took
5.4 s; before this ticket `pool.alive()` listed the processes once per instance (4 claims: 18.8 s). `claims()` and
`audit()` now list once per call (4.5 s), and `claims_under` lists only when a claim matches the ticket: a hand-back
with no claim under it adds 0.01 s, one with a claim about 4 s.

**The audit, the JD's item [C, tests; L 2026-11-16].** `pool.py audit [--json] [--box-age-min 60]`, exit 0 none, 3
findings: `box-open` (the watcher's marker older than an hour on a live game: the marker's `game_pid` among the
instance's processes), `ticket-finished`, `address-not-live`, `no-ticket`, `unreadable`. On the real pool at
2026-11-16 00:16 EST it reported four `no-ticket` claims (c5h-b `impl-B-0339`, c5h-c `0375`, c5h-g and c5h-i
`impl-9071`) and nothing else.

**Offline [C], 2026-11-16.** `test_pool_tickets.py` 23 passed, `test_dialogwatch_escalation.py` 14, queue class
`LiveClaimsHoldDone` 3. Full harness suite 871 passed, 2 failed (both ticket 0390's, on main before this branch), 23
skipped; queue suite 56 OK (the tests now run with a pool of their own, `setUpModule`); tracker server tests 42 passed.

**Live checks, 2026-11-16 [L].**
- Stand-in on a hidden desktop (`test_live_escalation.py`, 1 passed in 40.3 s at 00:46 EST; a throw-away queue store
  and git repository with fixture ticket 9996 claimed by `impl-live` for account B and desk 9995; a throw-away pool;
  watcher period 1 s, lead after 4 s, MC after 8 s by the environment; `C5_HARNESS_TEST_ROOT=C:\c5-64bit-work\0388\lt`):
  `pool.claim(ticket="9996")` resolved the address to `impl-live` (the holder) through the real `owners` reading of
  the store; a transfer to an unknown address was refused; the real detached watcher posted entry #4 "DIALOG fatal on
  wa ..." to `impl-live` on 9996's thread, #5 "ESCALATION (4 s)" to `dispatcher-B` on the same thread (the lead: the
  address is the ticket's owner and holder, so the account's dispatcher), #6 "ESCALATION (8 s)" to `MC` on desk 9995;
  the marker's `escalations` held both with their entry ids; `ticketq.py handback 9996` exited 2 "9996 has a live game
  under its claim: wa (owner impl-live, pid 23760)"; `pool.audit(box_age_s=1)` reported `box-open`; after the test
  closed the box and stopped the stand-in, the watcher ended and the same hand-back went to `review`. The 0386 live
  test (`test_live_dialogwatch.py`) passes on this branch.
- The real pool and store, 00:35 EST, no launch: `pool.py claim --name c5h-a --ticket 0388 --address ghost-0388`
  exit 2 ("not an address on the message board"); `--ticket 4203` exit 2 ("4203 is done in its tracker file on
  main"); `--ticket 0388` claimed c5h-a with the address `dispatcher-B` ("the ticket's owner (the holder)");
  `transfer --by someone-else` exit 2; `transfer --by impl-B-0388 --address MC-B` recorded one transfer; released.
  `tracker/build.py --check` on main's 650 issues with the real pool: exit 0, 4.5 s.

**Not verified.** A real game's box escalated (the stand-in's box is the same `#32770` MessageBox; the routing and the
delays do not depend on the process); the live address check against the real store's seen map at a box (the live
test uses a throw-away store; the real store was read by `claim`/`audit` only); the JD's use of `audit` in its loop
(the brief is written, not run).

### 17.3 A crash without a box (ticket 0419, D118 part 2, proposal 1c of 0410, 2026-11-16)

0410's run5nd ended with 0xC0000005 in the EXE and showed no box (`02-exe-internals/crashes-huge-load.md` 4); the
watcher logged `dialog_watch_end ... process ended`, as for a normal stop, and the driver probed a dead channel for
900 s. Code: `tools/harness/dialogwatch.py`; tests `tools/harness/tests/test_dialogwatch_boxless_crash.py`.

**What the watcher does now [C, tests].**
- **The exit code.** At its start the watcher opens a handle on the game (`ExitCodeReader`: `OpenProcess(SYNCHRONIZE |
  PROCESS_QUERY_LIMITED_INFORMATION)`, the creation time checked against the launch's, so a reused pid is refused), so the
  exit code stays readable after the end. Its sleep between polls waits on that handle: it wakes when the game ends, not
  up to 15 s later (the real-child test: the watcher, with a 5 s period, had ended and written its records less than 4.5 s after the child ended). `dialog_watch_end` now carries `exit_code`,
  `exit_code_hex`, `crash` and `boxes_seen`. A handle that cannot be opened is one `dialog_watch_error` line and the code
  is then unknown (treated as no crash).
- **Which codes are a crash**: `is_crash_code`, the server's rule (`03-dll-engine-boundary/error-report-exe-crash.md` 2):
  NTSTATUS error severity except 0xC000013A, and 0x80000002-0x80000004. Not: 0, 1, 3, 5, 6, 15, 0x8000DEAD, 0x40010004,
  259, unknown.
- **A crash code and no box seen in the watcher's life** (`boxes_seen == 0`; a box, held or answered by the assert
  policy, means the box path already told the owner) is recorded like a fatal box:
  - `crash.collect` into `run\crash\<stamp>_pid<pid>`;
  - the marker `run\dialog_open.json` with `kind: "crash"`, `exit_code`, and one stand-in box: title `(no box)`, no
    buttons, no window, `kind: "crash"`, the text "the game process (<exe>, pid N) ended with exit code 0x... <name> and
    showed no box", then the proxy's crash record (`<player>\Logs\VP64\run\<stamp>-<pid>-<n>\exe-crash.txt`, found by
    the pid in the run folder's name; or that the run folder has none, or that there is no run folder of the pid) and
    the server's newest report folder (`<player>\VP64\reports`, made after the game's start);
  - the signature `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at CivilizationV_DX11.exe+0x37C6DA` (the place from
    the record's first line; "(no proxy crash record)" without one), counted in the signature ledger;
  - one queue entry through `notify()`, routed by `route()` as a box's: "CRASH (no box) on <instance> (game pid N): ...
    The owner, in its next turn: nothing to close; relaunch or release the instance, and fix the cause or file the bug
    against the signature ..."; the lead at once only when `route()` says the address is not live (the watcher ends with
    the game, so the 15 and 60 minute escalations do not run for a crash);
  - the log line `dialog_crash_exit` (code, name, signature, record, report, ledger count), then `dialog_notify`.
- **The marker stays**: `marker_dialog()` returns it with `kind: "crash"` and the summary "crash without a box: ...", so
  every wait that reads it (turns, frontend, lua, driver, `wait_for.py --instance`) ends with exit 8 and that text;
  `DialogOpen`'s message says "the game process crashed without a box (nothing to press ...)". The next launch moves it
  to `run\evidence\dialog_stale_<stamp>.json` (`archive_stale_marker`). `pool.py audit` ignores it (the game is gone).
  `cli.py dismiss` has nothing to press.
- **Unchanged**: a box seen before the end (a fatal box, then the end) follows 17-17.2; the watcher still presses and
  ends nothing (`test_watcher_source_presses_and_stops_nothing` passes).

**Tests [L offline, 2026-11-16, workstation].** `python -I -m pytest tools/harness/tests/test_dialogwatch_boxless_crash.py
tools/harness/tests/test_dialogwatch.py`: **83 passed** (11:36). The crash codes and names; a boxless 0xC0000005 with a
record and a report (marker, box, signature, ledger count 2 over two instances, one entry, the log lines); the marker
read by `check`, `turns.wait_turn`, `lua.wait_ingame` and `wait_for` (exit 8), and moved at the next launch; no marker
for 0, 3, 15, 1, 0xC000013A or an unknown code; a box before the end; no record, and a run folder without one; no
notify and failing readers; the entry's text and route; `proxy_record` and `server_report` in a player folder; and two
real windowless children: `ExitCodeReader` on a child that ends with 0xC0000005 (and a refused reused pid), and the real
detached watcher on such a child with a planted record (signature with the record's place; ended within 4.5 s of the
child's end, under its 5 s period) and on a child that ends with 0 (nothing left). The whole harness suite
(`python -I -m pytest tools/harness/tests`, main merged at 11:53): **954 passed, 24 skipped** (the live files), 11:54-12:04;
`test_safety_static.py` alone: 53 passed.

**Not verified.** A real game's boxless crash (no game was crashed: the EXE cannot be made to fault without a code
patch, D4); the entry reaching a live address on the real queue (the route is the box path's, tested with a queue
double).

*Added 2026-11-20 (0602, 11.9 (c)) [L]:* a watcher that opens its game only after the game ended reads the exit code but
not the image (`QueryFullProcessImageNameW` fails on an ended process), polls 0 times, and its stand-in box then names
`crash_box`'s literal default `CivilizationV_DX11.exe`, whatever the process was (draft NEW-0602-1).

### 17.4 The patch's own popup (VP64HelperPopup, D118) for dismiss and the watcher, also after the game ended (ticket 0472, 2026-11-17)

Found by epic 2600's worker C (#3415, #3419, 2026-11-17 01:34): after a crash in VP's modpack form the patch's D118
popup stayed on the workstation; `cli.py dismiss --button Close` (also with `--hwnd 0x171656` and the coordinator's
override) answered "no dialog with hwnd ... in the instance's processes"; the helper (vp64helper.exe pid 20212) was an
orphan, its game (pid 64880) ended by a Lua-free stop. The MC had to approve a one-off window message. Code:
`tools/harness/dialogwatch.py` (`game_identity`, `process_snapshot`, `helper_log_pids`, `patch_processes`,
`patch_popups`, the watcher's `_tail`), `launch.py` (`dismiss_dialog`, `stop` keeps `last_game`, `status` lists
`patch_popups`), `cli.py` (help text); tests `tools/harness/tests/test_safety_dismiss_helper_popup.py`.

**Why it was invisible [C, code read 2026-11-17].**
- `crash.dialog_state` (used by dismiss, stop, status and the watcher) takes only visible `#32770` windows and windows
  whose title holds "crash" (`crash.pick_dialogs`). The helper's window is a plain top-level window of its own class
  `VP64HelperPopup` (`proxy/helper/vp64helper.cpp` `ShowWindowed`: `WS_CAPTION | WS_SYSMENU | WS_POPUP`,
  `WS_EX_TOPMOST | WS_EX_DLGMODALFRAME`, title "Vox Populi 64-bit patch: error report"), its text in a read-only
  `EDIT` control, buttons "Open folder" (control id 101; disabled without a report folder), "Try again next start"
  (103, D33 only; it writes `vpproxy.ini`) and "Close" (102, the default button; `WM_COMMAND` 102 or `IDCANCEL` ->
  `DestroyWindow` -> the helper exits). So neither dismiss nor the watcher ever saw it, even with the game alive:
  section 17's kind `patch` existed in the table only.
- The helper is started detached (`DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`, out of the EXE's job when it may) by
  the proxy inside the EXE (`proxy_session.cpp StartHelper`: `popup` in a stock session, `failed --pid <EXE pid>` in a
  D33 session, which waits for the EXE to end and only then shows its popup) or by `server.exe`
  (`srv_exe_crash.cpp StartHelper`: exe-crash, dll-fatal; the report and the helper took 327 ms after the crash,
  `03-dll-engine-boundary/error-report-exe-crash.md`). Its parent pid is the EXE's or the server's, and it outlives
  both. dismiss needed a live recorded EXE (`recorded_process`), and the watcher ended with the game.
- `stop` is not held up by the popup (it does not see it either); 2600's stop ended the game and left the popup. That is
  unchanged: the popup belongs to a separate process that holds no game data.

**Which process is the instance's [C, tests].** `dialogwatch.patch_processes(inst)`, from the game recorded for the
instance (`game_identity`: the state file's `pid` + `create_time`, else `last_game`, which `stop()` now writes when it
clears them; the next launch replaces the state file). A live process is the game's when it is
- a child recorded in the state file (`children.scan`) with the recorded creation time, or one the watcher remembered
  from its own earlier polls (same form, in memory only);
- or its parent pid is the game's, a recorded child's or a remembered one's, and it was created after that parent; when
  the parent's pid now belongs to another live process created before the candidate, it is refused (that process could
  be its parent); and transitively a process created after a matched live parent (the server's helper while the server
  runs);
- or it is the pid named by the game's own run folder: the line `helper: started ...: pid N (` of the proxy
  (`vpproxy.log`) or the server (`server.log`, opened in the server's working folder, which `srv_main.cpp` sets to its
  configured `run_dir`, the proxy's run folder [I: from the code, not checked in a live run]) in
  `<player>\Logs\VP64\run\<stamp>-<game pid>-<n>\*.log` (last 4 MB of each), created after the game, with image
  `vp64helper.exe` (the image name is only a second condition here).
Never by image name or window class alone: a VP64HelperPopup of a process the game did not start is not found (tested
with a second fake helper started by the test). `patch_popups(inst)` returns the visible `VP64HelperPopup` windows of
those processes in `dialog_state`'s form, `text` = the EDIT control's text, plus `pid`, `process`, `child: true`,
`via` (which rule matched) and `process_create_time`.

**Cost [L 2026-11-17 01:55 EST, workstation, about 600 processes, the machine busy].** `psutil.process_iter` with
`pid, ppid, create_time, name` took 23.9 s, with `ppid` alone 6.7 s; psutil's internal `ppid_map` 18 ms; one Toolhelp32
snapshot (`CreateToolhelp32Snapshot` + `Process32NextW`, pid, parent pid, image) 29-36 ms; `psutil.Process(pid).create_time()`
0.06-0.09 ms. So `process_snapshot()` takes one Toolhelp32 snapshot and reads creation times only for candidates. Never
use `process_iter` in a watcher poll on this machine.

**dismiss [C, tests].** `dismiss_dialog` adds `patch_popups(inst)` to the dialogs it finds (by hwnd, without
duplicates).
- With the game alive: as for any box (the human-turn check or `--coordinator-override`; the settle, the exact
  button, the re-read before the press). The popup's process passes when it is the EXE or a live recorded child, or one
  of `patch_processes` (pid and creation time re-read before the press).
- With the game ended (no live recorded process, but a recorded game): only a patch popup of that game can be pressed,
  with `--coordinator-override REASON` or, since ticket 0479 (2026-11-17), by the claim's owner with `--patch-popup`
  (box waiver `patch`, below); `--fatal` and the assert policy refuse it by kind as before. The newest save must still
  hold still for `--settle` seconds; the wait ends refused if the popup's own process ends. The refusal without a popup
  keeps the words "not running a process launch() recorded". (Until 0479 the override alone was accepted here.)
- **Box waiver `patch` [C, tests; ticket 0479, the MC's answer #3447 of 2026-11-17 01:51].** `cli.py -i NAME dismiss
  --button Close --patch-popup --requested-by "OWNER: why"` (`launch.dismiss_dialog(box_waiver="patch")`): the
  claim's owner closes the popup of an ENDED game without a coordinator, as `--fatal` for a fatal box (the game cannot
  go on; the helper holds no game data). Conditions, each a refusal with nothing pressed: the game has ended (with it
  alive: "still runs", nothing changes there); the box is of kind `patch` (`dialogwatch.classify`), its process one of
  `patch_processes` of THIS instance's recorded game (pid and creation time, never the image name; another instance's
  game finds no popup); the button is Close (`PATCH_NEVER` refuses "Open folder" / "Try again next start" first, then
  the exact-button check, then the waiver's own Close-only check); if the instance has a pool claim, `--requested-by`
  names its owner (`_claim_owner_refusal`, as `--fatal`; an instance outside the pool has no owner check); never
  together with `--coordinator-override`. Evidence first as for the other waivers (a record `evidence` with the box,
  its kind, signature and the claim, and `crash.collect` into the run folder), then the newest save must hold still for
  `--settle` s (skip mode `patch_box`: only the human-turn / front-end conditions are waived, which an ended game
  cannot answer anyway). Logged: harness.log `box_waiver` (`waiver: "patch"`, the reason names the box's signature and
  `--requested-by`), and the `closed` record has `box_waiver: "patch"`, `game: "ended"`, `settled.mode: "patch_box"`.
  `stop` does not take it (`--after-fatal` only; an ended game needs no stop). The watcher's entry for a `patch` box now
  names both commands (coordinator's override while the game runs; the owner's `--patch-popup` after it ended).
  `test_safety_static.py` flags `--patch-popup` as a whole string constant outside launch.py, cli.py and the refusal
  tests, like `--fatal`; the `box_waiver=` callers are unchanged (cli.py passes `args.box_waiver`).
  Tests [L offline, 2026-11-17 03:30-03:51 EST, workstation]: six in `test_safety_dismiss_helper_popup.py` with the real
  fake helper on a hidden desktop (owner closes after the game ended; another `--requested-by` refused; Open folder,
  Try again next start and OK refused; no pool claim as `--fatal`; refused while the game runs; another instance's
  ended game finds no popup, also by hwnd; `stop` does not take `patch`; the CLI flag), the file 27 passed,
  `test_safety_static.py` 55 passed, the whole harness suite 1470 passed / 54 skipped / 1 failed (the real watcher's
  "polls >= 3" timing under the suite's load, passed 3 of 3 alone). Evidence `evidence/0479/acceptance.txt`.
- Never pressed on the popup: "Open folder" (it opens Explorer on the desktop) and "Try again next start" (it writes
  `vpproxy.ini`): `launch.PATCH_NEVER`, refused before anything else. "Close" closes it.
- The evidence record (`run\evidence\dialog_dismiss.jsonl`) has `game: "running" | "ended"`, `helper_pid`,
  `helper_create_time` and `via`. The press stays the one `PostMessageW` in `dismiss_dialog` (`test_safety_static.py`).
- Command, on the coordinator's word: `python -I tools/harness/cli.py -i NAME dismiss --button Close --requested-by
  "WHO: why" --coordinator-override "REASON"`. `cli.py -i NAME status` lists `patch_popups` (hwnd, pid, via), also
  after the game ended; `dialogwatch.py once --instance NAME` prints them too.

**The D29 / D155 rules for this window (as implemented 2026-11-17; corrected 2026-11-17 by ticket 0479 after the MC's
answer #3447: the claim's owner may close the popup of an ended game; it replaced "the popup stays under the
coordinator's override" in every case).**
- D29: only `launch.dismiss_dialog` presses its button; never a window message from a script, never a kill of the
  helper by the harness.
- D60 / rule 29: while the game runs the popup is under the coordinator's override (the normal checks may stand in for
  it as for any box); after the game ended the claim's owner may close it with `--patch-popup` (above).
- D155: the watcher holds it as the instance's box (below); the owner reads the report folder named in its text, closes
  it (after the game ended) or asks the coordinator (while it runs), and files the failure (the popup is the D118
  report of a failure of the 64-bit path).
- Only Close is ever pressed.

**The watcher [C, tests].** Each poll adds `patch_popups` (one Toolhelp32 snapshot per poll) to `dialog_state`'s
boxes: a popup is a box of kind `patch` (signature `vp64helper.exe: <title>: <first text line>`), held like any box:
marker, crash.collect, picture, ledger, one entry to the claim's owner through `route()`, the 15/60-minute escalation.
The entry's text names the close command. The processes of the game seen at each poll are remembered by pid and
creation time. When the game ends:
- a patch popup open at that moment is handed over (no `dialog_closed` and no second entry for the same window);
- the boxless-crash record of 0419 runs as before (a popup seen before the end counts as a box);
- then the tail (`dialog_watch_tail`): while any process of the game runs (the helper, or the server that will start
  one), the watcher polls every 2 s (`HELPER_TAIL_PERIOD_S`); a popup that appears is held as above; when it closes
  the marker is renamed as usual (`dialog_closed`); when a 0419 crash marker exists it stays and carries the popup
  beside its stand-in box (kind `patch` while the popup is open, `crash` again after); the tail ends when no process of
  the game is left (`dialog_watch_tail_end`, "the game's processes ended"). Without such a process nothing runs, so a
  game that ends alone still ends its watcher at once (0419's 4.5 s check passes).
Remaining gap [I]: a server that was never seen by a poll or a scan (it started less than 15 s before the game
ended) and that starts the helper and exits before the tail's first poll leaves a helper whose parent nobody recorded;
then only the server's `helper: started` log line ties it to the game (dismiss and the watcher both read it). Not
seen in a real run: no real D118 popup was raised for this ticket.

**dialogs.py is not part of dismiss.** `tools/harness/dialogs.py` (in 0472's `touches`) is the template matcher for the
game's own leader and deal screens (issue 1117); it presses nothing on Windows dialogs and was left unchanged.

**Tests [L offline, 2026-11-17, workstation; nothing of a game, Steam or an instance used].**
`python -I -m pytest tools/harness/tests/test_safety_dismiss_helper_popup.py`: **21 passed** in 10.4 s (02:16 EST).
Real windows: a fake helper (python + ctypes, its source in the test file, written to `tmp_path`) attaches to a hidden
desktop the test creates (and exits without a window when it cannot: rule 14), registers `VP64HelperPopup` and shows
the EDIT text and "Open folder" (101) / "Close" (102); a fake game (a sleeping python process, recorded in the state file
as `launch()` records the EXE) starts it, as the proxy starts the helper. Checked: `patch_popups` reads class, title,
buttons and the EDIT text, via "started by the game"; with the game alive "Open folder" and "Try again next start" are
refused and Close is pressed (control id 102, the fake's `close_pressed`, never `open_pressed`, the game untouched);
with the game killed the press needs the override (refused without it, refused with `--fatal` as kind `patch`, "Open
folder" refused with it), then Close closes it (evidence: three refusals and `closed`, `game: "ended"`); after a
`stop`-like state (`pid` cleared, `last_game` kept) it is still found; a second fake helper started by the test, not by
the game, is never found (by hwnd: "no dialog"), and with the game and its helper gone the refusal says "not running a
process launch() recorded"; the real watcher's `step` writes a `patch` marker and one entry. Offline, with doubles: the
matching rules (after the game; older than the game refused; a reused parent pid refused; a recorded child's helper
after the server ended; a recorded child only with its creation time; the log-named pid only with the image name and
the order; image name alone never; no recorded game -> nothing), `helper_log_pids` on a player folder, `game_identity`,
and the watcher (popup before the end handed to the tail and closed there with one entry; a popup after a 0419 crash
marker, which stays; the D33 popup shown after the end; no tail without a process of the game; the remembered
processes). The whole harness suite (`python -I -m pytest tools/harness/tests`, merged main 36f1134df):
**1465 passed, 49 skipped** (the live files), 0 failed, 02:04-02:12 EST; no fake process left afterwards.

**Not verified.** A real D118 popup of vp64helper.exe (none was raised: that needs a failing 64-bit path on an
instance), the real `server.log` line of an exe-crash helper in the run folder, and the live D155 queue entry on the
real queue (the route is the box path's, tested with doubles).

### 17.5 WER's "has stopped working" box of a game that left the proxy's crash record is a crash (ticket 0531, 2026-11-18)

From 0519's dialogwatch part (`02-exe-internals/crashes-huge-load.md` 8.1 and 8.4): on the laptop, where Windows' error
reporting shows its box, 0428's huge load crashed the EXE (0xC0000005 at CivilizationV_DX11.exe+0x136580); the proxy's
filter wrote `exe-crash.txt`, WerFault.exe showed "Sid Meier's Civilization V" with the one button "Close program", and
after that press the EXE ended with **exit 255**, not the exception code. The watcher held the box as kind `unknown`
and ended with `crash: false`, so crash rates (D185) were understated on WER machines. Code: `tools/harness/dialogwatch.py`
(`is_werfault_box`, `read_crash_record`, `game_crash_record`, `record_signature`, `Watcher._wer_crash_end`); tests
`tools/harness/tests/test_dialogwatch_werfault_record.py`. Harness only: no proxy or server change (0519 does the
server's D119 report on 255).

**What the watcher records, by situation [C, tests]** (the dialog table; rows of 17, 17.3 and 17.4 for comparison):

| Situation | Kind while open | At the end | Ledger signature | `dialog_watch_end` `crash` |
|---|---|---|---|---|
| a box on the exact table (fatal, continuable, patch) | its kind | marker renamed `dialog_closed_*` (box gone / process ended) | the box's | `is_crash_code(exit)` |
| the game ends with a crash code, no box in the watcher's life (0419) | - | crash marker (kind `crash`, stand-in box) stays for the waits | `exe-crash: <code> <name> at <module>+<RVA>` | true |
| **WerFault.exe box, then exit 255 (or a crash code, or the box still open at the end with any code but 0), and the game's run folder holds the proxy's `exe-crash.txt` naming its pid (0531, 2026-11-18)** | `unknown` (classify is unchanged: the box alone proves nothing) | the closed marker is rewritten: the WerFault box and the record take kind `crash` and the record's signature (`kind_seen`/`signature_seen` keep what the table said); `crash_record`, `crash_proof: "werfault box + exe-crash.txt"`; log line `dialog_wer_crash` | the box's (`WerFault.exe: Sid Meier's Civilization V`, at sight) **and** the record's `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at CivilizationV_DX11.exe+0x136580`, with `crash: true`, `exception_code`, `exception_address` | **true**, with `exception_code`, `exception_address`, `exception_va`, `proxy_record`, `signature`, `crash_proof` |
| the same WerFault box with no record, a run folder without one, a stale record, or exit 0 (0531) | `unknown` | as before (marker renamed, kind `unknown`); one `dialog_wer_no_record` line says why (not for exit 0) | the box's only | `is_crash_code(exit)` (false for 255) |

- **Which record counts.** `proxy_record` finds the run folder by the pid in its name
  (`<player>\Logs\VP64\run\<stamp>-<pid>-<n>\exe-crash.txt`, 17.3); `game_crash_record` then reads the file
  (`proxy_crash_filter.cpp` FormatRecord: first line `exception 0x... <name> at <module>+<RVA> (VA 0x...), thread N`,
  then `key: value` lines) and takes it only when its `process:` line names the game's pid and the file is not older
  than the game's creation time (minus 1 s). A record naming another pid, one without a `process:` line (the filter's
  "no exception record" form), or an older one is stale and ignored.
- **WerFault box**: a box of process `WerFault.exe` (any case) that `crash.dialog_state` reported as a child of the game
  (Windows starts it as the faulting process's child, 17). The record is written before WER runs, so it is there while
  the box is open; the decision is still made at the end, because the ticket's exit-0 case keeps the box `unknown`.
- **crash.collect runs once in all**: at the box's sighting (as for every box); the end runs it only if that sighting
  left no collection in the marker.
- **No new queue entry** at the end: the box's sighting already posted one (kind `unknown`) to the claim's owner.
- **Closed as the other crash kinds are (D155)**: the box is held while open (marker, waits end with exit 8, entry,
  escalation) and the marker is renamed when the box is gone or the game ended, as for a fatal box. **Closing it
  (corrected 2026-11-18, ticket 0532)**: with the record, the claim's owner closes it with `cli.py -i NAME dismiss
  --button "Close program" --fatal --requested-by OWNER`, then `stop --after-fatal` (17.1's WER row; COMMON 29's fatal
  list). Was: "`cli.py dismiss --fatal` still refuses the WerFault box ... it stays under the coordinator's override".

| Box | `dismiss --fatal` (0532, 2026-11-18) | Then |
|---|---|---|
| WerFault.exe's box, WerFault a recorded child of the game itself (parent pid, created after it), the game's run folder holds `exe-crash.txt` naming its pid, not older than the game; button "Close program" / "Close the program" | **closed** by the claim's owner; evidence `fatal_proof`, `crash_record`, `crash_signature`; `box_kind` stays `unknown` | `stop --after-fatal` (usually "not running" + `crash_after`: the EXE ends with 255 by itself) |
| the same box without the record, a run folder without one, a record naming another pid or older than the game, or a record gone by the re-read before the press | refused, with `game_crash_record`'s reason | the coordinator's override |
| the same box with "Debug", "Debug the program" or "Check online for a solution and close the program" | refused ("ends the program") | "Close program" with --fatal, or the override |
| a WerFault.exe box of another process (started by `server.exe` or another descendant, not recorded by the harness, a recorded child whose image is not WerFault.exe, older than the game), or any other unknown box | refused | the coordinator's override |

**Tests [C, offline, 2026-11-18 03:55-03:58 EST, workstation; no game, Steam or instance used].**
`tools/harness/tests/test_safety_dismiss_fatal_werfault.py` (named `test_safety_*` because `test_safety_static.py`
allows `box_waiver=` and `--fatal` only there): **24 passed**, on 0387's doubles (a sleeping python child as the game,
a second one recorded as its WerFault.exe child, the box as `crash.dialog_state` read it on the laptop, the record in
the proxy's format in a fake player folder; PostMessageW recorded, never sent). With `test_safety_fatal_box.py`,
`test_safety_dismiss_helper_popup.py` and `test_dialogwatch_werfault_record.py`: 112 passed; `test_safety_static.py`
55 passed; the other offline suites that reach `dismiss_dialog` or the stop's dialog refusal (`test_cli_exit_codes`,
`test_dialogwatch`, `test_dialogwatch_boxless_crash`, `test_ops_crash`, `test_safety_children`,
`test_safety_dismiss_override`, `test_stop_main_menu`, `test_stop_nolua`, `test_turns_end_blocked`): 205 passed.
**Not verified live**: a real WER box closed through `--fatal` (only the laptop shows WER's box; 0428's recovery job
pressed the same "Close program" on the same box through `dismiss_dialog` with the override on 2026-11-17, so the press
itself is known to work there [L]; what a live run would add is only that `children.scan` had recorded WerFault.exe with
the game as its parent before the press, which 3516's record of the box in a child [L] and psutil's parent link [I]
support).
- Also since 0531: a boxless crash's stand-in box (17.3) carries `crash: true`, `exception_code` (the exit code) and
  `exception_address` (`<module>+<RVA>` or null), and `ledger_count` copies those keys (and `exit_code_hex`) from any box
  into its signature record, so the ledger says which signatures are crashes.

**Tests [L offline, 2026-11-18, workstation; no game, Steam or instance used].** `python -I -m pytest tools/harness/tests/test_dialogwatch_werfault_record.py`: **19 passed** (03:36 EST). With `test_dialogwatch.py`, `test_dialogwatch_boxless_crash.py`, `test_dialogwatch_escalation.py`, `test_dialogwatch_ia_exit.py` and `test_safety_dismiss_helper_popup.py`: **152 passed** (03:38); `test_safety_static.py`: **55 passed** (03:39). The new file: the box closed then exit 255; the box open at the end with 255, an unknown code or 0xC0000005; a crash code after the box closed; two instances, one crash signature with count 2; as before for no record, no run folder, a stale pid, a record in another pid's folder, a record older than the game, exit 0 (box closed or open), exit 1 after the box closed, exit 255 without a WerFault box, and a fatal box with exit 255; the parser on the laptop record's format, a fault outside any module, the "no exception record" form, garbage, empty and missing files.

**Not verified.** A live run: the laptop's live check of 0519 covers the real WER box; the watcher's path here is
driven by the box as `crash.dialog_state` read it on the laptop (3516, 2026-11-14) and the record's exact format, so a
live run would add only the real timing of "Close program" against the record's write (the record is written before WER
starts, 8.4). Not read: whether WER's reflected copy of the process (8.1) can show a second box.

### 17.6 The ledger tells causes apart and names known ones (ticket 0540, 2026-11-18)

From the D185 crash-ledger audit (`crash-ledger-audit.md` sections 1 and 5, T4): 4 of the ledger's 15 signatures held
sightings of different causes (VP's "Game Crash" box: 14 sightings at 9 sites under one signature), and `bug` was set
on 1 of 15. Code: `tools/harness/dialogwatch.py` (`vp_crash_detail`, `popup_stage`, `signature`, `describe`,
`journey_exit`, `known_exit_cause`, `known_bug`, `ledger_count`, `ledger_list`, the `ledger` command); tests
`tools/harness/tests/test_dialogwatch_signature_detail.py`. Harness only.

**The rule (2026-11-18) [C, tests].**
- **VP's crash box** (and any box whose text has VP's `--Crash details--` lines): the signature is the old one plus
  ` [<exception> at <module>+<offset> in file]`, from `Exception: 0x...` and `Location (in file): <module>+0x...`,
  normalised (code as `0x%08X`, offset upper-case without leading zeros), e.g. `Game Crash: The game has crashed for an
  unknown reason. ... [0xC00000FD at CivilizationV_DX11.exe+0x525D37 in file]`. A missing line is named (`no exception
  line`, `no location line`). The offset is a **file offset**, not an RVA; the box record keeps `vp_crash`
  (`exception`, `module`, `offset`, and for the DX11 EXE `va_if_text` = offset + 0xC00 + 0x400000, valid only if the
  offset is in `.text`), and `ledger_count` copies `vp_crash` into the signature's record. The DLL's precondition box
  ("Error ... the game will now crash") keeps `<file>:<line> <expression>` and takes the same suffix only when its text
  carries the crash details (the DLL's own box of 2026-11-16 did not).
- **The patch's popup** (class `VP64HelperPopup`): the signature is the old one plus ` [<stage>: <code>]`, from the
  first `(<stage>: <code>)` of its text that is a known stage (`pack`, `server-start`, `handshake`, `server-death`,
  `exe-crash`, `dll-fatal`, `uidraw`) or sits on one of the summary lines ("The 64-bit part of the patch failed (",
  "Details: ", "The game itself crashed (", "The game stopped on an internal check", "The patch could not "). The box
  record keeps `stage`. A popup without a stage line keeps its old signature; a `#32770` box never takes a stage.
- **The suffix is kept whole**: the head is cut so the signature stays within 300 characters (`SIG_MAX`).
- **A journey's own EXIT and Yes** (`journey_exit`): a stock game with an `-ia` DLL that ends 0xC0000005 with no proxy
  record and no `lua_exit_request` line gets 0483's signature (`IA_EXIT_SIGNATURE`, ledger id `cfc8ac3165a7`, the same
  record as after `UI.ExitGame()`) when all hold: `<run>\hi\session.json` is a replay session (`"replay": {"trace"}`)
  of this game's pid; the `journeys.json` beside that trace lists it with `"ends": "exit"` (today `release-basic`;
  `release-large` ends at the menu); the replay's own `<out>\trace.jsonl` recorded, with exit 0, the journey's last
  step (`replay_of` = the trace's last step n), or the one before it (the EXIT click) with an input (`hi_action` click,
  drag, key, hold or type) logged in `harness.log` at or after that record's write (the Yes click, whose record the
  exit can cut short); and the last input came at most `JOURNEY_EXIT_WINDOW_S` (120 s) before the end. The text names
  the journey and the step reached. Otherwise the hint of 0483 stays (now "no UI.ExitGame() request and no human-input
  journey's EXIT and Yes"). Why this is needed [L, harness.log of c5h-b, 2026-11-18]: pid 53896's replay of
  release-basic clicked EXIT at 03:57:40 and Yes at 03:57:53; the game ended 0xC0000005 at 03:57:56 and was ledgered
  under the plain `d392eb4ffeca`, because `hi_action` lines carry no step meaning and no `lua_exit_request` was written.
- **The bug field** (`known_bug`, from the signature and kind only, never from an example text): `IA_EXIT_SIGNATURE`
  -> `0483`; a WerFault.exe box's own signature (`WerFault.exe: ...`, any case) -> `0531` (the crash itself is counted
  under the proxy record's signature at the end, 17.5); a patch popup by its stage suffix: `exe-crash` -> `0418`,
  `dll-fatal` -> `0422`, `uidraw` -> `0487`, every other stage -> `3521` (the D118 route; the failure itself is in the
  popup's report and is filed by the run's owner). `ledger_count` writes `bug` and `bug_why` only where `bug` is null:
  a filed bug is never replaced. The queue entry's text adds `; bug <id> (<why>)`. Agreed by the MC on the queue
  (2026-11-18, thread 0540), with no back-fill of old lumped signatures from their example text.
- **`python -I tools/harness/dialogwatch.py ledger [--dir D] [--fill]`** lists every record with its `bug` and the
  table's `known_bug`; `--fill` writes the table's bug where `bug` is null (under the record's lock).

**Old ledger files** are read and counted as before (same keys, a missing `bug` is null). Signatures whose form did
not change keep their ids: every boxless crash (`exe-crash: ...`), the DLL's precondition and assert boxes, "Render
Error", WerFault boxes, popups without a stage line. A new sighting of a VP crash box or a popup with a stage goes to a
new id; the old record keeps its count. **How the 15 signatures of 2026-11-18 07:10 map** [C, `signature()` on each
record's example text; the other sightings by the audit's table]:

| Old id | Old content (0534 row) | From now on |
|---|---|---|
| `07a62bf44a75`, `1676d60fbd1a`, `687149c46513`, `4c7da30500db` | boxless crash with a proxy record (rows 1, 10, 6) | unchanged (the record's site is already in the signature); `bug` null |
| `d392eb4ffeca` | boxless crash, no proxy record: 12 -ia exits + 1 start-up crash (rows 7, 8) | unchanged for crashes the table does not know; -ia exits after `UI.ExitGame()` or a journey's EXIT/Yes go to `cfc8ac3165a7` |
| `cfc8ac3165a7` | -ia exit after `UI.ExitGame()` (row 9) | unchanged; `bug` 0483 (already set by hand) |
| `5e071aaea5b2` | VP "Game Crash", unknown reason, 14 sightings at 9 sites (rows 2, 3, 4, 11, 12, 14) | one new id per exception and site, e.g. row 11 -> `[0xC00000FD at CivilizationV_DX11.exe+0x525D37 in file]` (`0eaa8bdaeb43`); the old record stays with count 14 |
| `d919bedd3779` | VP "Game Crash" from the DLL (row 17) | `... [0xC0000005 at CvGameCore_Expansion2.dll+0x33F779 in file]` (`d51cc2912cfd`) |
| `737de4f02d23` | DLL precondition `CvGlobals.cpp:4244` (row 15) | unchanged (no crash details in that box) |
| `ef93300efeb8` | "Render Error" (row 13) | unchanged |
| `092dca94646e` | popup, dll-fatal (row 16) | `... [dll-fatal: eImprovementNum > -1 at CvGlobals.cpp:4279]` (`85d82161d198`), `bug` 0422 |
| `3592d6cd16cc` | popup "could not run": **mixed**, one `pack: no-pack` and one `uidraw: test` sighting (row 18) | `[pack: no-pack]` (`f34f9018b8af`, 3521) and `[uidraw: test]` (0487) apart; the old record is not back-filled |
| `1546aa563014` | popup "config.ini was left as it is" (row 19: `pack: file-missing` and `render_overlay=0`) | by stage, e.g. `[pack: file-missing]` (`61349c5edfe5`, 3521) |
| `62fbaca452d6` | popup "config.ini is back", `server-death: PeerDied` (row 20) | `[server-death: PeerDied]` (`bfd127883ee5`, 3521) |
| `41ddd6a00c0f` | popup with the fog reason (row 21) | `... [server-death: PeerDied]` (`967f7ba8ee6e`, 3521) |

**Tests [L offline, 2026-11-18, workstation; no game, Steam or instance used].**
`test_dialogwatch_signature_detail.py`: **39 passed** (07:44 EST): each of the four cases with its negatives (VP boxes
at two sites and two codes, a missing line, a 400-character head; six popup stages, the old mixed signature split, no
stage, a `#32770` box; the journey's exit with the Yes step recorded or cut short, and seven ways it is not one; the
table's ids, a filed bug kept, old records counted, `ledger --fill` never from an example text). With the older
`test_dialogwatch*.py` files and the suites that reach `describe` or the dismiss path
(`tools/harness/evidence/0540/run_suites.sh`): **468 passed, 1 failed** (07:44-08:07): the failure was
`test_safety_dismiss_helper_popup.py`'s assertion of the popup's pre-0540 signature form, now the stage suffix (27
passed, 08:09). `test_safety_static.py`: **55 passed** (08:07).

**Not verified.** No live box was shown: the texts are the ledger's example texts of 2026-11-16..18 and the helper's
documented summary lines. `journey_exit` reads `tools/hi`'s session and trace files as they are written on
2026-11-18 (`session.py` `state_path`, `replay.py` `_append_step`); a change of those formats turns the check off (the
hint stays), it does not misname a crash. A crash between the EXIT click and the Yes click with an input logged after
the EXIT step's record would be named the -ia exit [I, not seen]. The test machines' ledgers (oldpc, laptop) were not
read.

### 17.7 A full dump of a game that WER holds (ticket 0543, D185, 2026-11-18)

When the watched game has crashed and Windows Error Reporting holds it (a WerFault.exe child started after the game; the
proxy's `exe-crash.txt` names the pid), `crash.collect` writes a full user dump of the game process, read only and
non-invasive (`cdb -pv -p PID`, `.dump /ma`, `qd`; never a debug register or breakpoint, D65), into the crash folder
`<run>/crash/<time>_pid<pid>/`, within a time box under WER's hold (about 45 s), and records the dump's path and size or why
it did not (timeout, no cdb, too little disk). The dialog watcher calls it at a box's sighting, and also once when it sees
the WerFault.exe child with no box (0539's case). The owner's real installs are refused (`instance.FORBIDDEN_LAUNCH_ROOTS`, the
legacy Steam route). Dumps stay in the run folder and are never committed (D83). Verified with a stand-in; a real WER hold
on the laptop is not yet verified. (Added by the MC at 0543's merge.)

**Also at a fatal box (ticket 0548, 2026-11-18):** when the watcher's box is a fatal box of COMMON 29's list (the VP DLL's "Game Crash" box, the DLL's "Error ... the game will now crash", a C runtime abort), `crash.collect` takes the same full dump once per box, before any dismiss, with the same refusals and time box; continuable asserts and unknown boxes are not dumped. A non-invasive dump has no exception record (the DLL's own minidump keeps that). Stand-in verified (157.6 MB in 5.5 s); VP's real box in the 32-bit EXE not yet. (Added by the MC at 0548's merge.)


### 17.8 A reused pid makes the dialog watcher the game's "child" and it never ends (ticket 0570, 2026-11-18)

Found by impl-0570 on the laptop (job `20261118-193521-36ab`, run r01-p2, 2026-11-18 19:44-19:50 EST, harness at
main b7e1142f5; host Python 3.12.10, workstation psutil 5.9.0). Fixed in 0570 (MC #6319 widened its touches; see the end of
this section).

**What happened [L, job log and steps.jsonl].** `cli.py launch` (pid 16188) started the game (pid 13352), then the
audio mute watcher (pythonw.exe 13272) and the dialog watcher (pythonw.exe 8528, created 0.7 s after the game), and
exited. The proxy then started `server.exe`, which got the freed pid **16188**. `status-before-stop` listed the game's
children as server.exe 16188, pythonw 13272, pythonw 8528 and conhost. At `stop` the audio watcher ended with the game;
the dialog watcher did not, `stop` waited its 60 s for children and reported pid 8528 as an orphan (rc 7), `leftovers`
rc 7 ("recorded child"), and the turn-study series ended with exit 6 (stop refused) with the instance not restored.

**Why [C, code read 2026-11-18].** Two gaps together:

1. `children.descendants` uses psutil's `Process.children(recursive=True)`. In recursive mode psutil (5.9.0's source,
   checked) compares each descendant's creation time with the **root's** (the EXE's) only, not with its own parent's.
   A process created after the EXE whose parent pid was later reused by a descendant of the EXE is adopted. The
   watchers are created after the EXE and their real parent (the launching `cli.py`) exits at once, so any descendant
   that draws that pid adopts them. `children.scan` then records them in the state file as the game's children.
2. `dialogwatch.patch_processes` returns every recorded child that is alive, and does not leave out the watcher's own
   pid. After the game ended, the watcher's tail (`_tail`, 17.4) runs "while any process the game started still runs":
   it counts itself, so it runs until `MAX_S` (14 days).

`patch_processes`' own parent-pid rule already refuses a reused parent pid (17.4); `children.descendants` does not.

**Effect.** By chance, once per launch where server.exe (or another descendant) draws the launcher's pid: 1 of the 8
laptop client runs of 0563 and 0570 on 2026-11-18. The run itself is good (10 turns, statistics written), but the
series ends, the instance keeps the arm's layout and a pythonw.exe stays alive on the testbed until a coordinator
decides (the harness never terminates a process).

**Fix (ticket 0570, 2026-11-18) [C, offline tests].** (a) `children.descendants` walks the tree one level at a time
with psutil's non-recursive `children()`, which keeps a child only if it was created after its own parent;
`descendant_pids` uses it. (b) `children.is_own_helper(proc, state)`: the calling process itself, or a pid recorded in
the state file's `audio_watch` / `dialog_watch` (`watcher_pid`) whose creation time is no later than that record's
`started_at` + 0.5 s (a later process on the same pid, such as the server's crash helper after the watcher ended, is not
excluded; a process without a creation time is never excluded by this rule). `record` never records such a process,
`recorded` and `scan` leave them out (also for a state file written before the fix), and
`dialogwatch.patch_processes` leaves them out of its roots and its result, so the watcher's tail ends when the game's
real processes have ended. Tests `tools/harness/tests/test_children_pid_reuse.py` (7, the laptop's process table as
a double; 6 of them fail on the old code). Not changed: `crash._child_processes` and `audio.py` still use psutil's
recursive `children()` for their own listings (the status line's children and the mute watch's targets); they record
nothing, so they cannot start this loop, but the status line can still list an adopted process.

**Recovery of the laptop (2026-11-18 20:06, job `20261118-200539-7d4f`, MC #6319).** `perf/evidence/0570/recover_tb1.py`:
pid 8528 ended only after its creation time (1795049044.7105432) and image matched (its command line was
`dialogwatch.py watch --pid 13352`), then the run's own `ClientRun.restore` on r01-p2's record from the old job's
`out/` and `work/` (autosaves aside, stats.db collected, `set-proxy --original`, the RC extras by hash, the front-end
and std-backup restores, the save copy by hash, `provision.py verify` rc 0), `leftovers` empty. A pattern for a series
that ended with exit 6 when the game itself is already stopped.

## Game recorders and the hidden test instances (2026-11-25)

`[L by the owner, 2026-11-25 20:5x, ticket 0780]` A game-recording app that hooks game windows (Medal.tv) tried to
record a focus-guarded (hidden, no-focus) Civ V test instance on the workstation, and the whole desktop's mouse cursor
became very choppy while the game was in the background. Closing the recorder fixed it; it was not a patch bug. When
the owner reports desktop-wide input stutter with test instances running, ask about recording or overlay apps first.
