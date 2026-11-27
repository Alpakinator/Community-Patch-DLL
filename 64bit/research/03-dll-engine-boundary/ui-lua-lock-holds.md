# Which interface Lua holds the EXE's Lua lock during AI turns (ticket 0518)

impl-0518 (account B), 2026-11-17. Brief: `tracker/issues/0518-ui-lua-holds-the-lua-lock.md`. Starting point: ticket 4811
(`research/08-performance/speedup-p2-gameevents.md` section 11, on branch `epic-0504-speedups`): in a client M turn (S4,
`TS0438-M-T150`, turns 151-160, D165 options 2+3+4 and P2) the DLL's GameEvents raises spend 36.5 us of their 43.6 us
waiting for the EXE's Lua critical section, because the EXE's main thread holds that section 0.86 s a turn (10.7 holds
over 10 ms a turn, the longest 581 ms) while the AI plays. 4811 did not say which Lua that is. This file does.

Evidence: `proxy/evidence/0518/` (drivers, the reader `analyze.py`, the reports). **Workstation figures, an indication
only (D106):** the attribution (which Lua) is the result; the absolute times are indications.

## 1. Method `[the measuring build is a scratch branch, never merged]`

- **Build.** Scratch branch `scratch-0518-luaattr` (81e98de36, then 1b1a57281: chunk names cut to the part from `MODS\` or
  `Assets\` on, since the EXE names a chunk by its full path and `short_src` keeps only its first 60 characters; function
  names in the samples; the runs below used 1b1a57281) on 4811's `scratch-4811-with0437` (c977a6430: P2's one
  crossing per raise, 0437's D165 options, 4811's import-slot timers on the EXE's `EnterCriticalSection`,
  `LeaveCriticalSection` and `TryEnterCriticalSection`). Added in `proxy/src/proxy_lua.cpp` (`m4811` namespace, the
  "ticket 0518" block): the proxy also writes the EXE's import slots of `lua51_Win32.dll` `lua_pcall`, `lua_call`,
  `lua_cpcall`, `lua_resume`, `lua_load` and `luaL_loadbuffer` (data writes to the import table, no code byte, D4), with
  wrappers that call the function the slot held before. Installed at the first GameEvents raise P2 serves (as 4811's
  timers), so the front end and the load run unhooked.
  - An **entry** is the outermost hooked call whose callee is a Lua function (`lua_getinfo(">S")` on a copy of the
    callee: source and `linedefined`); calls the EXE makes inside it (a C function the Lua called, calling back into
    Lua) belong to it. A hooked call into a C function (the `lua_cpcall` of 4811's 11.1) counts its own time less the
    entries inside it. Identity = (lua_State, source, linedefined, the EXE call site = the wrapper's return address,
    the API used).
  - Per hold of the Lua section (any thread, outermost enter to leave) the entries made while held are summed per
    identity; a hold of 1 ms or more is a row (thread, QPC start, length, the ten largest identities). A wait of another
    thread for the section over 100 us is a row with its QPC span, so a hold's cost to the AI's thread is the overlap.
  - Inside an entry made while the section is held, a Lua count hook (`lua_sethook`, `LUA_MASKCOUNT`, every 1000 VM
    instructions; only when the state had no hook) takes a stack sample (six levels, `short_src`, current line and
    `linedefined`) each 1 ms and weights it by the time since the last sample. Removed when the entry returns (also after
    a Lua error: the protected wrappers close the frames an error skipped).
  - Rows go to `C:/c5-64bit-work/0518/live` and are flushed at 4811's turn marker (the DLL's `GameCoreTestVictory`
    raise, once a turn). The reader is `proxy/evidence/0518/analyze.py`.
- **Build commands.** `proxy/evidence/0518/prep_build.sh` (fork source by `git archive 4f4daac70`, a copy of 4804's
  release deps, `server/build_server.py --config release --offline`; 0 warnings, proxy `4232eb3a...`, server
  `f08fca95...`) and `make_layout.sh` (the pin layout `x64-4f4daac7-pf8e9f42`, DLL `2D737BD5...`, with that server and
  proxy).
- **Runs.** Windowed S4 on Goldberg instance `c5h-a` (pool claim on 0518), `TS0438-M-T150`, autoplay 10 turns through
  `perf/profilekit/acceptance.py` (`proxy/evidence/0518/arm.sh`), `VP64_TURN_BUDGET=1`; `VP64Options.ini`
  `[Performance]` backed up and restored by SHA-256 (run on 1b1a57281 unless named):
  - r1 `d165`: `GameEventsOneCrossing = 1`, `AiUpdateTickMs = 0`, `AiSkipVisualDrain = 1`, `AiDrainUnordered = 1` (4811's arms);
  - r2 `stock`: `GameEventsOneCrossing = 1` only (stock pacing; P2 stays on because the hooks install at its first raise);
  - r-d165-ovr: r1's settings plus the observer's interface held on one player (2.4);
  - a trial r0 (d165, 22:58-23:02, the build 81e98de36) gave the same split with cut file names; not used.
- **Build hashes.** Proxy `f8788323...`, server `ff1c63fe...` (1b1a57281); `layout/SHA256-0518.txt` on the scratch disk.
- **Pictures.** `acceptance.py` takes one picture a turn into the Visual Checks feed; for this ticket they are DIAGNOSTIC
  (measuring build, D112) and were re-described as such.
- **Read-only.** Nothing from the EXE is committed (D83); VP's, EUI's and the base game's Lua are read only to name the
  function at a line; no Lua text is copied here.

## 2. Results `[L, 2026-11-17 23:04-23:13 EST, workstation (indication), c5h-a, GPU driver 610.88]`

Window: the turn intervals between the turn markers 1 and 10 (9 full turns, as 4811 counted); per turn = / 9. Every run rc 0,
no dialog box, `VP64Options.ini` restored by hash. `quietlock.py check` before each arm: lock free, 3-4 heavy jobs of other
owners (an ASan suite, a game, a build, a server): indication only. Reports: `proxy/evidence/0518/r-d165/report.txt`,
`r-stock/report.txt` (the reader's output; its `--lua-root` column is left out of the committed copies, D83).

### 2.1 The EXE's two threads `[L]`

| Run | turn wall (151-159) | main thread: Lua section holds | holds over 10 ms | in them | longest | AI thread waits for the section | of it inside the main thread's holds over 10 ms |
|---|---|---|---|---|---|---|---|
| r-d165 (D165 2+3+4, P2) | 3.0-4.9 s | 12,282 a turn, **0.958 s** | 12.9 a turn | 0.657 s | 179.6 ms | 15,954 a turn, **0.590 s** | 0.542 s |
| r-stock (P2 only) | 10.6-13.7 s | 15,036 a turn, **3.694 s** | 64.6 a turn | 2.427 s | 214.6 ms | 15,956 a turn, **0.651 s** | 0.596 s |

The thread that serves the GameEvents raises during AI turns (the "AI thread": the DLL's raises reach the EXE on it) holds
the section only briefly (0.03-0.04 s a turn). The holder is the EXE's main thread (the proxy's pacing line names the same
thread as "the main thread"). r-d165 matches 4811's m1 (0.86 s of holds, 10.7 over 10 ms, 0.59 s of waits a turn).
93-100 % (r-d165) and 97-98 % (r-stock) of each turn's time inside the main thread's holds over 1 ms is inside a hooked
Lua entry; the rest (0.012-0.017 s a turn in the holds over 10 ms) is the EXE's own work under the lock.

### 2.2 Who blocks the AI: one handler, CP's `TechPanel.lua`, on `AIProcessingStartedForPlayer` `[L; C for the code path]`

In both runs one entry carries nearly all of the AI thread's wait:

| Run | entry | calls a turn | holds over 10 ms (9 turns) | self time a turn | AI thread's wait inside its holds, a turn | longest |
|---|---|---|---|---|---|---|
| r-d165 | `(1) Community Patch\Core Files\Overrides\TechPanel.lua:169` | 25 | 72 (8 a turn) | **0.551 s** | **0.535 s** (of 0.590) | 179.6 ms |
| r-stock | the same | 25 | 72 | **0.597 s** | **0.589 s** (of 0.651) | 199.7 ms |

- **What it is.** The function defined at line 169 of Community Patch's `TechPanel.lua` override is its listener of the
  EXE's interface event `Events.AIProcessingStartedForPlayer` (the EXE raises it once for each player whose AI turn starts:
  25 a turn on this map). When the active player is an observer (autoplay makes it one), it points the panel at the AI
  player whose turn starts (a major civ; else -1, or the observer override player when one is set) and, when that player
  differs from the last one, calls `OnTechPanelUpdated` (line 47), which rebuilds the tech panel's small buttons:
  `AddSmallButtonsToTechButton` (`TechButtonInclude.lua`) -> `GenerateNextButtonFromInfo` -> `SetupFunc` -> the building
  and unit tooltip text (`InfoTooltipInclude.lua` `GetHelpTextForBuilding`, `ExtractSimpleYieldTable`,
  `ExtractYieldTable`, `AddTooltip*`; `VPUI_core.lua` iterators). Stack samples (r-d165, s a turn inside this entry):
  `GetHelpTextForBuilding` 0.197, `ExtractSimpleYieldTable` 0.138, `ExtractYieldTable` 0.040, `VPUI_core` generator 0.032,
  `AddTooltipPositive` 0.029, `AddTooltipGeneric` 0.023. Each rebuild is 77-200 ms; 8 a turn are rebuilds (one a major
  civ), the other 17 calls return at once.
- **How it reaches the main thread while the AI plays.** EXE call path of the entry: `lua_call` from 0x558AF0 (inside
  0x558A90, the dispatcher of an interface event's listeners), under a `lua_cpcall` of 0x494CB0 made by the shared
  protected-call helper (call site 0x904E52, the helper 4811 named 0x904E30) `[C: call sites read live; VAs at 0x400000]`.
  It runs on the main thread inside the Lua section while the AI thread goes on with that player's turn; the AI thread's
  next GameEvents raise (a `PlayerCanConstruct`, `CanDeclareWar`, `*StrategyCanActivate` at the player's start, 4811's
  11.3) then waits for the whole rebuild. Other threads' waits over 100 us that overlap one of these holds: 1 per hold, as
  long as the hold (the AI thread is the only waiter).
- **Only in observer mode** `[C: CP's and EUI's Lua read; L since 2026-11-17 with a human player, section 5.1 (ticket 0522); was C only]`. With a human active player the
  handler sets the panel's player to `Game.GetActivePlayer()`, which does not change during AI turns, so it never rebuilds.
  EUI's `TopPanel.lua` `OnAIPlayerChanged` (line 3180, the same event; 25 calls a turn, 0.057 s a turn in r-d165, five of
  its holds over 10 ms in 9 turns, 0.006-0.007 s of AI wait a turn) has the same observer-only branch. **So the coupling
  4811 measured (the AI thread serialised behind UI Lua, 0.59 s of 0.70 s a raise-turn) is mostly a property of the
  autoplay observer, not of a game a person plays.** Every client turn timing taken with `Game.SetAIAutoPlay` (0437's S4
  series, 0504's P2 and the P15 stand-in) contains it.

### 2.3 The other main-thread passes: long, but not in the AI's way `[L]`

Functions whose entries lead holds over 10 ms (the hold's largest entry) but whose holds no other thread waited in
(`proxy/evidence/0518/r-*/byfunc.txt`: every lua_State and call site of a function together; r-stock / r-d165):

| Entry (file:line, function) | how the EXE calls it | calls a turn | s a turn | holds over 10 ms it led (9 turns) | longest | AI wait inside |
|---|---|---|---|---|---|---|
| EUI `CityBannerManager.lua:969` `RefreshCityBannersNow` | `ContextPtr:SetUpdate` (the context's update, `lua_pcall` from 0x9045D5) | 88 / 1 | 1.080 / 0.054 | 179 / 9 | 76.7 / 63.0 ms | 0 |
| CP `TechPanel.lua:47` `OnTechPanelUpdated` | `Events.SerialEventGameDataDirty` (cpcall of 0x5D6D10) | 21 / 0 | 0.493 / 0 | 58 / 0 | 214.6 ms | 0 |
| EUI `NotificationPanel.lua:1393` `UpdateCivListNow` (in several states) | `ContextPtr:SetUpdate` | 140 / 99 | 0.618 / 0.073 | 194 / 16 | 22.0 / 12.8 ms | 0 |
| VP `CultureOverview.lua:1992` `RefreshSwapGreatWorks` | `Events.SerialEventGreatWorksScreenDirty` (cpcall of 0x53E450) | 7 / 1 | 0.124 / 0.014 | 60 / 9 | 83.8 / 16.1 ms | 0 |
| EUI `TopPanel.lua:3142` (the top panel's deferred refresh) | `ContextPtr:SetUpdate` | 476 / 115 | 0.127 / 0.002 | 4 / 0 | 11.5 ms | 0 |
| EUI `PlotHelpManager.lua:1212` (`PlotToolTips`) | `ContextPtr:SetUpdate` | 476 / 115 | 0.068 / 0.003 | 0 | 4.3 ms | 0 |

These run between the AI's update ticks (the frame), not while the AI thread raises; their cost is frame time on the main
thread. Under stock pacing the AI advances one tick per frame, so they lengthen the turn through the frame count, not
through the lock; D165's options cut them from 3.69 to 0.96 s of holds a turn (the per-frame `SetUpdate` passes and the
dirty-event refreshes run far less often), which is why the turn wall falls from about 12.5 to 3.8 s while the AI
thread's lock wait stays at 0.59-0.65 s. Unit and city interface listeners (`UnitFlagManager.lua` 881, 896, 922, 945,
1022; `UnitMemberOverlay.lua` 487, 562; `CityBannerManager.lua` 1594, 1683; the tutorial's `TutorialChecks.lua` 2020) run
600-1,400 times a turn at under 1 ms each (0.005-0.019 s a turn each) and never form a hold over 10 ms.

Sampling gap: the per-frame `RefreshCityBannersNow` entry took no stack samples (its Lua runs where the count hook was not
set, presumably another coroutine of the state) `[I]`; it is attributed at the entry level only.

### 2.4 Check: the observer's interface held on one player `[L, 2026-11-17 23:14-23:19 EST, indication]`

Run r-d165-ovr: r-d165's settings, and `proxy/evidence/0518/override.sh` set `Game.SetObserverUIOverridePlayer(0)` through
the Lua channel at 23:17:34, as soon as autoplay was armed (answer: override 0, active player 8 = the observer, autoplay
10 turns left), so the observer's panels stay on player 0.

| Per turn (152-159 or 9 turn intervals) | r-d165 | r-d165-ovr |
|---|---|---|
| `TechPanel.lua:169` self time (25 calls) | 0.560 s, 72 holds over 10 ms | **0.016 s**, none over 10 ms |
| main thread's Lua holds | 0.958 s, 12.9 over 10 ms | 0.665 s, 5.3 over 10 ms |
| AI thread's wait for the section | **0.590 s** | **0.016 s** |
| server budget `into_exe.script` (the raises' time, 0437's 4652) | 0.701 s | **0.149 s** |
| server budget `game_logic` / `exe_alone` | 1.477 / 1.008 s | 1.869 / 1.342 s |
| turn wall | 3.763 s | 4.093 s |

The mechanism is confirmed: with the observer's panel player fixed, the per-player rebuild stops and the AI thread no
longer waits (-0.57 s a turn of wait, -0.55 s of the server's script time). The remaining holds over 10 ms are the
once-a-turn `OnTechPanelUpdated` on `SerialEventGameDataDirty` (one rebuild a turn, up to 207 ms) and
`RefreshCityBannersNow` (one a turn), with no AI wait in them. **The turn wall did not fall in this one pair:**
`game_logic` (the DLL's own work in the server) rose 0.39 s and `exe_alone` 0.33 s, while other owners' jobs ran (0517's
whole games and 0520's suite in r-d165-ovr only, per its `quietlock.py check`). A wall-time claim needs an interleaved
series on oldpc (D106); none is made here. Budget means: `proxy/evidence/0518/budgetcmp.txt`.

## 3. Options: moving or skipping those passes during AI turns (no implementation here)

Each option keeps the game's results (`stats.db`) unless it says otherwise; none was implemented or identity-tested in this
ticket.

| # | Option | What it moves or skips | Saves (AI thread's wait, client M) | What the player sees | Risk |
|---|---|---|---|---|---|
| O1 | **Measure like a player: autoplay runs hold the observer's interface on one player** (`Game.SetObserverUIOverridePlayer(<a living major>)` through the Lua channel right after `SetAIAutoPlay`, as `proxy/evidence/0518/override.sh`; or the harness's autoplay helper does it) | CP `TechPanel.lua:169` and EUI `TopPanel.lua:3180` stop rebuilding per AI player; the AI-phase coupling a human game does not have leaves the timing series | 0.57 s a turn of AI-thread wait, 0.55 s of the server's script time (2.4, one run); **corrected 2026-11-18 (ticket 0522, 5.2): the turn wall rises 1.17 s (+12 %, oldpc, pin, stock pacing) and the saves differ from turn 151 (team 0's view): not a free switch, see 5.3** | nothing in a played game (test runs only); the test client's panels show one player | low for timing; the field is in the save and read by a few DLL paths (`CvGame.cpp` 3880-3932, 5525; `CvPlayer.cpp` 13250, 33967-33987; `CvPlot.cpp` 11711), so a series that also checks game identity confirms `stats.db` once (D164) before relying on it |
| O2 | **Re-baseline what was measured on autoplay**: 0437's client S4 turn shares, 0504's P2 and P15 numbers, 4811's 11.4-11.5 | none (accounting) | - | - | P15's value for a player is closer to its wait-free part (4811 11.5: 0.11 s a client M turn) than to the stand-in's 0.34 s, because most of the wait it removed was this observer rebuild `[I]`; the Architect's P15 review (`research/07-architecture-decisions/architect-2026-11-17-p15-review.md`) already counts only that part |
| O3 | **Upstream (CP), the observer rebuild deferred**: the `AIProcessingStartedForPlayer` listener records the player and rebuilds once from `ContextPtr:SetUpdate` (or only while the tech panel is shown, or once at the end of AI processing) | the 8 rebuilds a turn out of the AI's way | as O1 for observer games (autoplay, spectators) | an observer's tech panel follows the AI player a frame later, or only when it is shown | low; a VP/CP Lua change (we keep their Lua read-only): a suggestion for upstream through the MC, without internal ids (D95) |
| O4 | **Proxy: hold the EXE's interface events during the AI phase** (deliver `AIProcessingStartedForPlayer` and the dirty events once after the AI phase) | all AI-phase UI listeners | the 0.55 s and the residual listener waits | observer panels freeze during the AI phase; a human sees the same final state | high: the events are queued and dispatched inside the EXE on its main thread (call path 2.2), the proxy does not hold them; an EXE-side interception (import slots, vtables) would have to be designed and checked against D4 `[I]`. Not recommended while O1-O3 exist |
| O5 | **Stock-pacing frame passes** (2.3: `RefreshCityBannersNow`, `UpdateCivListNow`, `OnTechPanelUpdated` on `SerialEventGameDataDirty`, `RefreshSwapGreatWorks`) | already cut by D165's options 2+3+4 (3.69 -> 0.96 s of holds a turn); further: skip the dirty refreshes while the AI turn byte is set and raise one dirty after it | frame time on the main thread, not the AI's lock wait (0 AI wait in their holds) | panels refreshed once after the AI phase | medium: what the player sees between AI players changes; D165's own options are the place for it |

The ticket's question "what could move those passes out of the AI phase" therefore has a short answer for the measured
scenario: the passes that block the AI are the observer's per-player panel rebuilds, which a played game does not run;
the passes a played game does run (2.3) do not block the AI thread on this map.

## 4. Not verified

- ~~A human active player was not run.~~ Done by ticket 0522 on 2026-11-17 (section 5.1): the listener runs and returns
  at once, the AI thread waits 0.020 s a turn. (Replaced text: "A human active player was not run ... needs one
  human-turn run with this build".)
- One run per arm on the shared workstation (indication, D106); no oldpc series; the H scenario not run.
- The measuring build's own cost: two QPC reads per hooked section enter (4811), one `lua_getinfo` and a map lookup per
  outermost hooked call, the count hook in held entries.
- `RefreshCityBannersNow` (per-frame) was not stack-sampled (2.3); `lua_resume` and `lua_load` are not imported by the
  EXE, so coroutine bodies count to the entry that resumed them from Lua.
- The event that `0x558A90` dispatches is identified from the listener's own registration in the Lua file
  (`Events.AIProcessingStartedForPlayer.Add`), not from the EXE.

## 5. With a human active player, and the turn wall with the observer's interface held (ticket 0522)

impl-0522 (account B), 2026-11-17. Brief: `tracker/issues/0522-ai-lua-wait-with-human-player.md`. Evidence:
`proxy/evidence/0522/` (drivers `play.py`, `arm.sh`, the testbed job `tb_ovr.py`, readers `ids_sum.py`,
`analyze_run.sh`; the human arm's reports in `h-d165/`).

### 5.1 A human active player: the listener runs and returns at once; the AI thread no longer waits `[L, 2026-11-17 23:44-23:48 EST, workstation (indication, D106), c5h-a]`

- **Run.** 0518's measuring layout reused by hash (proxy `f8788323...`, server `ff1c63fe...`, DLL `2d737bd5...` =
  scratch branch `scratch-0518-luaattr` 1b1a57281 on the batch-3 pin layout `x64-4f4daac7-pf8e9f42`), windowed S4 on
  Goldberg instance `c5h-a`, save `TS0438-M-T150` (SHA-256 `98fc454a...`), turns 151-160, `VP64_TURN_BUDGET=1`,
  `VP64Options.ini` `[Performance]` as 0518's r-d165 (`GameEventsOneCrossing = 1`, `AiUpdateTickMs = 0`,
  `AiSkipVisualDrain = 1`, `AiDrainUnordered = 1`; backed up and restored by SHA-256). **No autoplay**: the save's
  player 0 stays the active player (`IsHuman` true, `IsObserver` false, observer override -1 at the load and at the
  end, `proxy/evidence/0522/h-d165/turns.tsv`); each turn ended by the harness (`tools/harness/turns.py` `end_turn`:
  units set to no moves, an empty city queue to wealth, `CONTROL_FORCEENDTURN`) and the next human turn awaited
  (`wait_turn`). `proxy/evidence/0522/play.py --mode human`, `arm.sh ... d165 human`. rc 0, no dialog box, 10 turns.
  Window: 0518's (turn markers 1-9, 9 turn intervals; each interval now holds the human's turn, about 1.5 s of the
  harness's end-of-turn calls, as well as the AI phase).
- **Results** (`h-d165/report.txt`, `ids.txt`, `byfunc.txt`, `budgetcmp.txt`), against 0518's observer runs (2.1, 2.4):

| Per turn | observer autoplay (r-d165) | observer held on player 0 (r-d165-ovr) | **human player 0 (h-d165)** |
|---|---|---|---|
| CP `TechPanel.lua:169` (the `AIProcessingStartedForPlayer` listener): calls, self time, longest | 25, 0.560 s, 179.6 ms | 25, 0.016 s, 2.8 ms | **24.9, 0.0007 s, 0.3 ms** |
| EUI `TopPanel.lua:3180` (`OnAIPlayerChanged`, the same event) | 25, 0.057 s, 14.8 ms | 25, 0.018 s, 3.2 ms | **24.9, 0.0004 s, 0.3 ms** |
| CP `TechPanel.lua:47` `OnTechPanelUpdated` (on `SerialEventGameDataDirty`): calls, s | 1.0, 0.000 | 1.0, 0.132 | **4.1, 0.288** (37 holds over 10 ms in 9 turns, longest 128.9 ms, **0 AI wait in them**) |
| main thread's Lua holds | 0.958 s, 12.9 over 10 ms | 0.665 s, 5.3 over 10 ms | 1.101 s, 13.4 over 10 ms (0.439 s in them) |
| **AI thread's wait for the Lua section** (4811's counters) | **0.590 s** | 0.016 s | **0.020 s** |
| other threads' wait inside the main thread's holds over 10 ms | 0.542 s | - | 0.008 s |
| server budget `into_exe.script` (turns 152-159) | 0.701 s | 0.148 s | **0.127 s** |

  So the listener does run with a human player (the EXE raises the event for every AI player), but takes the
  `Game.GetActivePlayer()` branch and never rebuilds: 0.3 ms at most. The AI thread's wait falls from 0.590 s to
  0.020 s a turn and the server's script time to 0.127 s, a little below the held-observer run. The 2.2 conclusion
  ("mostly a property of the autoplay observer") is now `[L]`. What a human game keeps on the main thread during the
  turn: `OnTechPanelUpdated` four times a turn (the dirty events of the human's own turn, here the harness's
  end-of-turn calls; 0.29 s a turn of frame time), EUI's `UnitPanel.lua:1988`, `CityBannerManager.lua:969`,
  `NotificationPanel.lua:1393` and `TopPanel.lua:3142` context updates; none of them holds the section while the AI
  thread raises (0.008 s of AI wait a turn in all holds over 10 ms together).
- The turn wall of this run (8.1 s, turns 152-159) is not comparable with the autoplay runs: it includes the human's
  turn and the harness's polling.

### 5.2 Turn wall: observer autoplay (A) against autoplay with the observer's interface held (O): O is 1.17 s (12 %) slower `[L, oldpc, jobs 20261117-235344-1584 (not counted) and 20261118-000935-8c40]`

- **Why oldpc and the pin layout.** The MC's answer to question 4872 (2026-11-17): the series goes to oldpc as one quiet
  job (D106); no workstation timing series. The job runs the **current pin layout** `x64-4f4daac7-pf8e9f42` (COMMON 18:
  proxy `4815ED81...`, server `ED829BDA...`, DLL `2D737BD5...`), not the measuring build: the measuring build writes its
  rows into a fixed workstation folder and adds its own cost inside the very Lua it attributes (the rebuild that arm O
  removes), which would bias A against O. The pin has no `[Performance]` options of epics 0437/0504 (no P2, no D165
  options: stock pacing; `VP64Options.ini` written with an empty `[Performance]` section), and no turn-budget timers.
- **Job.** `proxy/evidence/0522/tb_ovr.py` on oldpc's `tb1` (its own EXE), headed and quiet, order
  `W,A,O,O,A,A,O,O,A` (W = an A run not counted), 4 counted runs per arm, 60 s gap. Each run: save copy by hash, front
  end with cp,vp,eui, `cli.py launch --mode offscreen --park none --timing` (window on screen, the game's own vsync as
  found), in game, turn 150 and a server.exe child checked; then **the turn clock** (epic 0437's: a handler in the
  InGame state records `os.clock()` at player 0's first `GameEvents.PlayerDoTurn` of each turn; tied to wall time by the
  shortest of six channel round trips), `Game.SetAIAutoPlay(12, 0)`, and for O `Game.SetObserverUIOverridePlayer` on the
  first living major that is not the observer (read back). No channel call during the window (progress from the
  AutoSave files); then autoplay's hand-back, the clock read, the safe stop; the run's autosaves and `stats.db` moved into
  the job's output. Prep and restore by SHA-256 (harness standard files, `VP64Options.ini`, the instance's `stats.db`
  moved aside and put back by hash), `provision.py verify`. Window: turns 151-160 (`seconds(t)` = DoTurn(t+1) -
  DoTurn(t)).
- **First job, not counted** (`20261117-235344-1584`, 23:54-00:08 EST): its warm-up run r01-W was complete (10 window
  turns, 9.678 s a turn, autosaves `Post_0151`..`Post_0161`, hand-back at 162, stop rc 0), but the driver then waited for
  an autosave of turn 162, which autoplay's hand-back never writes, and ended the job (rc 4, instance restored, verify
  rc 0). Fixed (the window ends at the `Post_0161` file) and resubmitted as `20261118-000935-8c40` (00:09, queued behind
  4807's two quiet jobs). oldpc's GPU in both: NVIDIA GeForce RTX 2080, driver 32.0.16.1088 (2026-07-21) (D175).
- **Results** `[L, oldpc, 2026-11-18 00:28-01:18 EST, job 20261118-000935-8c40, rc 0, quiet (alone; foreign_cores_mean
  0.448, mostly tvnserver.exe of the logged-on session: "undisturbed" false), CPU mean 25.6 %, max 60.6 %; NVIDIA
  GeForce RTX 2080 driver 32.0.16.1088 (D175); instance tb1, window 1920x1080 on screen, WaitForVSync = 1 as found;
  pin layout x64-4f4daac7-pf8e9f42 (proxy 4815ed81, server ed829bda, DLL 2d737bd5), stock pacing, no [Performance]
  option; save TS0438-M-T150 (98fc454a); no stats.db written in any run (tb1's statistics logging is off); grade B:
  4 interleaved runs per arm, W,A,O,O,A,A,O,O,A]`. `proxy/evidence/0522/oldpc-8c40/` (`stats.txt` by `ovr_stats.py`,
  `tb_ovr.json`, `savecmp.txt`, `savediff-0151.txt`, `savediff-0161.txt`, `quiet-summary.json`, `gpu.txt`). Every run:
  10 window turns, override answer `OVR_SET 0` in each O run (active player 8, the observer), hand-back at 162, stop rc 0;
  restore ok, `provision.py verify` rc 0.

| Seconds per AI turn, 151-160 (player 0's PlayerDoTurn clock) | n | mean | sd | median | min-max | 95 % interval |
|---|---|---|---|---|---|---|
| A: observer autoplay | 4 | **9.722** | 0.024 | 9.710 | 9.709-9.757 | 9.684-9.759 |
| O: autoplay, observer's interface held on player 0 | 4 | **10.894** | 0.064 | 10.904 | 10.809-10.958 | 10.793-10.995 |

  **O - A = +1.172 s a turn (+12.1 %)**, Welch 95 % interval +1.076..+1.269 (df 3.8, p < 0.0001), Mann-Whitney exact
  p 0.029 (the smallest possible with 4 against 4); the series could detect 0.128 s (1.3 %). O is slower in every turn of
  the window (+0.38 to +2.34 s; +0.83 s already in 151, +2.29/+2.34 s in 157-158). The warm-up W (9.708 s) and the first
  job's W (9.678 s) agree with A.
- **The two arms are not the same game byte for byte** (D164; `tools/d10/savecmp.py` on the `AutoSave_Post` files):
  - A against A (r02/r05, r05/r06) and O against O (r03/r04): O runs **IDENTICAL** at every turn compared (150-161); A
    runs differ only in one-byte values that are 1-2 apart at the same 113-151 offsets (offset 84 of the body and
    about 150 places at 7.2-22 MB), from `Post_0150` (the load turn) on: a stamp or counter that depends on timing
    `[I: by its pattern; the field is not named]`, the same game otherwise.
  - A against O: at `Post_0150` only that stamp; at `Post_0151` the observer override field itself (body offset 194,
    -1 against 0) and about 200 runs of 1-4 bytes at 2.8-4.0 MB (`ff`->`fe`, `ffff00`->`000001`, `ff`->`00`/`01`),
    by `Post_0161` 400 runs up to 328 bytes (blocks of `00`/`ff`). The pattern is per-plot visibility or revealed data
    for team 0 `[I: the DLL reads the override in CvPlot.cpp 11711 and CvGame/CvPlayer, section 3 O1]`. So holding the
    observer's interface on a player makes the DLL keep that player's team view as for an active player; whether the AI's
    own decisions then diverge is not shown (no stats.db; the save differences seen are of that visibility kind).
- **Why the turns got slower** `[I: not profiled; the pin has no budget timers]`: what O removes is the AI thread's wait
  behind the per-player tech panel rebuild (5.1 and 2.4: about 0.57 s a turn with D165's options; at stock pacing on
  this build it is the same listener). What O adds is the interface and DLL work of a player being shown: team 0's
  visibility and fog kept up to date, its unit flags, city banners and plot updates on the EXE's main thread, frame
  by frame. At stock pacing the AI advances one update per frame, so a longer frame lengthens the turn. 0518's single
  workstation pair at D165's options pointed the same way (2.4: `game_logic` +0.39 s, `exe_alone` +0.33 s, wall +0.33 s).
  The first window turn (+0.83 s) comes before most of the save differences, so the cost is the active-team work itself,
  not a different game.

### 5.3 Recommendation for the turn study (epic 0437) and P15 (epic 0504) `[I, from 5.1 and 5.2; 2026-11-18]`

- **Neither autoplay arm is a player's turn.** A player's AI phase has no observer rebuild (5.1: the AI thread waits
  0.020 s a turn) **and** carries the active player's own interface and visibility work. Observer autoplay (A) has the
  rebuild (0.59 s of AI-thread wait a turn at D165's options, 0.65 s at stock pacing, client M) and lacks the player's
  view; the held observer (O) has neither the rebuild nor the observer's emptiness: it shows player 0 as a player sees
  it. O is the closer model of what a player waits for, and on oldpc's M at stock pacing it is 12 % slower than A, not
  faster: **A understates a player's AI phase, it does not overstate it.**
- **0437:** keep A for the series already run and for every comparison with them (within-arm byte identity holds up to a
  timing stamp; O is a different save from turn 151). Where the study reports the EXE's Lua share or the AI thread's
  lock wait "for a player", say that A's share contains 0.55-0.6 s a turn of observer-only rebuild (2.2, 5.1) and lacks
  the active player's view; if a player-like number is wanted, add O as its own labelled arm (identity checked within O,
  never A against O) rather than switching the existing setups.
- **P15 (0504):** the AI-thread wait it removes is almost all the observer rebuild; under O or with a human player there
  is about 0.02 s a turn left to remove. Count for a player only P15's wait-free part (4811 11.5, as the Architect's
  P15 review already does), and if P15 is measured again, measure it under O, not A.
- **O1 (section 3) is corrected by this:** holding the observer's interface is not a free "measure like a player"
  switch: it changes save bytes (team 0's view) and costs turn time. (Replaced text of O1's "Saves" column, kept: "0.57 s
  a turn of AI-thread wait, 0.55 s of the server's script time (2.4, one run); wall time not shown".)
- **What would settle it for a player:** the human-seat driver of 5.1 (`proxy/evidence/0522/play.py --mode human`) on
  oldpc with a turn clock that leaves out the harness's end-of-turn calls, against O, on the same build; not run here.

### 5.4 Not verified (0522)

- The cause of O's +1.17 s is inferred, not profiled (the pin layout has no budget timers; the measuring build was kept
  off oldpc on purpose, 5.2).
- 5.2 is stock pacing on the pin; with D165's options (0437's later setups) the balance of the removed wait and the added
  work may differ (0518's one workstation pair suggests the same sign).
- Grade B (4 runs per arm, not 6); oldpc's VNC server used 0.45 cores on average during the job.
- No stats.db, so game identity is by save bytes only; the A-against-A one-byte differences are not named.
- Part 5.1 is one workstation run on the measuring build (an indication for times; the attribution is the result).


## 6. GameInfo filters: SQL prepares per AI turn and per city-screen open (ticket 5033, 2026-11-24)

impl-5033 (account B), 2026-11-24 22:00 onwards, branch `task-5033-gameinfo-prepares` (evidence, reader, this section) and
the scratch branch `scratch-5033-dbprobe` (the measuring proxy, never merged). The question (the Architect's note
`../07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md` row EX7, draft T3): how many SQL prepares do
the interface's `GameInfo` filters make per M AI turn with a human seat and per city-screen open, and what do they cost;
a go or no-go against 0.3 % of an H or M turn for statement reuse by condition shape and an empty answer from a per-table
index. Measurement only.

### 6.1 Where GameInfo lives: in the database module, not in the EXE `[C, 2026-11-24: Ghidra read of build 4390913 (references and a decompile, read only, runbook 3) and dumpbin /disasm of CvGameDatabaseWin32Final Release.dll (the 4390913 install's copy), behaviour only, nothing committed (D83)]`

- The EXE's registration function (`addresses.md` 0x711C80) makes `GameInfo` by one call through its import slot of the
  database module's export `Database::Scripting::Lua::PushDatabase(lua_State*, Connection&)`, with the EXE's own
  connection. The accessors are the **database module's own C functions**, not the EXE's; the filters' prepares and
  steps run inside `CvGameDatabaseWin32Final Release.dll` and never pass an import slot of the EXE. Only `DB.Query`
  (the EXE's C function) calls the module's export `PushDatabaseQuery` through the EXE's import slot.
- The module registers its C functions with `lua_pushcclosure` through **its own import slot of
  `lua51_Win32.dll!lua_pushcclosure`**. Functions by RVA in the module (ImageBase 0x10000000; the module's `.text` is
  identical in build 26577625's install, `../02-exe-internals/new-exe-build-26577625.md`):
  | RVA | Registered as | What it does (from the listing) |
  |---|---|---|
  | 0xBFA0 | `GameInfo`'s `__index` | `GameInfo.T`: the table object of T, made by the export `PushDatabaseTable` |
  | 0xB870 | a table's `__call` | **the filter**: `T()`, `T("where text")` or `T{col = val, ...}`. A table filter is turned into `col = ? and col = ?` (key order of `lua_next`); the statement text is `select * from T` or `select * from T where <cond>`; then the export `PushDatabaseQuery` (a new `Results` userdata with a `__gc` closure, **one `sqlite3_prepare` per call, no statement cache**), the values bound, and an iterator closure returned. Error texts: "Filter must be of type 'string' or 'table'" |
  | 0xB6F0 | the filter's iterator | one step and one row pushed per call (the row object made by the export `PushDatabaseRow`) |
  | 0xB1E0 | the query's `__gc` | the `Results` released when Lua collects the userdata: the prepared statement lives until the next GC cycle |
  | 0xBCB0 | a table's `__index` | `T[id]`, `T.Type`: through the metatable's `__rowdata` cache ("Indexing by ID/type is not available for this table") |
  | 0xBDD0 | a table's `__len` | `#T` |
  | 0xB390 | a table's `__newindex` | the error "Database table is read-only from Lua." |
  | 0xB200, 0xB2E0 | a row's `__index`, `__newindex` | a row object's field reads |
  | 0xBB10, 0xB300 | `__index`, `__len` of a table made inside 0xBCB0 and 0xBDD0 | not followed |
- A table's metatable holds `__tablename` (the SQL table's name), `__db` (the `Connection*` as light userdata) and
  `__rowdata`; the `__call`, `__index` and `__len` closures take that metatable as their one upvalue.

### 6.2 The measuring proxy `[C, the code; 2026-11-24]`

- **Build.** Branch `scratch-5033-dbprobe` (commit 0164b445a: main 9bda43beb + `proxy/src/proxy_dbprobe.cpp` and two call
  lines), built by runbook 1.2 and 1.5 on the shared pin build of 1b87dd185 (`C:/c5-64bit-work/keep/pins-0.1b`) with
  `perf/evidence/5033/build.sh`. NOT a player's build.
- **What it writes.** Data only, two import slots: the database module's slot of `lua51_Win32.dll!lua_pushcclosure`
  and the EXE's slot of the module's `PushDatabaseQuery` (`DB.Query`), at the proxy's boot (before any save loads, so
  every in-game interface state's `GameInfo` is made after it). No code byte of any module is read or written. Every C
  function the module pushes from then on is pushed as one of 64 thunks in the proxy that calls the original between two
  QPC reads and counts it. The proxy pins its own image (the closures keep pointing at the thunks).
- **Filter shapes.** For 0xB870 and 0xBCB0 the thunk reads the upvalue's `__tablename` and `__db` and, for a filter, its
  argument: `all` (no argument), `str` (the where text, first 200 characters) or `tbl` (the keys and value types);
  calls and time are kept per shape. The `Connection`'s live statement count (its export `StatementCount`, a walk of
  `sqlite3_next_stmt`) is read before and after each such call; its own cost is counted apart and is not inside the
  timed span.
- **Output.** `dbprobe-5033.tsv` beside `vpproxy.log` in the server's run folder: cumulative rows every 250 ms when
  something changed (per function: calls, QPC ticks, longest call, net statement change; per shape: calls, ticks). The
  reader `perf/evidence/5033/analyze.py` cuts them at the run's phase boundaries (accurate to 250 ms at each end).
- **The timed span is the whole C function**: for the filter that is the string building, the prepare, the binds and the
  iterator's closure; a prepare alone is not split out. The time is the original function's own wall time on the calling
  thread, with whatever GC step Lua runs inside it.

### 6.3 Runs

- **Job 1, M per AI turn (oldpc).** The turn-time study runner with the arm `S4x5033` (`perf/evidence/5033/mk_arm.py`,
  5044's `S4x5044` form with this layout) and the variant `x5033_human` (`variants5033.json`: 5030's human seat, options
  2+3+4 as the release writes them, Quick Combat and Movement as the save has them, the turn budget), `TS0438-M-T150`,
  through `ts5033.py` (5044's wrapper form; it also copies `dbprobe-5033.tsv` into the run folder). The runner's M
  scenario played turns 150-175 in each run (26 human-seat turns, 2 runs, 52 turns). Per turn the reader cuts: **ai**
  (the harness's end of the human turn done to the next turn's autosave seen: the AI phase), **start** (that autosave
  seen to the harness's next end-of-turn call: the next human turn's start, its records and panels, and the runner's own
  polls), **end_call** (the harness's end-of-turn call itself: units to no moves, an empty queue to wealth, which raises
  dirty events) and **turn** (one end-of-turn call to the next).
- **Job 2, city screens (oldpc).** `perf/evidence/5033/city_job.py` on `product_path.py`, TB-HUGE_0350 (the huge save,
  the human's turn), options 2+3+4: idle 12 s, then the capital's city screen and up to three other cities' screens
  opened (`UI.DoSelectCityAtPlot`), held 8 s, closed (`Events.SerialEventExitCityScreen`), 6 s idle after each; the
  capital opened 3 times in all; then 2 AI turns.
- **EXE build 4390913.** oldpc carries only its Steam EXE of build 4390913 (5032's, 5035's and 5044's deviation, MC
  #13221). The acceptance names 26577625; the database module's `.text` is the same in both installs.

### 6.4 Job 1: per M turn with a human seat `[L, oldpc, job 20261124-222352-335d, 2026-11-24 22:24-22:38 EST, 2 of 2 runs exit 0, quiet and alone (foreign 0.147 cores); build 4390913; analysed 22:40-22:47 by impl-5033]`

Outputs: `perf/evidence/5033/series-335d/` (`series.txt` per function and interval, mean [95 % half-width, normal] over
52 turns; `shapes-<interval>.txt` the shapes; `figures.txt` rows and empty share). The raw probe files and run records
of jobs 1 and 2 and the reader's full JSON are not committed: archived in `D:/c5-64bit-work/5033/` (2026-11-24). Mean interval lengths: ai 3.76 s, start 1.76 s, end_call 0.95 s, turn
**6.45 s** (5044's 6.6 s); 0.3 % of the turn is **19 ms**.

| Per M turn | filter calls = prepares | filter C function | iterator steps | together |
|---|---|---|---|---|
| ai (the AI phase) | **3,371** [±1,329] | 42.7 ms [±16.8] (max 218) | 3,396, 13.0 ms | **55.7 ms** (0.86 % of the turn) |
| start (the next human turn's start) | 11,368 [±1,330] | 144.0 ms [±16.8] | 11,442, 43.7 ms | 187.6 ms |
| end_call (the harness's end of turn) | 828 | 10.6 ms | 833, 3.2 ms | 13.8 ms |
| **turn** | **15,984** [±1,665] | **202.7 ms** [±21.6] | 16,090, 61.5 ms | **264.2 ms (4.1 % of the turn)** |

- **One prepare per filter call** `[L]`: the live statement count rose by exactly 1 in every one of the 831,150 filter
  calls of job 1 (and in every call of job 2); the statements stay alive until Lua's GC runs their `__gc` (0.18 us
  each, in bursts of thousands). A filter call costs **12.7 us** on oldpc, an iterator step 3.8 us. `T[id]`/`T.Type`
  and `#T` are nearly absent (the interface reads these rows through filters); `DB.Query` 4.6 calls a turn (0.16 ms).
- **99.3 % of the filters return no row** `[L]`: iterator steps less filter calls is 106 rows a turn against 15,984
  calls (a loop that runs to its end steps rows + 1 times); the empty share is 99.26-99.43 % in every interval kind.
- **203 distinct shapes** for 831,150 calls: every shape repeats about 80 times a turn. The top 40 shapes (table
  filters such as `{YieldType = ..., BuildingType = ...}` over VP's building, policy and belief yield tables of 0-14
  rows) carry 42 % of the time; the most expensive shape is `Building_YieldFromVictoryGlobal` with four keys (561 calls,
  9.5 ms a turn).
- **Who asks** `[I: from the shapes; the Lua read only to name the functions, D83]`: Community Patch's `TechPanel.lua`
  through `TechButtonInclude.lua`'s `AddSmallButtonsToTechButton`, which reads `Units{PreReqTech}`,
  `Buildings{PreReqTech}`, `Buildings{EnhancedYieldTech}` and the improvement yield tables for the current research and
  calls `InfoTooltipInclude.lua`'s `GetHelpTextForBuilding` for each building it unlocks; that function runs one filter
  per yield and yield table (`Building_YieldFromVictoryGlobal` four times per yield, and so on). The panel rebuilds on
  the dirty events (0518 5.1: `OnTechPanelUpdated` 4.1 times a turn with a human seat, 0.29 s a turn of main-thread time;
  job 1's 0.26 s is that figure's engine part). This is the Architect's ML1 in its engine form.
- **The probe's own cost**: the two `StatementCount` reads per filter call took 200.6 ms a turn (about 6 us each, a walk
  of the live statements), outside the timed spans: the times above are the original functions', but the measured game
  ran that much more main-thread work. Counts are not affected.

### 6.5 Job 2: per city-screen open (H) `[L, oldpc, job 20261124-223809-a3f2, 2026-11-24 22:39-22:45 EST, exit 5 only for LOUD lines (121 of ticket 0576's stale-Unit-instance guard in server.log and 2 lockstep held_skip at the load, as in every product-path huge run, 5044's job 2); every open and close seen, 2 AI turns played, restored by hash; quiet and alone (foreign 0.107 cores); analysed 22:46 by impl-5033]`

TB-HUGE_0350, the human's turn, cities 1102 (the capital), 1567, 2007, 3663 (population 35, 35, 26, 23). Outputs
`perf/evidence/5033/city-a3f2/` (`city.txt` per phase, `shapes-open.txt`, the job's `city_job.json`). The screen was
up 0.50-0.74 s after the request and gone 0.52-0.67 s after the close (the channel's 0.3 s polls included).

| Phase | filter calls = prepares | filter C function | iterator |
|---|---|---|---|
| idle, 12 s before the first open | 0 | 0 | 0 |
| open and 8 s held (each of 6) | 2,894-4,341 | 37.9-59.3 ms | 12.8-19.8 ms |
| close and 6 s idle after it (each of 6) | 1,447-2,894 | 18.7-39.2 ms | 6.2-13.3 ms |
| **one open-and-close cycle** (the two summed) | **5,788-5,795** | **76.1-80.1 ms** | **25.6-27.1 ms** |
| 2 AI turns (350-351; turn intervals 24.2 and 15.6 s) | 13,198 | 175.3 ms | 59.3 ms |

- The work comes in **units of 1,447 filters** (about 19 ms of filter and 6 ms of iterator): every open and every close
  raises two of them, whichever city (the 250 ms rows split a pair 3:1 or 2:2). The screen itself asks for almost
  nothing; the shapes are the tech panel's (6.4), rebuilt on the dirty events of the selection change. Nothing runs
  while idle.
- **Per city-screen open on the huge save: about 5,790 prepares and 103 ms of main-thread time** (filter 78 ms,
  iterator 26 ms), in four bursts of about 25 ms, two at the open and two at the close `[L]`. Whether each burst falls
  into one frame was not matched with the frames `[I]`.
- **Per H turn** (the two AI turns' intervals, the human turn's start included): 6,600 prepares, 88 ms + 30 ms = 117 ms;
  0.3 % of the 19.9 s mean turn is 60 ms.

### 6.6 What a prepare costs, and what the two cuts would leave `[L, oldpc, job 20261124-224502-ef5b, 2026-11-24 22:45-22:46 EST, quiet; a copy of tb1's cache Civ5DebugDatabase.db (SHA-256 d8fbf247...) loaded into an in-memory database; SQLite 3.7.17 from the third-party lock's amalgamation, x86 /O2; 2,000 repeats per statement]`

`perf/evidence/5033/prepbench.c` (run by `bench_job.py`) times the 203 statement texts the module builds for job 1's
shapes (`stmts.json` from `mk_stmts.py`); `weigh.py` weights them by job 1's calls per turn
(`bench-ef5b/weigh-turn.txt`). Per M turn (15,984 filters):

| | per call | per M turn |
|---|---|---|
| the module's filter C function, in game | 12.7 us | 202.7 ms |
| `sqlite3_prepare_v2` + `sqlite3_finalize` alone (bench) | 4.3 us (34 % of the in-game call) | 68.9 ms |
| prepare + binds + one step + finalize (bench: the SQL part of an empty filter) | 6.6 us | 105.1 ms |
| `sqlite3_reset` + rebind + one step on a kept statement (bench: statement reuse) | 1.8 us | 28.3 ms |
| the iterator's steps, in game | 3.8 us | 61.5 ms |

The other 8.4 us of the in-game filter call are the module's own Lua work (the key loop and the `lua_concat` of the
condition, the statement text's formatting, the `Results` userdata and its metatable, the iterator closure) `[I: by
difference]`. The expensive single statements are the wide tables: `select * from Buildings where PreReqTech = ?` takes
313 us to prepare, `Units` 107 us (one call each per rebuild). A first bench on the database file itself gave 70-140 us
per step: SQLite's file lock and change-counter read per statement; the game's connection is in memory, hence the
in-memory copy (negative result, 2026-11-24).

### 6.7 Verdict: GO for the empty answer; statement reuse alone does not clear the floor in the AI phase `[2026-11-24, impl-5033; the figures of 6.4-6.6, build 4390913]`

Floor: 0.3 % of a turn: **19 ms per M turn** (6.45 s), 60 ms per H turn (19.9 s).

| Cut | Reaches (main-thread time) | Against the floor |
|---|---|---|
| **Statement reuse by condition shape** (one kept statement per SQL text: reset and rebind instead of prepare and finalize) | about 77 ms per M turn (105.1 - 28.3 ms), 16 ms of it in the AI phase; 32 ms per H turn; about 28 ms per city-screen cycle | above it per M turn; **below it in the AI phase** (16 of 19 ms) and per H turn (32 of 60 ms) |
| **An empty answer from a per-table index** (the filter's conditions looked up in a set of the table's values first; 99.3 % of the filters have no row) | nearly the whole filter call and its one iterator step, about 15.5 of 16.5 us per empty call `[I: the answer itself taken as about 1 us]`: about **245 ms per M turn** (52 ms in the AI phase, 175 ms at the human turn's start), about 110 ms per H turn, about **95 ms per city-screen cycle** | **above it** in every case (3.8 % of an M turn, 0.55 % of an H turn) |

**GO** for T3's second half as the main cut, with statement reuse as its route for the 0.7 % of filters that have rows.
What the gain is, and what it is not:

- It is **main-thread time**, the interface's Lua. With a human seat the AI thread waits for the Lua section only
  0.020 s a turn (0522, 5.1), so the AI phase's wall time can fall by at most about that `[I]`; the 52 ms of the AI
  phase are frame time while the AI plays. The real effects are the human turn's start (175 ms in the 1.76 s before the
  player's first input; it fits 5044's EX13, 0.3 s of long frames and a 60 ms record group at the turn start `[I]`) and
  **every city-screen open and close: about 95 of 103 ms on the huge save, in four bursts of about 25 ms**.
- **No code write is needed** `[I: by the probe's route]`: the module registers its filter through its own import slot
  of `lua_pushcclosure`, so a product form can register its own filter in place of RVA 0xB870 (written from 6.1's
  description of what the module does, D83) with the same data write the probe makes, behind an option of its own and
  not under `FullExeOptimizations` (D228: an import slot is a base tool). It must give the same rows in the same order
  (a kept statement has the same plan) and the same errors ("Filter must be of type 'string' or 'table'", a bad column's
  SQL error), and the index must follow SQLite's rules for `=` (the column's affinity against an integer, real, text or
  boolean value, `NULL`, the column's collation) or fall back to the statement; T3's verify run compares every filter's
  rows with the module's own.
- **The other route is upstream's** (T4, the file form): `GetHelpTextForBuilding` asks four times per yield of each of
  about 20 tables of 0-14 rows, for every building the current tech unlocks; a per-file cache of those tables' rows by
  building would remove the same calls in Lua (for the owner, after the beta by D130, as an upstream text without internal
  ids, D95). Either removes the same 0.26 s a turn; the engine form also covers every other mod's filters.
- The Architect's caution ("more than half of the measured Lua time belongs to the observer seat's tech panel, which a
  human player does not pay") holds for the per-AI-player rebuild only: the human seat still pays the tech panel on the
  dirty events of its own turn (0518 5.1) and on every city-screen open and close (6.5).

### 6.8 Not verified (5033)

- **Build 26577625**: nothing ran on it (oldpc carries 4390913 only; MC #13221's deviation for 5032, 5035, 5044). The
  database module's `.text` is the same in both installs (`../02-exe-internals/new-exe-build-26577625.md`), so the
  counts and per-call times should hold `[I]`; the EXE's import slot of `PushDatabaseQuery` is found by name.
- The probe's `StatementCount` reads (200 ms of main-thread work per M turn, outside the timed spans) were in the
  measured game; no run without them, so the times may differ a little from a clean game's (caches, GC) `[I]`.
- The split of the in-game 12.7 us (34 % prepare) comes from the bench on the same machine and SQLite release with one
  text value bound per parameter, not from inside the game; the bench's database is the cache file the game wrote at
  that load `[I]`. The module's Lua work (8.4 us) is by difference.
- The source attribution (CP's tech panel through `GetHelpTextForBuilding`) is from the shapes and a read of the Lua,
  not from a stack: 0518's Lua-attribution build was not combined with this probe.
- No frame matching for the city-screen bursts; H per turn from two AI turns of one run; M from one save
  (`TS0438-M-T150`, turns 150-175).

## 7. Interface Lua per file with a human seat, and the cut while a panel is hidden (ticket 5034, 2026-11-25)

impl-5034 (account A), 2026-11-25 02:13-03:35 EST, branch `task-5034-ui-lua-cuts` (evidence, reader, this section) and the
scratch branch `scratch-5034-luafile` (the measuring proxy, never merged). The question (the Architect's draft T4, rows ML1's
file form, ML2, ML3 and A14 of `../08-performance/external-port-speedups-review.md`): which Vox Populi or EUI interface Lua
files cost 0.3 % or more of an M or H turn with a human seat, and for those, the exact cut while their panel is hidden.
Measurement only.

### 7.1 The measuring proxy `[C, the code; 2026-11-25]`

- **Build.** Branch `scratch-5034-luafile` (commit 64cecca51: main 39e736026 + `proxy/src/proxy_luafile.cpp` and two call
  lines; copies in `perf/evidence/5034/proxy_luafile.cpp.txt` and `proxy_wiring.diff.txt`), built by runbook 1.2 and 1.5
  on the shared pin build of 1b87dd185 with `perf/evidence/5034/build.sh`; layout hashes `perf/evidence/5034/layout-SHA256.txt`
  (proxy `4ecb2698...`, server `e2d801dd...`, composite `82c8a85e...`, x64 DLL `b84aa3e9...`). NOT a player's build.
- **What it writes.** Data only: the import slots of `lua51_Win32.dll` `lua_pcall`, `lua_call` and `lua_cpcall` in the EXE's
  import table (0518's and 5030's method) and in the proxy's own import table (the proxy's Lua channel and its event
  drain), at the proxy's boot. No code byte of any module. Each wrapper calls what the slot held.
- **What it counts.** Every hooked call is a frame keyed by (thread, API, call site, callee). A Lua callee is named by its
  chunk name from `MODS\` or `Assets\` on and its `linedefined`; a C callee by its address. **Self time** = the frame's time
  less the frames opened inside it, so a listener the EXE calls from inside another listener counts to its own file; Lua
  the handler calls directly (includes, `GameInfo` filters, the EXE's C functions) is inside its handler. Before the timed
  span, for a Lua callee, the wrapper asks `ContextPtr:IsHidden()` of the callee's environment through a protected call
  of the real `lua_pcall`: each frame is **hidden**, **shown** or unknown (no `ContextPtr`). Output `luafile-5034.tsv`
  beside `vpproxy.log`, cumulative rows every 250 ms; reader `perf/evidence/5034/analyze.py` (5033's interval form: `ai`
  = the harness's end of the human turn done to the next autosave, `start` = that autosave to the next end-of-turn call,
  `end_call` = the harness's call, `turn` = one end-of-turn call to the next).
- **Call sites seen** (EXE VA at 0x400000, build 4390913): `lua_call` from 0x558AF0 (an interface event's listeners,
  0518 2.2), `lua_pcall` from 0x9045D5 (`ContextPtr:SetUpdate`), `lua_cpcall` from 0x904E52 (the shared protected-call
  helper) `[L]`; they match 0518's.
- **Its own cost.** Its bookkeeping and the hidden check outside the timed spans: 58 ms per M turn, 306 ms per H turn
  (about 1 %) of extra main-thread work in the measured game; the turn intervals contain it, the per-file times do not.

### 7.2 Runs `[L, oldpc, 2026-11-25; build 4390913 (oldpc has no 26577625; MC #13819); instance tb1, NVIDIA GeForce RTX 2080, driver 32.0.16.1088 (D175)]`

The turn-time study runner through `perf/evidence/5034/ts5034.py` (arm `S4x5034`: S4f's product layout with this proxy;
variant `x5034_human`: 5030's human seat, options 2+3+4 as the release writes them, Quick Combat and Quick Movement as the
saves have them (on)). Instrument runs, grade D (per-file attribution, not a timed series).

- Smoke `20261125-023231-7521` (M, 1 run x 3 turns, exit 0): probe on, all six slots written.
- **M** `20261125-024146-ab09` (02:42-02:58, quiet, alone, foreign 0.118 cores): `TS0438-M-T150`, 3 runs x 12 turns
  (150-161), 3 of 3 exit 0; 36 turn intervals, mean **6.649 s** [+-0.433] (5033's uninstrumented 6.45 s). Floor 0.3 %:
  **19.9 ms**. Report `perf/evidence/5034/series-M-ab09/series.txt`.
- **H** `20261125-024224-6a5d` (02:58-03:23, quiet, alone, foreign 0.165 cores): `TS0438-H-T300`, 3 runs x 6 turns
  (300-305), 3 of 3 exit 0; 18 turn intervals, mean **29.23 s** [+-2.59]. Floor 0.3 %: **87.7 ms**. Report
  `perf/evidence/5034/series-H-6a5d/series.txt`.
- Raw rows, run records and the reader's JSON are archived in `D:/c5-64bit-work/5034/` (not committed).

All hooked interface Lua per turn (harness channel left out): M 523 ms (5030 measured 494 ms of main-thread Lua per M turn
with a different instrument), H 1,545 ms.

### 7.3 Per file: self time per turn, the hidden part, go or no-go `[L]`

Mean [95 % half-width] over the turns, median where it differs; a file absent in a turn counts 0. "Hidden" = the part of
the file's self time whose frames ran while the function's own context was hidden.

| File (from `MODS\` or the install's `Assets\`) | M ms/turn (floor 19.9) | H ms/turn (floor 87.7) | hidden | Verdict |
|---|---|---|---|---|
| CP `(1) Community Patch\Core Files\Overrides\TechPanel.lua` | **216.5** [+-26.1] | **144.3** [+-16.8] | **100 %** (every call, M and H) | **GO**: above on both; the cut in 7.4 |
| EUI `(3a) ...\LUA\NotificationPanel.lua` | 60.9 [+-8.2] | 204.1 [+-33.6] | 0 | above; no hidden time: **no cut of this form** |
| EUI `(3a) ...\LUA\CityBannerManager.lua` | 46.2 [+-2.8] | 258.7 [+-29.9] | 0 | above; no hidden time: no cut of this form |
| EUI `(3a) ...\LUA\UnitFlagManager.lua` | 44.6 [+-1.6] | 210.6 [+-13.3] | 0 | above; no hidden time: no cut of this form |
| EUI `(3a) ...\LUA\UnitPanel.lua` | 40.3 [+-2.4] | 243.2 [+-170.1], median 56.7 | 0 | above on M; no hidden time: no cut of this form |
| EUI `(3a) ...\ImprovedTopPanel\TopPanel.lua` | 30.6 [+-2.4] | 68.2 [+-9.4] | 0 | above on M only; no hidden time: no cut of this form |
| EUI `(3a) ...\LUA\PlotHelpManager.lua` | 2.5 | 91.6 [+-119.2], median 3.2 (one turn 826 ms) | 0 | below by its median; no cut |
| VP `(2) Vox Populi\Core Files\Overrides\CultureOverview.lua` | 9.7 [+-1.4] | 36.9 [+-6.4] | 100 % | **below** on both: no cut (the textbook hidden refresh, `RefreshSwapGreatWorks` on `SerialEventGreatWorksScreenDirty`, 2.3 calls a turn, but too small) |
| base `Assets\UI\InGame\UnitMemberOverlay.lua` | 14.5 | 62.4 | 0 | below; not VP/EUI |
| VP `EspionageOverview.lua`, CP `ActionInfoPanel.lua`, `InGame.lua`, EUI `LeaderHeadRoot.lua`, `DiploCorner.lua`, every other file | under 6 ms (M), under 10 ms (H) | | | below |

- **Only one file has hidden time above the floor: CP's `TechPanel.lua`.** All of it is `OnTechPanelUpdated` (line 47) on
  `Events.SerialEventGameDataDirty` (the EXE's event dispatch, `lua_call` from 0x558AF0): 3.7 calls per M turn of 58 ms each
  (59.6 ms in the AI phase, **149.2 ms at the human turn's start**, 7.7 ms in the harness's end-of-turn call), 5.2 calls
  per H turn of 28 ms (78.3 ms in the AI phase, 31.4 at the start, 34.6 in the end call). Its other listeners (line 169 on
  `AIProcessingStartedForPlayer`, 25 calls a turn, 0.4 ms; line 151 on `SerialEventGameMessagePopup`, 0.25 a turn) are
  nothing with a human seat (0522 5.1).
- **The panel was hidden in every one of the 54 turns** `[L]`, and why it is hidden is in the Lua `[C: CP's TechPanel.lua,
  the install's InfoCorner.xml and InfoCorner.lua, read only]`: the info corner loads the `TechPanel` context with
  `Hidden="1"`; at the load the corner's active-player handler opens `InfoCornerID.None` (no saved choice for the player);
  CP's `TechPanel.lua` shows itself only on `Events.OpenInfoCorner(InfoCornerID.Tech)`, which it raises itself only when a
  `BUTTONPOPUP_CHOOSETECH` or `BUTTONPOPUP_CHOOSE_TECH_TO_STEAL` popup comes while it is hidden; the player can also pick
  it from the corner's menu. So **after every load the panel is hidden until the player's next research choice popup or
  their own choice of the corner**; a player with a research queue may never see the popup. In the measured games no
  such popup came in 12 M or 6 H turns. While shown, the same refresh draws a visible panel and this cut saves nothing.
- **The five visible EUI files above the floor** (NotificationPanel, CityBannerManager, UnitFlagManager, UnitPanel,
  TopPanel) ran 0 ms while hidden: their time is `ContextPtr:SetUpdate` passes (`UpdateCivListNow` line 1393,
  `RefreshCityBannersNow` 969, `UpdateDisplayNow` 1988, the top panel's 3142) and per-unit and per-city listeners
  (`UnitFlagManager.lua` 945, 881, 896, 1022; `CityBannerManager.lua` 1594, 1683, 1837) of panels that are on screen. EUI
  already defers them to one pass per frame. A cut there changes what a shown panel draws per frame (or needs a rewrite of
  the panel's update), which is not T4's form; none is proposed here. The per-unit flag listeners are the ones 4677 (A11,
  A12) thins at the DLL's source.
- The large H outliers (UnitPanel 921 ms and PlotHelpManager 826 ms in one turn each, in the AI phase) were not followed;
  their medians are 57 and 3 ms.

### 7.4 The exact cut proposed for the MC: CP `TechPanel.lua`, no refresh while hidden `[I: the form; C: the code it changes, read only]`

The form (CP's file; an upstream text for the owner, D95, after the beta by D130, or the 0638 override route if the owner
prefers):

1. A file-local flag `bPending` (false).
2. At the top of `OnTechPanelUpdated`: when `ContextPtr:IsHidden()`, set `bPending = true` and return. Everything after it
   is unchanged.
3. `ContextPtr:SetShowHideHandler(function (bIsHide) if not bIsHide and bPending then bPending = false;
   OnTechPanelUpdated() end end)` (the file sets no show-hide handler today). The file's own call at its load runs while
   the context is hidden, so the first show refreshes.

Why it shows the same text and selection: what the function writes (the icon, the small buttons and their callbacks, the
meter, the text, the three tooltips, `eCurrentTech` and `eRecentTech`) is only read by the panel's own controls, which
draw and take clicks only while shown; the right-click callback reads `eCurrentTech`/`eRecentTech`, which the show's
refresh sets before any click can come. At each show the refresh runs from the state at that moment, so what is drawn
equals the eager form's last refresh whenever every change since that refresh raised `SerialEventGameDataDirty`; where a
change raised none, the deferred form shows the newer value. What the change ticket must still prove: that the show-hide handler runs inside `SetHide(false)` before the next frame
draws, and, by a Lua-channel dump of the controls' texts, tooltips, visibility and the small buttons in both forms, equal
output after each dirty event while shown and after a show that follows hidden dirty events. **Proven live by ticket
0750, section 7.6 (2026-11-25)**; this paragraph said "[I: not verified here]" until then. 0750's candidate keeps the
refresh body as `RefreshTechPanel` and lets the handler call it directly (17 added lines, 7.6.1); the three-line form
above also works, because `ContextPtr:IsHidden()` already reads false inside the handler on a show (7.6.3).

**Reach** (the measured games, the panel hidden throughout): 216 ms per M turn (3.3 % of the turn; 149 ms of it in the
1.6 s before the player's first input at the turn start, where 5044 found EX13's long frames) and 144 ms per H turn
(0.49 %); nothing while the panel is shown. It is main-thread time: with a human seat the AI thread waits for the Lua
section only about 0.02 s a turn (0522 5.1), so the AI phase's wall time falls by at most its frames' share `[I]`.

**Overlap with 5033** `[I]`: most of this refresh is `GameInfo` filter calls (section 6.4: the tech panel through
`AddSmallButtonsToTechButton` and `GetHelpTextForBuilding`, 0.26 s of filter time per M turn). 5033's engine form (an empty
answer from a per-table index) and this cut reach the same time; their sum is not additive. This cut is a three-line Lua
change in one file with no engine part; the engine form also covers the city screen and every other mod.

### 7.5 Not verified (5034)

- **Build 26577625**: nothing ran on it (oldpc has 4390913 only; MC #13819 agreed). The Lua and the EXE's dispatch are the
  same files; the per-file times should hold `[I]`.
- The hidden check reads the context's own flag; a context whose parent is hidden while its own flag is clear counts as
  shown (the cut would then not apply either).
- Lua errors caught by a Lua-level `pcall` inside a hooked frame leave that frame open until the enclosing protected
  wrapper closes it, and would shift time to the wrong frame; the probe cannot see such a case (its frame stack never
  overflowed: 0 frames dropped in the six runs).
- The panel's visible case (after a research choice popup) was not run; the cut saves nothing there by its form.
  (0750, 7.6: the panel shown by the corner's own event was run; the cut leaves the shown panel byte-equal.)
- The H outliers of UnitPanel and PlotHelpManager were not attributed below the entry.
- 5033's engine cut and this cut were not measured together.

### 7.6 The cut proven equal live: CP `TechPanel.lua`, eager against deferred (ticket 0750, 2026-11-25)

impl-0750 (account A), 2026-11-25 03:36-04:4x EST, branch `task-0750-techpanel-deferral`. Evidence and tools:
`perf/evidence/0750/` (its README). Nothing shipped, nothing sent upstream. The shipping route: 7.6.6.

#### 7.6.1 The candidate `[C: make_candidate.py; L: loaded without a Lua error]`

`perf/evidence/0750/make_candidate.py` makes it from the player's own installed file at run time (no byte of Vox Populi's
file is committed, as 0638): it refuses any file but 5.4.8's installed CRLF form (SHA-256 `5efa5495...2769`; the same
file is on upstream `master` at c434d45da, 2026-11-20: `git diff Release-5.4.8 upstream/master` on it is empty), renames
the refresh body `OnTechPanelUpdated` to `RefreshTechPanel` (one line), and adds before
`Events.SerialEventGameDataDirty.Add(OnTechPanelUpdated);`:

```lua
local bRefreshPending = false;
local function OnTechPanelUpdated()
	if ContextPtr:IsHidden() then
		bRefreshPending = true;
		return;
	end
	RefreshTechPanel();
end

ContextPtr:SetShowHideHandler(function (bIsHide)
	if not bIsHide and bRefreshPending then
		bRefreshPending = false;
		RefreshTechPanel();
	end
end);
```

Candidate SHA-256 `03e22fdd...76dc4`, 7,034 B. Every existing caller (the `SerialEventGameDataDirty` listener, the
`GameplaySetActivePlayer` and `AIProcessingStartedForPlayer` listeners, the call at the file's load) now reaches the new
`OnTechPanelUpdated`; the two listeners that change the file-local `eActivePlayer` set it before the call, so a pending
refresh reads the right player.

#### 7.6.2 Method `[C: the scripts]`

- **Two headed testbed jobs on oldpc** (`perf/evidence/0750/tp0750_job.py`, a wrapper of
  `tools/testbed/jobs/live_session.py`), instance `tb1`, EXE build 4390913, the instance's DLL (the pin
  `i0ship-26234371-ia`, quiet-testbed.md 24), CP + VP + EUI, a copy of `TS0438-M-T150` (player 0 human, turn active, no
  autoplay). Eager job `20261125-035158-ec30` (03:52-04:09 EST): the installed file, checked to be 5.4.8's. Deferred job
  `20261125-040847-c6b1` (04:09-04:22 EST): the installed file backed up by SHA-256 in the job's work folder, the
  candidate written, put back by hash in the restore before `provision.py verify` (record: candidate `03e22fdd...` in
  place, after the run `5efa5495...` again, verify 0). Both: exit 0, stop 0, restore and verify ok; `Lua.log` has no
  error beyond the `VPUI_loader.lua` line every instance has.
- **The helper** (`helper0750.lua`) sent through the Lua channel into the `TechPanel` state (`--state TechPanel`). The
  channel runs on the game-core thread, so every interface action and every dump runs as a main-thread job from a
  `LocalMachineAppUpdate` listener (one job a frame). A dump reads all 13 controls of `TechPanel.xml` (`IsHidden`,
  `GetText`, `GetToolTipString`, `GetSizeVal`, `GetVoid1/2`, `IsDisabled`), the context's own hidden flag, the game
  state the panel reads, and the last arguments of 17 setters (`SetText`, `SetToolTipString`, `SetTexture`,
  `SetTextureOffsetVal`, `SetPercents`, `SetSizeVal`, `SetHide`, `SetVoids`, ...) called on each of the panel's controls
  since the helper was installed: the icon's texture and offset and the meter's percents have no getter.
- **The steps** (`drive0750.py`, the same 20 in both jobs; S = panel shown, compared; H = hidden, reported): research
  choice by `Network.SendResearch` (as the tech tree sends it), research progress +5 by `ChangeResearchProgress`, gold +5,
  `ClearResearchQueue`, show and hide by `Events.OpenInfoCorner` (as the corner's pull-down sends it), one end of turn
  hidden and one shown (`turns.py` through live_session). H: load, research, progress; S: show; S: research, progress,
  gold, clear (the panel's "FINISHED" branch), research; H: hide, research, progress; S: show; H: hide; S: show with no
  dirty event between (the no-pending path); H: hide; H: end turn (26 `SerialEventGameDataDirty` in it); S: show; S: end
  turn shown; then a probe of the show-hide handler.
- **Compare** (`compare0750.py`): per step the eager and the deferred dump, byte for byte, and "drawn equal": a control
  hidden in both forms compares only as hidden (it draws nothing and takes no hover or click). The eager session's first
  helper install failed on `rawequal` (7.6.5) before it registered anything; the corrected helper (identical dumps, plus
  the corner log) ran in both sessions.

#### 7.6.3 Results `[L, oldpc, 2026-11-25, build 4390913]`

| Steps | Result |
|---|---|
| S: research, progress, gold, clear, research while shown; show after no dirty event; end turn while shown (7 steps) | **byte-equal** dumps in both forms |
| S: show after hidden dirty events (load + research + progress; research + progress; a hidden end turn) (3 steps) | **byte-equal** in two, **drawn equal** in the first: the only difference is the fifth small button `B5`, hidden in both forms, whose tooltip and text in the eager form are stale from the load's refresh of the previous tech (Machinery had five small buttons, Theology four; `AddSmallButtonsToTechButton` hides an unused button without clearing it). Not visible, not reachable |
| H: while hidden | the eager form refreshes on every dirty event (26 times in the hidden end turn); the deferred form keeps the last shown state (after the load the XML defaults: the file's own call at its load already runs while hidden) |

`perf/evidence/0750/compare-brief.txt` has all 20 steps: **10 S steps, 0 different** (9 byte-equal, 1 drawn equal).

**The handler runs inside `SetHide` and before the next frame draws** `[L]`:
- Probe (step s19, both forms): a show-hide handler set from a main-thread job, then `ContextPtr:SetHide(true)` and
  `SetHide(false)` in the same job: the handler had run once when the first call returned and twice when the second
  returned, all in one frame; inside it `ContextPtr:IsHidden()` already read the new state (true on hide, **false on
  show**) and `bIsInit` was nil. So the three-line form of 7.4 (the handler calls `OnTechPanelUpdated`, which checks
  `IsHidden()`) would refresh too.
- The real path (deferred form, steps s03, s12, s17): `Events.OpenInfoCorner(InfoCornerID.Tech)` sent from Lua is not
  dispatched inside the call (the panel read right after it, in the same Lua call, was still hidden with its stale
  text); the EXE dispatches it in a later frame. A listener the helper added after the panel's own (the corner log) read
  the panel in that same dispatch, right after the panel's listener had called `ContextPtr:SetHide(false)`: shown, with
  the fresh text (s03 `Theology (10)` where the panel had never been refreshed; s12 `Education (11)` over the stale
  `Theology (10)`; s17 `Education (9)` over `Education (11)` after the hidden end turn). The interface's Lua and its
  drawing are on the EXE's main thread (2.1), so the refresh is done before the shown panel is first drawn.

#### 7.6.4 What it does not show

- **Timing: not measured.** The saving is 5034's measurement (7.3: 216 ms per M turn, 144 ms per H turn, all of it while
  hidden); a shipping change re-measures it under `docs/briefs/PERFORMANCE.md`. The deferred form still runs one
  `IsHidden()` check per dirty event.
- Build 26577625: not run (oldpc has 4390913 only, as for 5034). The Lua is the same file.
- Not exercised: `GameplaySetActivePlayer` (hot seat) and the observer path of `AIProcessingStartedForPlayer` with the
  panel hidden then shown; the research choice popup that shows the panel itself (`BUTTONPOPUP_CHOOSETECH`). All reach
  the same `OnTechPanelUpdated` and the same show path `[C]`.
- The comparison is of what the Lua sets and reads back, not of pictures.

#### 7.6.5 Helper facts for the next interface-Lua test `[L, 2026-11-25]`

- In a context's environment through the channel: `rawget`, `rawequal`, `_G` and `getfenv` are nil; `getmetatable`,
  `setmetatable`, `pcall`, `loadstring` exist. A control's metatable (`CTypeName`, `BaseType`, `__eq`, `__tostring`) has
  a C function as `__index`: methods cannot be listed or wrapped in a table; replacing `__index` with a Lua function that
  calls the old one works (`helper0750.lua`). `Controls.X` returns the same userdata on every access (usable as a table
  key).
- Getters that exist: `GetText`, `GetToolTipString`, `GetSizeVal`, `GetVoid1/2`, `IsHidden`, `IsDisabled`; not
  `GetTexture`, `GetTextureOffset`, `GetPercent(s)`.
- `lua.py` prints a single string result in Lua's `%q` form (quotes, a newline as backslash + newline): decode it
  (`drive0750.unquote`).
- `Events.OpenInfoCorner(...)` called from Lua is dispatched in a later frame, not inside the call.

#### 7.6.6 The shipping route: recommendation for the MC `[I]`

- **0638's override route** (the installer's `vpfix` step: VP's file replaced by hash, backed up, put back on removal)
  ships only "upstream's bytes or nothing" (MC ruling for 0638, `upstream-lua-fixes.md` 8): a file must equal the file
  at an upstream commit. This candidate is ours, so the route needs either upstream's merge or an MC ruling that widens
  that rule. Its D255 form would be an installer box like the VP fixes' (options-file.md R13: a `VP64Options.ini` key
  cannot act on a file placed at install time), ticked by default since the proof holds.
- **Upstream** (D95 scrubbed text, after the beta by D130, on the owner's yes): helps every VP player, 32-bit too; the
  diff applies to upstream `master` unchanged. A draft is in `perf/evidence/0750/upstream-draft.txt` (not sent).
- **Recommendation: upstream first, then 0638's route with upstream's bytes.** Once upstream merges it, the file
  qualifies for 0638's table as it stands (5.4.8's file plus that one fix), behind the VP-fixes box or a box of its own
  (D255). No early ship by widening 0638's rule: priority 2 (epic 0647's tier), and 5033's engine form (6.7) reaches
  the same `GameInfo` time.
- **Against 5033's engine form:** this cut is cheaper to ship (17 lines of Lua in one file, no engine part, proven equal
  here); the engine form also covers the city screen and every other mod. Not additive: if the engine form ships first,
  what this cut still saves is the rest of the refresh (not measured).
