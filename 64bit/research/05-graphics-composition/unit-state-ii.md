# State (ii): the EXE's unit drawing off, its unit simulation on, in the product (check F2)

Issue 0317, 2026-11-15, implementer impl-B-0317 (account B), branch `task-0317-unit-state-ii`. The check F2 of the
Architect's increment 3 sheet (`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md`
section 4). Marks: `[L]` seen live in this issue's runs, `[C]` read in code or records, `[I]` inferred. Every picture
named here is a DIAGNOSTIC (D112): a test key list and a branch build.

## 0. Results in short

1. **The product's list + Unit crashes at boot** [L]. With MovementPath on (the product since 0179), `Unit = 1` kills
   the EXE 28 s after launch, before any front end: AV at VA **0x9A3CF7**, which is crash A of
   `research/02-exe-internals/world-systems-and-stubs.md` section 6 (MovementPath's startup uses the Granny pose pool
   that only Unit's boot startup builds). State (ii) in the product therefore needs **MovementPath off as well**, or
   the pose-pool fill by a call into the EXE (stub 2 of that section), which needs the owner's yes as D136 did.
2. **With Unit and MovementPath off (arm unitnomp) the game loads and plays the human's turn**, but the EXE draws no
   unit models (flags only) and no path discs or chevrons [L].
3. **The AI turn stalls in state (ii), reproducibly** [L]: in arm unitnomp the AI turn of turn 350 never ended
   ("Processing Turn For The Aztecs"), on c5h-e and again on c5h-c: the EXE stopped calling `ICvGame1::Update` and
   called `ICvPlayer1::GetUnit` + `ICvUnit1::IsDelayedDeathExported` + `ICvUnit1::IsBusy` once per frame instead. The
   same save, stimuli and turn passed with the product's list (Unit on) twice, and with the control arm basenomp
   (MovementPath off, Unit on) once: **the stall belongs to the Unit key**. **Where it waits (issue 0335, the dumps
   read, section 9)** [C]: the game-core thread sits in its own `SuspendThread` between two updates, and the EXE's
   update hold counter (0x2DE044C) is 1 in both EXE dumps: the Aztecs' AI leader message to the human
   (`GameplayDiplomacyAILeaderMessage`, one call through the proxy in each stalled run [L]) raised the hold, which
   only the leaving of the leader screen drops, and in state (ii) the leader screen never came up (no
   `AILeaderMessage` reached `LeaderHeadRoot` [L]; no leader scene loaded [C]). *Corrected 2026-11-15 (0335); it read:
   "so the EXE most likely waits for its own unit visualisation to finish, which the Unit system's drawing side never
   does in state (ii) [I]".*
4. **Which unit events reach Lua** [L]: in state (ii) `SerialEventUnitCreated`, `...FacingChanged`,
   `...TeleportedToHex`, `...MoveToHexes`, `...InfoDirty`, `UnitSelectionChanged`, `UnitVisibilityChanged`,
   `UnitShouldDimFlag`, `UnitActionChanged`, `DisplayMovementIndicator`, `UIPathFinderUpdate` still fire.
   **`LocalMachineUnitPositionChanged`, `UnitHexHighlight`, `UnitMoveQueueChanged`, `SerialEventUnitSetDamage` and
   `SerialEventUnitDestroyed` stop.** So the flags lose their smooth motion and also their damage and death updates.
5. **Sounds** [L]: in state (ii) the unit's creation, selection and quick-move sounds still play at the same peak as
   with Unit on; **the sound of a walk (quick movement off) is missing** (peak 0.011 against 0.158 in the control, same
   instance; section 4).
6. **Memory and load time** (one launch per arm, the workstation busy with 6 other games: an indication, COMMON 23).
   Same instance, unitnomp against the control basenomp (both MovementPath off): EXE private bytes after the load
   1,341.4 against 1,393.8 MB, **-52 MB**; largest free block +64 MB; after the probes 1,365.2 against 1,521.7 MB,
   **-157 MB** (the unit visuals the EXE builds during play are not built). `Unit_System::Startup` (6.7-7.3 s at boot)
   is gone; the restart at mod activation stays (`LoadUnitLibraries` 8.9-9.2 s). Launch to in-game 205 against 202 s,
   `LoadGameState_TOTAL` 47.1 against 54.2 s: no load-time gain visible at this noise.

**What state (ii) must supply for the stall** (corrected 2026-11-15 after the dump read, section 9): the leader
screen. An AI leader's message to the human must come up as the leader screen (so that its closing drops the EXE's
update hold), or the hold must not be raised. Units are not involved [C]. *It read before: "the end of the unit's
visualisation ... or the EXE must not enter the wait. Which object the EXE waits on is the next step" [I].*

**Recommendation for the sheet's question** (positions from events through the bridge, or the server as the
authority with the anchor table): **the server as the authority with the anchor table** (hand-over designs 6.3).
`LocalMachineUnitPositionChanged` does not fire in state (ii), so there are no positions to take; and the damage and
death events stop too, so the bridge must raise `SerialEventUnitSetDamage` and `SerialEventUnitDestroyed` (from the
DLL's `GameplayUnitSetDamage` / `GameplayUnitDestroyed*` calls) besides the position events. State (ii) as tested is
not yet a playable product state: the AI turn stall (item 3) must be understood first, and MovementPath must go off
or the pool must be filled (item 1).

## 1. What was run

- **Configuration.** The product path on the workstation: the 32-bit EXE as the shell, the proxy in the CP slot,
  `server.exe` with `[Renderer] ServerRenderer = 1`, the composite and its manifest beside the proxy, `config.ini`
  `[DisableSystems]` = `shell/tools/darkred.py`'s D33 list with Overlay kept on (D93), no viewmask marker, `vpproxy.ini`
  `mode=on local_answers=1 local_verify=0 lua_mirror=0` (`shell/evidence/3308/run_3308.py`'s prep, used through
  `shell/evidence/3311/run_3311.py`). Builds from repository 33005c9e on the x64 pin 04b1211b8: x64 DLL 74588135 (the
  pin), `server.exe` 0364bf51, proxy 5c528806, composite ea24823d.
- **The Unit key** is set only in the instance's `config.ini` by the driver (a test copy of the key list).
  `darkred.py` and `proxy/src/proxy_session_keys.h` are unchanged: the shipped default stays without it. The proxy's
  session rule still classifies the session as D33 (its rule keys are all still off) [C, `proxy_session.cpp`].
- **Instance and save.** Goldberg instance c5h-e (F:), the huge save copy `C5H-0317-HUGE_0350` of the instance's own
  `C5H-VP8P-HUGE_0350` (SHA-256 C499D3C6..., turn 350, the human is player 0, capital at 101,25); Vibrant Textures
  installed (D94). The save has **quick movement and quick combat on** (`PreGame.GetQuickMovement()`,
  `GetQuickCombat()`). The instance's four audio volumes were all 0. Never the owner's saves.
- **Arms, one launch each** (`shell/evidence/0317/run_0317.py`):

| Arm | `config.ini` keys off besides the product's list | Instance, time (EST) | Result |
|---|---|---|---|
| base (run 1) | none (the product) | c5h-e 00:40-00:47 | loads; probes (first version, no quick-off move); 2 AI turns 31 s, 27 s |
| base (run 2) | none | c5h-e 00:48-00:55 | loads; probes; 2 AI turns 35 s, 29 s |
| unit | Unit | c5h-e 00:55-00:56 | EXE dies at boot (crash A) |
| unitnomp | Unit, MovementPath | c5h-e 00:57-01:19 | loads; probes; AI turn 350 stalls; stopped Lua-free on the MC's word |
| basenomp | MovementPath (control) | c5h-c 01:23-01:31 | loads; probes; 2 AI turns 45 s, 39 s; every event as in base |
| unitnomp (repeat) | Unit, MovementPath | c5h-c 01:31-01:46 | as unitnomp; the AI turn of 350 stalls again; stopped Lua-free on the MC's word |

- **Probes per arm** (`shell/evidence/0317/probe_0317.py`, over the harness Lua channel in the InGame state):
  listeners on 40 unit-related events (counts, first and last time, the first argument lists, per unit for the
  position and move events); music and ambience volume knobs at 0 and SFX and speech at 1 for the probe window (not
  committed; put back after), every unit of the active player asleep, auto unit cycle off; the EXE's audio session
  peak meter (`tools/harness/audio.py`, read before the session's mute; nothing reached a device) through the stimulus
  window; stimuli with a count snapshot around each: create a warrior beside the capital (`InitUnit`, at 100,25),
  camera on it, select it, move it two plots (`PushMission(MISSION_MOVE_TO)`, the save's quick movement), quick
  movement off (`Network.SendGameOptions`), move it back with full moves (a real walk), damage it, fortify it, select
  it again, move mode with the cursor on a plot three away (the path display; picture), selection mode, kill it;
  quick movement back to the save's value; then two AI turns; memory after the load and after the probes
  (`tools/harness/memory.py`), launch-to-in-game wall time, the instance's `system_init.log` / `stopwatch.log`.

## 2. The boot crash with Unit off and MovementPath on [L]

Arm unit, 2026-11-15 00:55:46 launch: the EXE was gone at 00:56:17 (`wait_game` state gone). The Goldberg emulator's
crash printer (`<instance>/run/gbe/crashes.txt`; Goldberg replaces Steam's `SteamAPI_WriteMiniDump`, so no `.mdmp`
appears in the game root on a Goldberg instance) logged `05:56:14 UTC, code 0xc0000005, @address 0x01593CF7`. At the
load base 0xFF0000 of that launch this is VA 0x9A3CF7, the faulting `mov eax,[eax]` of crash A (0x9A3CE0,
`granny_world_pose.cpp`). `stopwatch.log` ends after the boot `SetActiveDLCandMods` (13.2 s, no mods) and
`system_init.log` was not written: the EXE died in the app's system startups, where MovementPath (bit 23) starts after
Unit and before City and Landmark. Same signature as the 2026-11-09 Unit-alone runs (`stubs/unit.md` 1).

So since MovementPath went back on in the product (3803, 2026-11-14), **the Unit key cannot be added alone**. The
2026-11-10 state (ii) run was B4 + Unit, and B4 has MovementPath off.

## 3. Unit events in Lua [L]

Counts per stimulus (deltas of the listener counts around the step; `LocalMachineAppUpdate` and city events left out).
base = run 2; base run 1 gave the same counts for the steps it had.

| Stimulus | base (product list) | unitnomp (Unit and MovementPath off) |
|---|---|---|
| create (InitUnit) | Created 1, FacingChanged 1, TeleportedToHex 1, Visibility 1, ShouldDimFlag 2, ActionChanged 1, InfoDirty 1 | the same |
| select | SelectionChanged 2, Visibility 2, **UnitHexHighlight 2**, DisplayMovementIndicator 2, UIPathFinderUpdate 2, InfoDirty 1 | the same without UnitHexHighlight |
| move 2 plots, quick movement on | TeleportedToHex 2, **LocalMachineUnitPositionChanged 2**, **UnitHexHighlight 4**, Visibility 6, ShouldDimFlag 2, SelectionChanged 2, ... | TeleportedToHex 2, Visibility 6, ShouldDimFlag 2, SelectionChanged 2, ...; **no position event, no highlight** |
| move 2 plots back, quick movement off (a walk) | MoveToHexes 1, **LocalMachineUnitPositionChanged 52**, **UnitHexHighlight 52**, **UnitMoveQueueChanged 1**, Visibility 2, ShouldDimFlag 3 | MoveToHexes 1, Visibility 2, ShouldDimFlag 3; **no position event, no highlight, no queue-changed** |
| ChangeDamage(20) | **SerialEventUnitSetDamage 1**, InfoDirty 1 | InfoDirty 1 only |
| fortify | nothing | nothing |
| kill | **SerialEventUnitDestroyed 1**, GameDataDirty 1, InfoDirty 1 | GameDataDirty 1, InfoDirty 1 only |

In both arms the unit was where the DLL put it after each move (`GetX/GetY`): the simulation moved it. The unitnomp
repeat on c5h-c gave the same counts as the first unitnomp run, and the control basenomp the same counts as base.

Further facts from base [L]:
- `LocalMachineUnitPositionChanged(playerID, unitID, position)`, `position` a table `{x, y, z, w}` in world units
  (for example `0, 17542, {x 2303.9, y -831.2, z 12.04, w 0}`). With quick movement on it fires twice per hex
  (`TeleportedToHex` per hex); during a walk about every frame of the walk (52 in the two-plot walk). During the two
  AI turns it fired 3,122 times for 100+ units of several players (the AI's moves are shown with the save's quick
  movement), so it is not limited to the active player's units.
- Of the 40 names, 39 exist as `Events` members; `UnitLibraryLoaded` does not.
- `SerialEventUnitCreated` has 13 arguments (player, unit, hex `{x, y, z}`, unit type, ..., two colour tables, ...),
  as the hand-over design 6.2 says.
- `UnitDebugFSM`, `UnitMemberCombatStateChanged`, `UnitMemberPositionChanged`, `RunCombatSim`, `EndCombatSim` and
  `UnitDataEdited` did not fire in base during these stimuli and two AI turns (combat is quick in this save; the
  Combat key is off in the product).

What the stopped events mean for the flags (VP and EUI `UnitFlagManager.lua`, hand-over designs 6.1) [I]: flags still
appear, move hex to hex on `TeleportedToHex` / `MoveToHexes`, dim, hide and select; they no longer glide with the
model, and their damage bar and their removal at death are not updated by an event. The AI turn stall (section 5)
prevented reading the state (ii) counts after AI turns.

## 4. Sounds [L]

The EXE's own audio session peak (before the session's mute, 0.1-s samples, `tools/harness/audio.py`) in each step's
window. The first unitnomp run lost its step times with the stall; the table uses the repeat (c5h-c), which has
them, beside the control on the same instance and base run 2 (c5h-e):

| Step | base | basenomp (control) | unitnomp (repeat) |
|---|---|---|---|
| quiet baseline (8 s, music and ambience 0) | 0.019 | 0.014 | 0.000 |
| create | 0.309 | 0.309 | 0.309 |
| select | 0.167 | 0.167 | 0.167 |
| move 2 plots, quick movement | 0.187 | 0.186 | 0.182 |
| **move back, a walk (quick movement off)** | **0.160** | **0.158** | **0.011** |
| damage / fortify | 0.025 / 0.043 | 0.000 / 0.031 | 0.009 / 0.015 |
| select again | 0.356 | 0.360 | 0.356 |
| selection mode | 0.356 | 0.356 | 0.358 |

So the selection, creation and quick-move sounds do not need the Unit system's drawing side (they play in state (ii)
at the same level), and the walk's sound does [L]: it is missing with the Unit key. That fits the timed sound
triggers of the walk clips in the animation files (`model-reader.md` 6) [I]. A louder peak (0.44-0.47) around the
path picture in base, between the step windows, was not attributed.

## 5. The AI turn stall [L]

- **Arm unitnomp**, save copy `C5H-0317-HUGE_0350` (turn 350), after the probes, `turns.py end` at about 01:03 EST.
  The human's turn ended (the end was forced after `ENDTURN_BLOCKING_CITY_RANGE_ATTACK` and `..._EVENT_CHOICE`, as in
  base) and the autosave `AutoSave_Post_0350` was written at 01:02:52. The AI turn then stopped at **"Processing Turn
  For The Aztecs"** with the camera on the human city Asyut and a target marker over it (Visual Checks
  vc-20261115-012056-workstation-0906). `turns.py wait` gave up after 84 s ("a screen holds the AI turn"); nothing
  moved for the next 15 minutes; the Lua channel never answered again.
- **Proxy counters** (`proxy/tools/proxy_snapshot.py`, `vpproxy-snapshots.tsv` and `vpproxy-snapshot-methods.tsv`,
  copies in `shell/evidence/0317/stall-unitnomp/`): in a 30.7-s window at 01:12 (922
  frames at 30 fps) the EXE called **`ICvGame1::Update` 0 times** and, in 676 of 922 frames, `ICvGame1::IsPaused`,
  `ICvGame1::CountNumHumanGameTurnActive`, `ICvPlayer1::GetUnit(int)`, `ICvUnit1::IsDelayedDeathExported`,
  `ICvUnit1::IsBusy`, `ICvPlayer2::IsOptionKey` once each. The frame loop and the server's renderer ran on
  (render.log). Over the whole run before it, `ICvGame1::Update` was called 954 times and `ICvUnit1::IsBusy` 25,584
  times. So **the EXE holds the game update while one unit reports busy, and that unit never stops being busy.**
  *Note by the Architect, 2026-11-15 02:15 EST: the bold sentence is an inference, and the counters do not
  support it. The same file shows the three unit reads at 0.82 per frame over the whole run before the stall
  (`IsBusy` 25,584 in 31,112 frames) against 0.73 per frame in the stall, all crossing to the server (answered
  locally: 0), and `proxy-design.md` 16 lists them among the calls of every idle frame. The poll is the EXE's
  normal per-frame read, not a sign of a wait on a busy unit. What is abnormal is only that the game-core thread
  makes no call. Where it waits is in the dumps (the increment 3 sheet, section 4.2 and item N12).*
- **What `IsBusy` is in the DLL** [C, pin 04b1211b8]: `CvUnit::IsBusy()` = `GetMissionTimer() > 0 || isInCombat()`
  (`CvUnit.cpp` 30060). The mission timer is counted down only in `CvUnitMission::UpdateMission`, which runs inside
  `CvGame::update`, and is non-zero only for human units (`CalculateMissionTimer` returns 0 for AI units). Combat
  state is cleared inside `ResolveCombat`, which the DLL calls at once also for a visualised combat
  (`CvUnitCombat.cpp` 3714-3770). So with the EXE no longer calling `Update`, a human unit's mission timer can never
  reach 0: a deadlock if the EXE waits for it [I]. **What makes the EXE resume `Update` in stock** is known since
  issue 0335 (section 9, `research/02-exe-internals/game-core-thread.md`) [C]: the main thread's frame requests each
  `Update`, and an update hold counter, raised by an AI leader message and dropped when the leader screen is left,
  stops them all; the EXE does not wait on a unit. *Corrected 2026-11-15; it read: "not known: the inferred
  candidate is the Unit system's per-frame update ... finishing the unit's visualisation" [I].* Which unit the EXE
  polls was not read (the channel was dead).
- **Evidence kept**: non-invasive full dumps (`cli.py dump`, cdb `-pv`) of the EXE
  (`c5h-e-x86-43908-20261115-011444.dmp`, 1,781,745,137 B, SHA-256 5199bb79...) and of `server.exe`
  (`c5h-e-x64-48724-20261115-011517.dmp`, 4,189,127,345 B, SHA-256 5a72438e...) in
  `F:/c5-64bit-work/0317/dumps/unitnomp-stall/` (D141; never committed, D83). The unit the EXE polls and its DLL state
  can be read from them. EXE memory at the stall: private 1,371.3 MB, largest free 1,927.4 MB.
- **Reproduces: yes** [L]. Repeat on c5h-c (01:31-01:46, the same save copy, stimuli and build): the same stop at
  "Processing Turn For The Aztecs" after the human's turn ended (`turns.py wait` rc 6 after 86 s); proxy counters
  (`shell/evidence/0317/stall-unitnomp-rerun/`, 30-s window at 01:44, 909 frames): `ICvGame1::Update` 0,
  `ICvPlayer1::GetUnit` / `ICvUnit1::IsDelayedDeathExported` / `ICvUnit1::IsBusy` 665 each. EXE dump
  `F:/c5-64bit-work/0317/dumps/unitnomp-stall-rerun/c5h-c-x86-19056-20261115-014359.dmp` (SHA-256 ea1d4eee...).
  Stopped with `tools/harness/stop_nolua.py` (rc 0) after the newest save had been still since 01:37:44.
- **No unit reports busy to Lua during the stall** [L]. A watcher armed before the turn
  (`shell/evidence/0317/busywatch_0317.lua`, a `LocalMachineAppUpdate` listener in InGame) scanned every unit of every
  player for `IsBusy()` every 150 frames and printed a line when the count changed. It printed `busy_units=0` once
  (01:35) and nothing after; the 30-s stall window shows 10,968 `rt:LuaCall` calls, about the watcher's six scans, so
  it kept running and kept finding 0; the EXE dump holds no later watcher line (searched for its marker). So the unit
  the EXE polls is either not in any player's unit list (for example one in delayed death, which fits the
  `IsDelayedDeathExported` call before each `IsBusy`) or not busy, and the EXE waits on its own state [I].
  Owner, type and mission of the polled unit were therefore not identified.
- **Control** (basenomp, MovementPath off, Unit on): the same AI turn passed in 45 s and the next in 39 s, so the
  stall is not MovementPath's.

## 6. Control arm basenomp (MovementPath off, Unit on) [L]

c5h-c, 01:23-01:31. Every event count of section 3 as in base (`LocalMachineUnitPositionChanged` 2 and 52,
`UnitHexHighlight`, `UnitMoveQueueChanged`, `SerialEventUnitSetDamage`, `SerialEventUnitDestroyed` all present); the
path display shows no discs or chevrons (MovementPath off); both AI turns passed. So every difference of unitnomp
against base in sections 3-5 belongs to the Unit key, apart from the path display, which is MovementPath's.

## 7. Memory and load time [L, indication]

| | base run 1 (c5h-e) | base run 2 (c5h-e) | unitnomp (c5h-e) | basenomp (c5h-c) | unitnomp repeat (c5h-c) |
|---|---|---|---|---|---|
| launch to in game (wall) | 186 s | 170 s | 172 s | 202 s | 205 s |
| `LoadGameState_TOTAL` | 50,586 ms | 45,390 ms | 49,080 ms | 54,245 ms | 47,110 ms |
| `Unit_System::Startup` (boot) | 7,336 ms | 6,691 ms | absent | 7,195 ms | absent |
| `MovementPathSystem::Startup` (boot) | 96 ms | 57 ms | absent | absent | absent |
| `LoadUnitLibraries` at mod activation | 7.04 s | 6.08 s | 8.92 s | 10.81 s | 9.19 s |
| EXE private / committed / largest free after the load (MB) | 1,398.6 / 1,730.2 / 1,917.3 | 1,392.7 / 1,725.1 / 1,892.2 | 1,346.7 / 1,679.5 / 1,943.3 | 1,393.8 / 1,723.9 / 1,895.5 | 1,341.4 / 1,671.7 / 1,959.2 |
| EXE private / largest free after the probes (MB) | 1,506.5 / 1,804.8 | 1,507.3 / 1,783.9 | 1,371.3 / 1,927.4 (at the stall) | 1,521.7 / 1,784.1 | 1,365.2 / 1,943.4 |
| server.exe private after the load (MB) | 1,531.3 | 1,490.3 | 1,445.4 | 1,467.1 | 1,489.2 |

One launch per arm on a workstation running six other games: load times are indications only (COMMON 23).
The boot startups of Unit (6.7-7.3 s) and MovementPath (0.06-0.1 s) are gone; the gain is eaten by noise in the
launch-to-in-game time, and the mod-activation restart (`ReloadUnitSystem=1` in the CP, VP and EUI modinfos) still
loads the unit libraries. On one instance, Unit off against on (both MovementPath off): EXE private -52 MB after the load and -157 MB after
the probes; the gap after the probes is the unit visuals the EXE builds once units are seen and moved.

## 8. Not verified

- The polled unit; whether the stall also happens on other saves, on other turns or with quick combat off. (The
  EXE-side mechanism was read from the dumps by 0335, section 9; why the leader screen does not come up with the
  Unit key is still open there.)
- Unit events during AI turns in state (ii) (the stall prevented the read).
- Which sound each peak is (only levels per step were measured).
- Frame rate; more than one launch per arm.

## 9. Where the game-core thread waits: the three dumps read (issue 0335, 2026-11-15)

Implementer impl-B-0335 (account B), branch `task-0335-stall-dump-read`; no live game. The dumps of section 5 read
with cdb 10.0.26100 (x86 for the EXE, x64 for `server.exe`, `-z <dump> -cf <script>`, the server's and the proxy's
PDBs of `F:/c5-64bit-work/0317/runs/b`), the EXE file of c5h-e for call and reference scans. The general rule
(thread object, request, hold) is in `research/02-exe-internals/game-core-thread.md`; here what the stall shows.
Behaviour only, no disassembly committed (D83); the dumps stay on F: (D141).

**The stack** [C, both EXE dumps]. The game-core thread (Win32 start `CivilizationV_DX11.exe+0x42A360` = VA
0x82A360; thread 77 of 79 in the c5h-e dump, 0xC00C; the same thread in the c5h-c dump) is in `NtSuspendThread`
called by `kernel32!SuspendThread` from its own loop (return address VA 0x82A3F4), with its own handle (the
c5h-e dump: handle 0x1E00, `!handle`: thread 0xC00C). Its object (pointer at 0x2DD202C): running 1, suspend
requested 1, update pending (`+0x40`) **0**. That is the thread's normal place between two updates: after each
`Update` it requests its own suspend and the main thread resumes it for the next one. Its total user time in the
c5h-e process was 2.5 s; the main thread's 5 min 37 s.

**The object it waits on** [C]: the EXE's **update hold counter at 0x2DE044C = 1** in both EXE dumps. While it is
non-zero the step skips `ICvGame1::Update` (and so does the synchronous path), so even a resumed thread makes no
call; this is the whole of "the EXE stopped calling `Update`" (section 5) and why the Lua channel, which runs on that
thread inside `Update`, was dead.

**Who raised it and which system was expected to signal it**:
- The only increment is the EXE's handler of `ICvEngineUtility1::GameplayDiplomacyAILeaderMessage` on its
  "show the leader screen" branch [C]. The proxy counters show **one** `GameplayDiplomacyAILeaderMessage` from the
  server to the EXE in each stalled run (`shell/evidence/0317/stall-unitnomp/vpproxy-snapshot-methods.tsv` and
  `.../stall-unitnomp-rerun/`, snapshot 1; also one `ICvUserInterface1::isDiploActive`) [L]. The diplomacy
  manager's current leader (`[[0xCC8E74]+0xD0]+0x1E4`) is **player 1** in both dumps [C], and the stall screen says
  "Processing Turn For The Aztecs".
- The release is expected from the **leader screen**: the decrement is in the leader-head scene class and runs when
  the leader view is left [C], which the harness's diplomacy answerer does through `LeaderHeadRoot` (`OnClose()` ->
  `UI.RequestLeaveLeader`, `run-and-drive-the-game.md` 5) [I for the path to the decrement].
- **In the passing arms the screen came up; in the stalled arms it never did** [L]. `Lua.log` of base
  (`F:/c5-64bit-work/0317/runs/w/out-base/`, also `out-base-r0`) and of the control basenomp (`.../runs/wc/out-basenomp/`)
  each carry, on turn 350, `LeaderHeadRoot: c5h-diplo: seen n=1 turn=350 leader=LEADER_MONTEZUMA
  state=DIPLO_UI_STATE_TRADE_AI_MAKES_OFFER` and the answerer's `done ... actions=trade` two seconds later, then a
  Bismarck screen. Both unitnomp `Lua.log`s (`runs/w/out-unitnomp/`, `runs/wc/out-unitnomp/`) have **no**
  `c5h-diplo` line: the EXE never raised `AILeaderMessage` to Lua, so no screen, no answer, no leave, and the hold
  stayed at 1.
- **No leader scene was loaded** [C]: the leader-head class's scene pointer (0xDA8F8C) is 0 in both EXE dumps (its
  per-frame method has the debug text "THERE IS NO SCENE LOADED FOR THIS LEADER!!" for that case). The word at the
  class's +0x718, whose bit 9 that method tests as "leaderheads enabled", is 0xFF2C0627 in the c5h-e dump: bit 9
  (LeaderHead) set, bit 6 (Unit) clear [C]; that it is a copy of the key mask is [I].
- **Why the leader screen does not come up with the Unit key is not read** [I]. The leader scene is an animated
  Granny model; the Unit system's boot startup builds the Granny pose pool that MovementPath also needs (crash A,
  section 2), so the leader scene's load most likely fails or never completes without it. (Corrected 2026-11-15 from 0355,
  `research/02-exe-internals/leader-scene-player-limit.md` 3: the EXE does not wait for the scene before raising
  `AILeaderMessage`; a player with no cached scene still gets the screen. Replaced: "and the EXE raises `AILeaderMessage`
  only once the scene is ready" [I].) Not verified: where the EXE raises the event was not found (its
  name strings are referenced only from registration tables).

**The server side** [C, `c5h-e-x64-48724-20261115-011517.dmp`]: no thread of `server.exe` has a DLL frame; the
service threads wait in `vpipc::ChannelImpl::next_message`, the renderer in its loop, the main thread in
`RemoteMain`. The DLL is idle between two calls: its last `Update` returned and it is not waiting on anything.

**Does the stall need the product path?** Not known for certain; the dump read says most likely not [I]. Every link
of the chain is inside the EXE (the leader-message handler, the hold, the leader-head scene, the game-core thread);
the proxy's part is to deliver `GameplayDiplomacyAILeaderMessage`, and it did (one call, in both runs) [L]. With the
DLL in process the same call reaches the same handler, so stock with Unit=1 + MovementPath=1 should stall the same
way at the first AI leader message to the human, if the leader scene fails to load there too. How known: by reading,
not by a run. The 2026-11-10 in-process run with B4 + Unit that passed three AI turns (`world-systems-and-stubs.md`
6) has no record of a leader screen, so it neither confirms nor contradicts this. A live check (route C of the
sheet, or an in-process arm with the same keys on a save whose next AI turn sends the human a leader message, such as
`C5H-0317-HUGE_0350`) would settle it.

**For 0328 (true absence)** [I]: any state that takes the Unit system away must keep AI leader messages working: the
leader screen must come up and close, or the hold must not be raised. The poll of `GetUnit` / `IsBusy` needs
nothing (section 5, the Architect's note).

## Sources

`shell/evidence/0317/` (drivers and scripts); run outputs `F:/c5-64bit-work/0317/runs/w/out-*` and pictures under
`F:/c5-64bit-work/0317/runs/pics*` (game data, not committed; kept on F: for epic 0327, D141; the builds used in runs/b and runs/shell-build-bin);
Visual Checks entries of issue 0317; `research/sources/shell-gates/stubs/unit.md`;
`research/02-exe-internals/world-systems-and-stubs.md` 6; `architect-2026-11-14-hand-over-designs.md` 6;
the pinned DLL source (fork 04b1211b8) `CvUnit.cpp`, `CvUnitMission.cpp`, `CvUnitCombat.cpp`.
