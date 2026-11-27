# Quick Combat and Quick Movement after observer mode and a seat change (ticket 0716)

The owner's beta 3 report (2026-11-23 20:3x; build 0.1b3-std-only, x64-1b87dd18-pd56fbd9, main d56fbd94f, the owner's
Steam game, route S combat): after entering observer mode, leaving it and taking another player's seat, toggling Quick
Combat / Quick Movement in the options no longer switches the new player's units between quick and animated. The
owner's `[Performance]` lines: AiDrainUnordered = 0, AiSkipVisualDrain = 0, AiUpdateTickMs = 0, GameEventsOneCrossing =
0, HiddenEmbarks = 1. This file holds how the options travel, the stock arm and the patch arm.

## 1. Where the two options live (DLL read at the x64 pin 1b87dd185, 2026-11-23) [C]

- **Quick Combat** is `CvPreGame::quickCombat()` = the static `s_quickCombat` (`CvPreGame.cpp` 1687). Written by
  `setQuickCombat` (also writes the option `GAMEOPTION_QUICK_COMBAT`, line 3229), by any `SetGameOption` through
  `SyncGameOptionsWithEnumList` (line 896-918: `s_quickCombat = value > 0` for that enum), by `CvGame` init when the
  game option is on (`CvGame.cpp` 426), and by the pre-game load (line 1858). Every attack path asks it
  (`CvUnitCombat.cpp` 3658: `!quickCombat` = `lookAt`, `setVisualizeCombat(isActiveVisible)`, `GameplayUnitCombat`).
- **Quick Movement** is `CvPreGame::quickMovement()` = the pre-game option `GAMEOPTION_QUICK_MOVEMENT` read by its
  key string (line 1697-1703); `setQuickMovement` writes that key. `Game.IsOption(GAMEOPTION_QUICK_MOVEMENT)` is not
  the same storage (the enum list has no such entry; it read false while the key was on, `unit-combat-server.md`
  W24 #8726). Every animated-move choice in `CvUnit.cpp` asks `quickMovement()` (with `MOD_UI_QUICK_ANIMATIONS`).
- **Both are global**: there is no per-player copy. A seat change (`CvGame::setAIAutoPlay` -> `setActivePlayer(...,
  bAutoplaySwitch = true)`, `CvGame.cpp` 5445-5580, 6302-6370; how the switch runs: `../05-graphics-composition/
  observer-switch-minimap.md` section 2) changes slot status, claim and the active player, calls
  `sendPlayerOptions(true)` (the 9 player options, which do not include these two) and touches neither option.
- **The options menu** (`Assets/DLC/UI_bc1/Options/OptionsMenu.lua`, the in-game branch, single player too): on show it
  caches `PreGame.GetQuickCombat()` / `GetQuickMovement()`; the check boxes set the cache (single player, not hot seat);
  OK sends each option whose cached value differs from `PreGame` in one `Network.SendGameOptions({{name, bool}, ...})`.
  The EXE's Lua binding of `Network.SendGameOptions` is 0x5C44C0 on build 4390913 [C, Ghidra 2026-11-23]: it reads the
  table into (name, value) pairs and hands them to a network object's virtual +0x4C (a message post); the receiving side,
  which writes the DLL's pre-game options, was not read.
- **On the product path** the `ICvPreGame` calls cross to the server's DLL through the proxy; the proxy keeps no local
  copy of `quickCombat` / `quickMovement` / `GetGameOption` (none is in `proxy/model/pure-methods.tsv`; `ICvGame1::IsOption`
  is mirrored by epoch, and every setter crossing is impure, so a set drops the mirror) [C, this repository at 8994ede07].
- **Route S** refuses a fight (`CombatCode::QuickCombat`, `render/src/unitanim/combat.cpp` checkRecord) when the feed's
  combat flag says Quick Combat; the flag is `ICvGame1::IsOption(GAMEOPTION_QUICK_COMBAT)` at the combat
  (`server/src/remote/srv_remote_feed.cpp` CombatGameFlags), which `SyncGameOptionsWithEnumList` keeps equal to
  `s_quickCombat` after every `SetGameOption`; and with `s_quickCombat` on the DLL sends no `GameplayUnitCombat` at all.
  Moves follow the DLL's own `GameplayUnitMoved` / teleport choice; the server has no quick-movement switch of its own.

## 2. Stock arm: the options follow the seat change [L, 2026-11-23 21:33-21:43 EST]

Set-up: c5h-new-a (Goldberg, EXE 26577625), no proxy, the instance's test DLL `ff0f6e86...` (`+ia4968`: the pin's
32-bit VP 5.4.8 DLL with the Lua channel; VP's own released DLL has no channel, runbook 3.7 trap 0679), cp + vp + eui,
the save `C5H-0716-T0` (0683's new game, Small Continents, quick speed, turn 0, human Carthage = player 0; QC and QM on
in the save). Hidden, dialog watcher on, safe stop, saves restored by hash, `verify --quick` ok. State reads in
`server/evidence/0716/stock/`.

| step | what | PreGame QC / QM after (Game.IsOption QC) | effect |
|---|---|---|---|
| s0 | loaded, player 0 | true / true (true) | |
| s1 | SendGameOptions off/off as player 0 | false / false (false) | |
| s2 | observer on (DoControl, T0): active player 5, team 21 | false / false | AI turns slow (animated) |
| s3 | SendGameOptions on/on while observer | true / true (true) | turns sped up |
| s4 | observer off at T12: seated as India, player 3 (slot 0 now SS_COMPUTER) | true / true | |
| s5 | SendGameOptions off/off as player 3 | false / false (false) | a move of an Indian unit (20,26 -> 20,27): 1 walk event, 0 teleports |
| s6 | SendGameOptions QC on alone, then on/on | true / false, then true / true | the next move (23,25 -> 24,26): 0 walks, 1 teleport |

So on stock the options menu's commit path reaches the DLL in every seat, before, during and after observer mode,
and the DLL's move choice follows Quick Movement for the new player's units. Walk / teleport are the EXE's Lua events
`SerialEventUnitMoveToHexes` / `SerialEventUnitTeleportedToHex` for that unit (`mt_move.lua`). Not covered by this arm:
the options screen itself driven by mouse (the same Lua file on both arms, so it cannot differ between them), and a
combat after the switch (Quick Combat's read-back is the value every attack path reads, section 1).
`Network.SendGameOptions` acts a frame later: `PreGame` still reads the old values inside the same handler.

## 3. Patch arm: the options follow the seat change with the beta 3 build too [L, 2026-11-23 23:09-23:30 EST]

Set-up: c5h-new-b (Goldberg, EXE 26577625), the player DLL VP 5.4.8's own (262aee88...), then the beta 3 release
installer `VP64-Patch-0.1b3-std-only-for-VP5.4.8.exe` (6da3170e...) laid by `run_installer.py` (installer defaults:
ServerRenderer, ServerUnits, ServerCities, ServerLandmarks, ServerFarmFields, ServerCombat = 1, ServerArc = ServerPath
= 0), then the owner's `[Performance]` lines (AiDrainUnordered, AiSkipVisualDrain, AiUpdateTickMs, GameEventsOneCrossing
= 0, HiddenEmbarks = 1; `set_opts.py`). Same save and steps as the stock arm; removal by the installer, tree diff,
`set-dll --previous`, `verify --quick`. State reads in `server/evidence/0716/patch/`; the run's logs (render.log,
server.log, vpproxy.log) kept in the work folder, not committed.

| step | what | PreGame QC / QM after | effect (the new player's units) |
|---|---|---|---|
| s0-s1 | loaded as Carthage (player 0); SendGameOptions off/off | false / false | a move: 1 walk, 0 teleports |
| s2-s3 | observer on at T0 (player 5); SendGameOptions on/on while observer | true / true | |
| s4 | observer off at T15: seated as Venice, player 2 | true / true | a move: (not read, see below); an attack on a placed barbarian warrior: RunCombatSim 0, EndCombatSim 0 |
| s5 | SendGameOptions off/off as player 2 | false / false | a move (44,29 -> 45,30): 1 walk, 0 teleports; an attack (44,23 -> 45,24): RunCombatSim 1, EndCombatSim 1, and the server played it (render.log `units trigger 1s: fights=1 ss=50 es=7 ...`, its only fight of the run) |
| s6 | SendGameOptions on/on as player 2 | true / true | a move (45,30 -> 45,31): 0 walks, 1 teleport |

(The s4 move's line was cut by the driver's output filter; the s5/s6 pair answers the same question both ways.)

Restore trap [L]: an options file edited after the install (here by `set_opts.py`) is left by the installer's removal,
as designed (`../06-tooling-and-workflows/release-builds.md`, the options row: "a file changed since is left, with a
note naming the kept backup"), together with `VP64/install/backup/<sha>` and `VP64/install/record.txt`. The tree diff
then shows `VP64Options.ini` changed and `VP64\` added. Put the original back from that backup by its SHA-256 (the
record's `saved` line names it), remove `VP64\`, list again: My Games then equals the listing before outside the run
outputs, apart from the DLL that `set-dll --previous` puts back.
So on the product path too the options menu's commit reaches the server's DLL in the new seat, and the new player's
moves and route S fights switch between quick and animated with them [L]. The patch does not reproduce the report
with this path; section 4 lists what was not covered.

## 4. Verdict and what is open

- **Whose bug:** neither arm reproduces it. The DLL holds both options globally (section 1); after observer mode and a
  seat change, `Network.SendGameOptions` as the options menu sends it changes both, and the units of the new seat
  follow, on stock and with beta 3 (sections 2, 3) [L]. No stock bug to name, and no patch cause found.
- **Not covered, in order of likelihood [I]:**
  1. The options screen driven by the mouse. Its Lua caches `PreGame.GetQuickCombat()` / `GetQuickMovement()` only
     when the screen is shown and sends only the options whose check box differs from `PreGame`; this is the same file
     on both arms and every value it reads crossed correctly above, so a difference by the patch is unlikely, but a
     click-through was not made (the human-input runner, runbook 3.10, would do it).
  2. What the owner saw [reworded for privacy] as missing animation. With Quick Movement off the DLL still teleports a move the active team does
     not see (`isActiveVisible`), and with Quick Combat off a fight on a plot the active team does not see is not
     visualised (`setVisualizeCombat(isActiveVisible)`); right after a seat change the new team's view is the AI's.
     Units seen only through the observer's all-plots view are not visible to the new seat.
  3. A different switch path (CONTROL_TOGGLE_AI_TAKEOVER, VP's exit button while an observer UI override player is set),
     or a save whose pre-game options differ. The owner's save or the exact clicks would settle it.
- **What would confirm a patch cause:** the same click path on the owner's save showing `PreGame.GetQuickCombat()` /
  `GetQuickMovement()` changed while the new seat's visible moves stay teleports (or the reverse) only with the patch.

## 5. Commands

Scripts in `server/evidence/0716/` (driver `drv_0716.sh INSTANCE put|launch|state|obs|send QC QM|move|attack|end|stop`;
the patch arm's layout `drv_0716_patch.sh INSTANCE install|remove`, a copy of 0683's with the beta 3 release installer and
`set_opts.py` for the owner's options; `mt_move.lua` and `mt_attack.lua` count the EXE's walk / teleport and
RunCombatSim / EndCombatSim events for the active player's unit):
`q_state.lua` (read only, InGame state: PreGame and Game values of both options, the seat, every major's slot),
`mt_sendopts.lua` (OptionsMenu.lua's commit on the main thread). The observer toggle is 0683's
`server/evidence/0683/mt_observer.lua`. The save is 0683's new game `C5H-0683-T0` (d6cf96cc...), put in as
`C5H-0716-T0` and taken out by hash (`saves_snap.py`).
