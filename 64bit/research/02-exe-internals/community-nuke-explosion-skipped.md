# Community report: nuke explosion skipped after Quick Combat / Quick Movement are switched off in game (ticket 0553)

Community report, unconfirmed (the owner, 2026-11-18, decision D187 [reworded for privacy]: investigate these with caution, as the report is unconfirmed). Report:
after Quick Movement and Quick Combat are switched from on to off in the in-game options during a game, an atomic
bomb's bomber or a nuclear missile flies to the target but the explosion animation is skipped.

Timebox: about 1 h of reproduction on Goldberg instances, stock route first, then the product route of the current
pins (batch 4). Work 2026-11-18 from 14:32 EST, workstation, pool instance `c5h-b` (profile mods548, mods cp, vp, eui).

## 1. What the code says (before any run)

- **Where the switch lands [C, DLL source at the 32-bit pin `i0-ship-batch4` e96457fd5].** The in-game options
  screen (`Assets/DLC/UI_bc1/Options/OptionsMenu.lua` lines 138-146 of the instance's install) sends only the changed
  options with `Network.SendGameOptions({{"GAMEOPTION_QUICK_COMBAT", b}, {"GAMEOPTION_QUICK_MOVEMENT", b}})` on OK.
  In the DLL the game option `GAMEOPTION_QUICK_COMBAT` is mirrored into `CvPreGame::s_quickCombat`
  (`CvPreGame.cpp` `SyncGameOptionsWithEnumList`, around line 912; `setQuickCombat` line 3229), which every attack
  path reads at the moment of the attack (`CvUnitCombat::AttackNuclear`, `CvUnitCombat.cpp` line 4170:
  `bDoImmediate = CvPreGame::quickCombat()`). So the DLL holds no stale copy of the setting from the game's start.
- **The nuclear attack's two branches [C].** With Quick Combat on, `AttackNuclear` resolves at once: no
  `GameplayUnitCombat` call, so the EXE plays no combat simulation (no flight, no explosion; the damage arrives
  directly). With it off and the target visible to the active player (or the attacker or a victim on the active
  team), the DLL moves the camera to the target (`lookAt`), marks the combat visualised and calls
  `GameplayUnitCombat`, which posts `RunCombatSim` / `EndCombatSim` (combat-choreography.md 1 and 3.3; a nuke's
  record is a barrier, flags +2). The flight and the detonation are both played by the EXE's combat simulation
  from the unit member's combat weapon rows.
- **The weapon data [C, the instance's `Civ5DebugDatabase.db`, VP 5.4.8].** `UNIT_ATOMIC_BOMB` (range 6, nuke level 1)
  uses member `ART_DEF_UNIT_MEMBER_ATOMICBOMB`; its weapon 1 is the bomb: `IsDropped` 1, `TargetGround` 1,
  `WaitForEffectCompletion` 1, `AreaDamageDelay` 0.5, `ProjectileSpeed` 0.8, no `HitEffect` row (the effect comes
  with the dropped projectile). `UNIT_NUCLEAR_MISSILE` (range 24, nuke level 2), member
  `ART_DEF_UNIT_MEMBER_NUCLEARMISSILE`: `HitEffect` `ART_DEF_VEFFECT_NUCLEAR_BOMB_01`, `WaitForEffectCompletion` 1,
  `AreaDamageDelay` 0.5.
- **Ticket 0477 (D176) [C].** Its DLL change gives the engine a copy of the combat info without retaliation only when
  the defender is a submarine-class unit (combat-choreography.md 13). A nuclear strike on a city or a land plot does
  not meet that criterion, so 0477 is not involved by the code; the runs below confirm it live (section 3).

## 2. Set-up

- Instance `c5h-b` (C:, short-run place released by 0398), the Steam DX11 EXE of the instance, Goldberg wrapper route,
  frame cap on, dialog watcher on (`hold`). **Stock route:** the 32-bit pin `i0ship-e96457fd-ia` (DLL `3EAA3290...`,
  batch 4, carries 0477's D176 fix) put in with `provision.py set-dll`, no proxy layout. Mods cp v151, vp, eui.
- Save: a copy `C5H-0553-TINY_0008` of `REL548-TINY_0008` (SHA-256 `b2ceb663...`, Polynesia, Tiny, turn 9 at load).
  **The save loads with Quick Combat and Quick Movement ON** (`PreGame.GetQuickCombat()` true,
  `GAMEOPTION_QUICK_COMBAT` true, `PreGame.GetQuickMovement()` true) [L], which is the report's starting state.
- Scene (`tools/harness/evidence/0553/setup.lua`): the area round the human capital (51,14) revealed and visible,
  barbarian target cities founded with `InitCity` at distance 4-5 (47,13; 48,17; 52,18; later 46,14; 48,9; 48,18).
  Each strike makes a fresh unit in the capital and sends `MISSION_NUKE` through
  `Game.SelectionListGameNetMessage` (`nuke.lua`), the message the interface sends. The switch is
  `Network.SendGameOptions` with the same table `OptionsMenu.lua`'s OK sends in game (section 1); the options screen
  itself was not clicked.
- Measured per strike (`n.py strike`): the Lua event time line (0456's logger plus `GameEvents.NuclearDetonation`),
  the target city's population and damage before and after, 24-32 window shots 0.5-0.6 s apart from the order
  (`shot.shot`, PrintWindow, scale 0.5; all in Visual Checks under issue 0553 as DIAGNOSTIC; D114: popups closed
  through Lua before every strike except a2, where a city-state popup covered the map centre). Contact sheets:
  `C:/c5-64bit-work/0553/sheet-*.png` (not committed). The strike records are in
  `tools/harness/evidence/0553/strikes/`.

## 3. Runs

### 3.1 Stock route, loaded ON, switched OFF in game (2026-11-18 14:47-14:57 EST) [L]

| Strike | Unit | Options at the strike | Target | RunCombatSim -> EndCombatSim | Flight | Explosion | City |
|---|---|---|---|---|---|---|---|
| a1 | Atomic Bomb | ON (as loaded) | 47,13 | no combat sim (immediate) | none | none (expected with Quick Combat on) | damage 0 -> 129 |
| a2 | Atomic Bomb | switched OFF | 48,17 | 20.07 s | yes | **played** (dome and cloud at +10-12 s; popup over the centre) | 0 -> 129 |
| a3 | Nuclear Missile | switched OFF | 52,18 | 14.01 s | yes | **played** (flash +3.9 s, dome, mushroom, fallout) | destroyed |
| a4 | Atomic Bomb | switched OFF | 47,13 | 20.07 s | yes | **played** (flash +4.2 s, dome, cloud to +12 s) | 129 -> 193 |
| a5 | Nuclear Missile | OFF, ON, OFF again | 48,17 | 14.07 s | yes | **played** | destroyed |
| a6 | Atomic Bomb | Quick Combat OFF, Quick Movement back ON | 48,9 | 20.04 s | yes | **played** (flash +4.1 s) | 0 -> 129 |

In every strike `GameEvents.NuclearDetonation` fired at the order, the human's turn stayed active, and no box
appeared. The atomic bomb's combat simulation always ran to the EXE's 20 s limit for a combat with an air unit
(combat-choreography.md 1 item 4) while the missile's ended by itself after about 14 s; the picture was finished
(fallout drawn, the bomber gone) several seconds before the limit, so the player sees a still map for about 6-8 s
before `EndCombatSim`. Whether the bomb's 20 s is the same on a game loaded with Quick Combat off is the control
(3.2). After the strikes turn 9 was ended (`turns.py end`, forced, then `wait`): turn 10 came normally, no box.

### 3.2 Control: stock route, loaded with both options OFF (2026-11-18 15:04-15:10 EST) [L]

The game of 3.1 was saved at turn 10 with both options off (`UI.SaveGame("C5H-0553-T10-QCOFF")`), stopped with the
harness's stop and loaded in a new EXE process: `PreGame.GetQuickCombat()` false, `GetQuickMovement()` false at load.

| Strike | Unit | Target | RunCombatSim -> EndCombatSim | Flight | Explosion | City |
|---|---|---|---|---|---|---|
| c1 | Atomic Bomb | 46,14 | 20.07 s | yes | played (flash +4.4 s, dome, cloud) | 0 -> 129 |
| c2 | Nuclear Missile | 48,18 | 14.83 s | yes | played (flash +4.4 s, dome, mushroom) | destroyed (and 48,16 in the blast) |

So the control is the same as the switched case: the same flight, the same explosion, the same 20.07 s for the
bomb and about 14 s for the missile. **The bomb's run to the 20 s limit does not depend on the switch** [L]. It is
a separate observation (section 4.2).

**A capture trap, not a skipped explosion [L for the frames, I for the cause: the dome is in the frames 0.6 s
before and after each such frame].** In c1 several frames inside the explosion (+6.3, +7.6, +8.8,
+11.0, +11.5 s) show the map with no effect at all, between frames that show the dome. They are PrintWindow
frames without the effect layer (E9's picture-round list: PrintWindow artifacts), not frames the player saw; a
single shot of a strike is no evidence that its explosion was skipped. Judge from a series.

### 3.3 Product route, loaded ON, switched OFF in game (2026-11-18 15:11-15:21 EST) [L]

The proxy layout of the batch-4 pins `x64-c29442e8-p58724e0` put on c5h-b with `provision.py set-proxy` (proxy
`3DFD7521...`, server.exe `FC73E60E...`, x64 DLL from c29442e8c; both logs' build lines `repo 58724e072 dirty=0
dll_pin c29442e8c`), `vpproxy.ini` mode=on as `provision.py` writes it, the instance's `VP64Options.ini` unchanged
(ServerRenderer at its default: the EXE draws the world and, since D147, the combat), 32-bit fallback the pin
`i0ship-e96457fd-ia`. Same save `C5H-0553-TINY_0008`, loaded ON, the scene rebuilt, then switched OFF.

| Strike | Unit | Options at the strike | Target | RunCombatSim -> EndCombatSim | Explosion | City |
|---|---|---|---|---|---|---|
| p1 | Atomic Bomb | switched OFF | 47,13 | 20.07 s | played (flash +4.0 s, dome, cloud) | 0 -> 129 |
| p2 | Nuclear Missile | switched OFF | 52,18 | 14.03 s | played (flash +3.6 s, mushroom) | destroyed |
| p3 | Atomic Bomb | OFF, ON, OFF again | 48,17 | 20.07 s | played (flash +5.2 s) | 0 -> 129 |

Turn 9 then ended normally (turn 10, no box); the server ran through it. The product route behaves as the stock
route in every column.

## 4. Result

### 4.1 Could not reproduce [L]

**Not reproduced in 13 strikes (2026-11-18 14:47-15:21 EST, about 35 min of attempts):** 8 strikes with both options
switched from on to off in game (3 Atomic Bomb and 2 Nuclear Missile on the stock route, 2 Atomic Bomb and 1 Nuclear
Missile on the product route, two of them after a second on/off toggle), 1 with only Quick Combat switched off,
2 controls loaded with both off, and 1 check with both on (no animation at all, as the code says). Every strike with
Quick Combat off played the flight and the full detonation (flash, dome, mushroom or cloud, fallout), applied its
damage, raised `GameEvents.NuclearDetonation` and let the turn end normally. The code agrees (section 1): the DLL
reads the live setting at each attack and keeps no copy from the game's start, and the EXE plays the detonation
from the weapon data whichever way the game was loaded. Ticket 0477 is not involved (section 1; its criterion is a
submarine defender, and the strikes behave the same on the stock DLL pin that carries it as the code predicts).

Not tried in the timebox, each a possible condition of the original report [I]: the options screen clicked by mouse
(the Lua call is the same message its OK sends); a target outside the camera's view or under fog (the DLL moves the
camera with `lookAt` when the active team is involved); an AI's nuke seen in the AI turn (a barrier record,
combat-choreography.md 3.3); a late-game save with many units in the blast; the server's world renderer (`ServerRenderer = 1`, not the
default); a nuke into a city with nuclear interception. The last one has a designed no-explosion path [C, DLL source
e96457fd5]: `GenerateNuclearCombatInfo` (`CvUnitCombat.cpp` 2261-2290) rolls the target's
`GetNukeInterceptor` city's `getNukeInterceptionChance()`. A successful roll **kills an Atomic Bomb outright** (the
attack becomes a bombing mission with nuclear level 0 and `AttackNuclear` takes its intercepted branch, lines
4176-4196, which calls no `GameplayUnitCombat`: no simulation, so by the code no flight either [I, not run]) and
**turns a Nuclear Missile into an atomic-bomb-sized blast** (level 1: a smaller explosion). A player who misses the
interception notification (`TXT_KEY_NUCLEAR_MISSILE_INTERCEPTED`) may read either as a skipped explosion; this is
the designed behaviour of the DLL, not a defect, and whether it matches the report ("flies to the target") would
need a strike on an intercepting city.

### 4.2 Side observation: the atomic bomb's combat always waits for the 20 s limit [L]

Every Atomic Bomb strike (a2, a4, a6, c1, p1, p3) ran 20.04-20.07 s from `RunCombatSim` to `EndCombatSim`, switched
or not, stock or product, while every Nuclear Missile strike ended by itself in 14.0-14.8 s. 20 s is the EXE's limit
for a combat with an air unit (combat-choreography.md 1 item 4, 13.2), so the bomb's simulation never finishes on its
own: the explosion is over by about +12 s and the map stands still for about 8 s with the human's interface waiting.
The bomb weapon is the dropped one (`IsDropped` 1, `TargetGround` 1, `WaitForEffectCompletion` 1, section 1); the
missile's is a hit effect. Why the bomber's simulation does not end (a member that never reaches its end state, like
0477's submarine, or the bomber's fly-away) is not determined [I]; it is not the report's symptom and was left
outside the timebox. If it matters (an AI turn with several atomic bombs would add 20 s each under the human's
view), it is a candidate for its own ticket.

### 4.3 Evidence

- Drivers and scene: `tools/harness/evidence/0553/` (`n.py`, `setup.lua`, `nuke.lua`, `sheet.py`, `summ.py`; 0456's
  `logger.lua` and `logread.lua` are reused from `server/evidence/0456/scripts/`). Per-strike records (events, city
  before and after, shot times): `tools/harness/evidence/0553/strikes/strike-<label>.json`.
- Pictures: Visual Checks, issue 0553, all DIAGNOSTIC (not acceptance pictures: window shots by PrintWindow at half
  scale, the stock and product routes without the server's world renderer). Contact sheets and the two save copies
  (`C5H-0553-TINY_0008`, `C5H-0553-T10-QCOFF`) are kept in `F:/c5-64bit-work/0553/` and `C:/c5-64bit-work/0553/`.
- Instance restore (2026-11-18 15:21-15:24): `set-proxy --original`, `set-dll --previous` (back to `3c2f47da...`),
  `frontend.py restore`, the save copies removed, `backup.py verify` of the standard set equal to the start backup,
  `VP64Options.ini` unchanged by hash, `provision.py verify c5h-b` ok (no errors, no changed mutable files); claim
  released. No crash, no box, no assert in any run.
