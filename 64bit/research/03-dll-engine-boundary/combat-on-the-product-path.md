# Combat on the product path: human attacks with Quick Combat off and on (issue 0316)

Queue ticket 0316 (increment 2 risk named by the Architect's increment 3 sheet,
`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md` section 5): the product's D33 key
list turns the EXE's Combat system off (with Projectile and Particle; `proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2`),
and no run of a human attack with Quick Combat unticked had been recorded. The stub plan expected the turn to stall `[I]`
(`research/sources/shell-gates/stubs/unit.md` 3d, `coupling.md` "Combat animation hand-shake").

*Since 2026-11-15 (issue 0368, the owner's decision D147) the product's list no longer turns Combat, Projectile and Particle
off: the EXE draws the combat over the server's picture. The runs below were made with the three off; the run with them on
in the product is in `research/02-exe-internals/disable-systems-and-masks.md` 5, "Combat, Particle and Projectile on in
the product".*

## 1. What the DLL does with a visualised attack [C, read 2026-11-15 in the x64 pin 04b1211b8, plain copy]

- `CvPreGame::quickCombat()` is the static `s_quickCombat`. It is set by `setQuickCombat` (which also writes the game
  option `GAMEOPTION_QUICK_COMBAT`), by `SetGameOption(GAMEOPTION_QUICK_COMBAT, v)` directly, by `CvGame` init when the
  game option is on, and by the INI key `[GAME] QuickCombat` (`CvPreGame.cpp` 1364-1366). It is saved with the pre-game
  data (`CvPreGame.cpp` 1858, 3642).
- The in-game options menu changes it with `Network.SendGameOptions({{"GAMEOPTION_QUICK_COMBAT", b}})`
  (`Assets/UI/Options/OptionsMenu.lua`, the in-game branch of the OK handler; single player too). Lua reads it with
  `PreGame.GetQuickCombat()`; on the product path `ICvPreGame` crosses to the server's DLL through the proxy.
- In every attack path of `CvUnitCombat` (melee `Attack`, `AttackRanged`, air, city, nuclear) the visualised case
  (`!quickCombat()`) calls `lookAt` on the target plot if it is visible, `GameplayUnitCombat` / `GameplayCityCombat`
  with the combat info (an EXE call; on the product path it crosses from the server to the EXE), sets
  `setCombatUnit` on both sides and returns `ATTACK_QUEUED`, **and then calls `ResolveCombat(kCombatInfo,
  uiParentEventID)` at once, in both cases** (`CvUnitCombat.cpp` 3655-3682 melee, 3714-3772 ranged on a unit). The
  resolve clears `setCombatUnit(NULL)` on both units (lines 500-508) and clears the attacker's mission queue with the
  post-combat delay (`GetPostCombatDelay()`: 110 quick, else `POST_COMBAT_DELAY`, line 27-33). `ATTACK_QUEUED` is read
  by no mission code (only inside `CvUnitCombat.cpp`). `CvUnit::IsBusy()` is `GetMissionTimer() > 0 || isInCombat()`.
  The engine-facing unit wrapper (`CvDllUnit.cpp`) has no combat-end entry. So in Vox Populi's DLL no wait for an
  engine combat-end callback was found by reading; the stock DLL's protocol (the Architect's sheet: "visualised combat
  makes the DLL wait for the engine's combat-end callback") is not VP's. What remains open before the live run is the
  EXE side: what the EXE's Unit system does with `GameplayUnitCombat` / `RunCombatSim` while the Combat system is off.

## 2. The live run (method)

Driver `shell/evidence/0316/run_0316.py` (prep and restore are `shell/evidence/3308/run_3308.py`'s, the workstation's
product-path form, as 0307 and 0192 use them): Goldberg instance on the workstation, `[Renderer] ServerRenderer = 1`, the
composite and its manifest beside the proxy, D33 by `shell/tools/darkred.py` with Overlay kept on (D93), no
`server_config=`, no viewmask marker, `vpproxy.ini` `mode=on local_answers=1`; the run checks that config.ini disables
Combat, Projectile and Particle and keeps Unit, City, LandmarkSystem and MovementPath on. Save: a copy
`C5H-0316-TINY_0018` of `C5H-3000-TINY-T0018`. Per arm (Quick Combat off, then on): Quick Combat set by the options
menu's own call and read back; a human Warrior and a human Archer spawned near the capital, a barbarian Warrior next to
each; the melee order as VP's `WorldView.lua` right-click gives it (`UI.SelectUnit`, then
`Game.SelectionListGameNetMessage(GAMEMESSAGE_PUSH_MISSION, MISSION_MOVE_TO, x, y, 0, false, false)`), then the ranged
order (`MISSION_RANGE_ATTACK`); 20 s of 0.5 s samples of both units (damage, moves, `IsBusy`, `IsFighting`,
`IsInCombat`, `IsAttacking`, `IsDelayedDeath`), `HasBusyUnit`, the end-turn blocking type, the notifications; two
DIAGNOSTIC pictures per attack (Visual Checks); then the turn ended with `tools/harness/turns.py end` + `wait`.

## 3. Result: no stall [L, 2026-11-15 00:39-00:48 EST, one run]

Instance c5h-a (workstation, C:), builds from main 0cc2d703 on the x64 pin 04b1211b8 and 9045's release deps (proxy
8BF0D2E4..., server.exe 01D65D56..., x64 DLL 74588135..., composite 4FB56E27...), config.ini keys = 1: OverlayDebug Decal
Forest River Combat Particle Projectile FOW FluidFOW YieldIcons DistanceFog ColorKeyEdit Arrows. Save copy of
`C5H-3000-TINY-T0018` (Spain, turn 18), which has Quick Combat on; set off and back on through the options menu's call,
read back as `PreGame.GetQuickCombat()` false / true and `GAMEOPTION_QUICK_COMBAT` false / true.

| Quick Combat | Attack | Resolved after the order | Defender damage | Attacker busy / fighting / in combat (41 samples, 20 s) | Turn ended |
|---|---|---|---|---|---|
| off | melee (`MISSION_MOVE_TO`) | 0.13 s | 0 -> 34 (attacker took 26) | 0 | 18 -> 19 in 6.0 s |
| off | ranged (`MISSION_RANGE_ATTACK`) | 0.12 s | 0 -> 27 | 0 | (same end turn) |
| on | melee | 0.13 s | 0 -> 33 (attacker took 18) | 0 | 19 -> 20 in 5.7 s |
| on | ranged | 0.13 s | 0 -> 31 | 0 | (same end turn) |

- `HasBusyUnit()` was false in every sample; the end-turn blocking type stayed 2 (as before the attacks); the end turn
  (`turns.py end` + `wait`, AI turns included) took 6.0 s with Quick Combat off and 5.7 s with it on.
- **The UI** shows the stock combat message at the top centre ("Your Warrior (26 damage) attacked an enemy Warrior (34
  damage)!", "Your Archer bombarded an enemy Warrior! (27 damage)"). With Quick Combat off the camera centres on the target
  (the DLL's `lookAt` of the visualised path); with it on it does not. No popup. Two still pictures per attack (Visual
  Checks, issue 0316, DIAGNOSTIC; 3 of 8 are flat grey PrintWindow frames). Whether a moving picture shows any combat
  animation with the Combat system off was not examined.
- **The proxy** carried `ICvEngineUtility1::GameplayUnitCombat` to the EXE three times with Quick Combat off: the two human
  attacks and one more during the end turn (an AI-side combat visible to the human `[I]`), none with it on. These are the
  first live calls of the combat pair (`proxy-design.md` 17.4 named it "never called live"). Inside the calls the EXE read
  the combat info: `GetUnit` and `GetVisualizeCombat` 3 times per combat, `GetPlot`, `GetAttackerAdvances`,
  `GetDefenderRetaliates`, `GetAttackIsRanged`, `GetAttackIsBombingMission`, `GetAttackIsAirSweep`, `GetDefenderCaptured`,
  `GetDamageMemberCount` once, `GetNuclearDamageLevel`, `GetDamageInflicted`, `GetFinalDamage` twice; and asked
  `ICvPreGame1::quickCombat` once per combat. Each call logged one LOUD line "received a DLL object ... not in the
  borrowed-argument list"; 0 "unknown handle" or "not a live wrapper" lines (no use after the call seen).
- server.log 0 LOUD, guard checks 0/0/0, start-up watch ok, harness stop rc 0, restore by SHA-256 ok.

Evidence: `shell/evidence/0316/acceptance.txt`, `data/` (run.json with every sample, summary.txt, vpproxy.log,
server.log, proxy-calls.tsv, server-report.txt, server.cfg, prep.json, restore.json); driver `run_0316.py`, summary
`summarise_0316.py`.

So the Architect's risk does not occur in increment 2: with the EXE's Combat system off, a visualised human attack does
not wait for anything. VP's DLL resolves at once (section 1) and the EXE's Unit system queues the combat message and
returns. The stub plan's "turn stalls" `[I]` is answered for this configuration. Increment 3 (0042) still decides where
the combat end goes once the server draws units; today there is no wait to answer.

## 4. The EXE's GameplayUnitCombat handler borrows its argument [C, 2026-11-15]

Read by hand with llvm-objdump 19 on the analysis copy (`%USERPROFILE%\tools\ghidra-projects\bin\CivilizationV_DX11.exe`;
addresses only, D83). Engine utility vftable 0x9EA718, slot 161 -> **0x67DD90** (`__thiscall`, `ret 4`); the neighbours
agree with `proxy-design.md` 17.4 (160 -> 0x444040, 162 -> 0x58E880, 164 -> 0x4C13D0, 165 -> 0x66F040, 175 -> 0x741BB0,
176 -> 0x484E70). The argument is loaded once into `ebx` and used only as the `this` of its own getters: 23 call sites,
vftable offsets 0x8 and 0x10-0x40 (slots 2 and 4-16). There is no slot 0 (QueryInterface) or slot 1 (Destroy) call, it is
never stored and never passed to another call. The values go into a message built on the stack and queued (0x87E3D0 /
0x87E4A0, then 0x813D60 on the per-context globals 0x2A10E08 / 0x2A10E3C + 0x4C * index, the same queue as
`GameplayNaturalWonderRevealed`). So `GameplayUnitCombat` can join `kBorrowedArgMethods` (`proxy/src/proxy_rt.cpp`), which
silences the LOUD line that every combat with Quick Combat off now logs. That is a proxy change outside ticket 0316; it is
proposed to the MC. *(Done by ticket 0329, 2026-11-15: section 4.1.)* ~~`GameplayCityCombat` (slot 167 -> 0x48CCA0) was
neither called live nor read.~~ *(Replaced 2026-11-15 by 4.1: read, it borrows too.)*

### 4.1 GameplayCityCombat borrows too; both joined the list (ticket 0329) [C, 2026-11-15]

Read by hand with llvm-objdump 19 (Intel syntax) on the same analysis copy, addresses only (D83); the listing and the
small scanner that lists every use of the argument register were scratch files, not committed.

- **The slot.** Engine utility vftable 0x9EA718 slot 167 (at 0x9EA9B4) -> **0x48CCA0**; the neighbours read in the same
  dump agree with `proxy-design.md` 17.4: 160 -> 0x444040, 161 -> 0x67DD90, 162 -> 0x58E880, 164 -> 0x4C13D0, 165 ->
  0x66F040, 166 -> 0x632B10, 168 -> 0x633860. `__thiscall`, one stack argument: a single `ret 4` at 0x48D6B4 (the function
  is 0x48CCA0..0x48D6B6, 0xA84 bytes of locals).
- **The argument.** `mov esi, [ebp+8]` at 0x48CCC2, the only read of the argument's stack slot. `esi` is callee-saved and
  is not written again until the epilogue's message copy (`lea esi, [esp+0x74]` at 0x48D640, after the last getter). All 28
  uses before that are `push esi` followed by a call through a slot loaded from `[esi]` (one of them, at 0x48D293, takes the
  slot from `ecx` loaded at 0x48CE31 before a branch): ICvCombatInfo1 slots **2-14** (`GetUnit` 3, `GetCity` 2, `GetPlot`
  1, `GetAttackerAdvances` 1, `GetVisualizeCombat` 4, `GetDefenderRetaliates` 1, `GetNuclearDamageLevel` 2,
  `GetAttackIsRanged`, `...BombingMission`, `...AirSweep`, `GetDefenderCaptured` 1 each, `GetDamageInflicted` 5,
  `GetFinalDamage` 5). **No slot 0 (QueryInterface), no slot 1 (Destroy), no store of `esi` anywhere, never pushed as a
  plain argument of another call.** The unit and city objects the getters return are the EXE's own (Destroyed through
  their slot 1 before `ret`; one returned unit goes to 0x522730 with a flag), not the argument.
- **Where the values go.** As in `GameplayUnitCombat`: a message built on the stack, queued through 0x87E3D0 and then
  0x87E4A0 (a second copy, `rep movsd` of 0x142 dwords), each followed by 0x813D60, on the per-context globals 0x2A10E08 /
  0x2A10E3C + 0x4C * [0x2A10200]; the return value (the DLL's `uiParentEventID`) is the dword 0x87E3D0 hands back.
- **The unit handler re-read with the same scanner** (0x67DD90..0x67E51E): the argument in `ebx` from 0x67DDB6, 23 getter
  calls on slots 2 and 4-16, no slot 0 or 1, `ebx` reused only at 0x67E454 for the queue's answer (the return value). It
  agrees with section 4.

So **both combat methods borrow their `ICvCombatInfo1` argument for the call only** and since ticket 0329 both are in
`kBorrowedArgMethods`: `"ICvEngineUtility1::GameplayUnitCombat("`, `"ICvEngineUtility1::GameplayCityCombat("` (commit
18371d8c). Who calls `GameplayCityCombat` in the DLL [C, the x64 fork's `CvUnitCombat.cpp` / `CvCity.cpp`, read
2026-11-15]: `CvUnitCombat::AttackRanged` on a city (3798), `AttackAir` on a city (3916), `AttackCity` (a melee unit on a
city, 4093) and `CvCity::rangeStrike` (the city's own bombard, `CvCity.cpp` 33147), each only with `!quickCombat()`, each
followed at once by `ResolveCombat` (as section 1).

Test: `proxy/tools/test_proxy_rt.py` section 6 (`--only-borrowed` runs it alone): per added method its key is in the build's
`vp_ids.gen.cpp` table, the list matches it and the built proxy carries the entry; every entry matches a key; negatives:
`GameplayUnitParadrop` stays unmatched, and the list without the two entries matches neither. [L, 2026-11-15 06:20,
offline] build `C:/c5-64bit-work/0329/b` (commit 18371d8c, proxy d8d3d179...): 12/12; negative control, the same checks on a
build without the change (`C:/c5-64bit-work/0349/srv-rel`, ticket 0349's, main f6175f76): 10/12, exit 1, the two
"built proxy carries the entry" checks fail.
Not changed (outside 0329's files): `proxy/tools/hutest/build_hutest.py` still exempts `GameplayUnitCombat(` LOUD lines
(written when it was unlisted); with the list changed that exemption only hides a regression and can go. *(Gone since
ticket 0344, 2026-11-15: proxy-design.md 17.4.)*

## 5. Not covered (ticket 0316's run)

One run on one Tiny save. The following were not run: an attack on a city or by a city (`GameplayCityCombat`; *run by
ticket 0329, section 6*); air,
nuclear and naval combat; the stock game with the Combat system on, for comparison; a moving picture; Quick Movement
off (it was on throughout).

## 6. Live check of the list change: unit and city combat, Quick Combat off (ticket 0329)

Method: driver `shell/evidence/0329/run_0329.py` (0316's helpers and 3308's product-path prep / restore, as section 2), one
arm, Quick Combat OFF set by the options menu's call. In order: 0316's human melee (`MISSION_MOVE_TO`) and ranged
(`MISSION_RANGE_ATTACK`) attacks on barbarian Warriors (`GameplayUnitCombat`); then a barbarian city made with
`Players[63]:InitCity` on a free unowned flat plot 5-9 from the human capital and at least 4 from every city, with a human
Warrior and a human Archer spawned next to it: the Warrior's melee on the city (`MISSION_MOVE_TO` onto the city:
`AttackCity`), the Archer's bombard (`MISSION_RANGE_ATTACK` on the city: `AttackRanged` on a city); then the human capital's
own strike on a barbarian Warrior spawned next to it, as the banner's bombard button and WorldView's `CityBombard` give it
(`UI.SelectCity`, `Game.SelectedCitiesGameNetMessage(GAMEMESSAGE_DO_TASK, TASK_RANGED_ATTACK, x, y)`,
`UI.ClearSelectedCities()`; walls added only if the capital cannot strike: `CvCity::rangeStrike`); 20 s of samples and two
DIAGNOSTIC pictures per attack; the turn ended. Pass: proxy-calls.tsv shows `GameplayUnitCombat` and `GameplayCityCombat`
crossing to the EXE, vpproxy.log has 0 "not in the borrowed-argument list" lines, 0 "unknown handle" and 0 "not a live
wrapper" lines; every attack resolved; the turn ends.

### 6.1 Result: 0 LOUD lines, both methods crossed, every attack resolved [L, 2026-11-15 06:32-06:38 EST, one run]

Instance c5h-d (workstation, F:), builds from branch task-0329-gameplay-combat-borrowed at 18371d8c on the x64 pin
04b1211b8's plain copy and 9045's release deps (`C:/c5-64bit-work/0329/b`, release): proxy D8D3D179..., server.exe
073763FD..., x64 DLL 74588135... (= the pin), composite 087C0E02...; EXE 04CFDC71.... (Build lines say dirty=1: the only
uncommitted files at build time were `test_proxy_rt.py`, which is not built into anything, and the untracked worktree
marker; `git diff 18371d8c..` over proxy/src, proxy/gen, server, ipc, shell/composite, shell/frame, render and deps is
empty.) config.ini keys = 1 as section 3 (Combat, Particle, Projectile off; Unit, City, LandmarkSystem, MovementPath on).
Save: our copy `C5H-0329-TINY_0018` of `C5H-3000-TINY-T0018` (Spain, turn 18), Quick Combat on in the save, set OFF by the
options menu's call and read back false / false.

| Attack (all Quick Combat OFF) | DLL path | Before -> 20 s after | Attacker busy / fighting / in combat (41 samples) |
|---|---|---|---|
| Warrior melee on a barbarian Warrior | `Attack` -> `GameplayUnitCombat` | defender damage 0 -> 34, seen 0.14 s after the order | 0 |
| Archer bombard on a barbarian Warrior | `AttackRanged` -> `GameplayUnitCombat` | defender damage 0 -> 27, 0.13 s | 0 |
| Warrior melee on the barbarian city | `AttackCity` -> `GameplayCityCombat` | city damage 0 -> 46 (the Warrior took 24), 0.14 s | 0 |
| Archer bombard on the city | `AttackRanged` on a city -> `GameplayCityCombat` | city damage 46 -> 89, 0.13 s | 0 |
| The capital's strike on a barbarian Warrior next to it | `CvCity::rangeStrike` -> `GameplayCityCombat` | Warrior damage 0 -> 35, 0.14 s (the capital could strike without walls: `CanRangeStrike` and `CanRangeStrikeAt` true, strength 900) | 0 (the defender) |

- **The proxy** (`proxy-calls.tsv`, dll->engine): `GameplayUnitCombat` 3 calls (the two unit attacks and one during the end
  turn, an AI-side combat visible to the human `[I]`, as in 0316), `GameplayCityCombat` 4 calls (the three city attacks and
  one more during the end turn `[I]`). **vpproxy.log (386 lines): 0 lines "not in the borrowed-argument list", 0 "unknown
  handle", 0 "not a live wrapper", 0 LOUD of any kind.** Before 0329 each of these 7 calls logged one LOUD line (0316: 3
  of 3).
- Inside the calls the EXE read the combat info as section 4.1's reading predicts: `GetCity` 4 (only the city handler calls
  slot 3, once per call on the path taken), `GetDamageMemberCount` 3 (only the unit handler calls slot 16), `GetPlot` and
  the other single getters 7 = 3 + 4, `GetVisualizeCombat` 21 = 3 x 3 + 4 x 3, `GetNuclearDamageLevel`,
  `GetDamageInflicted`, `GetFinalDamage` 14 each = 3 x 2 + 4 x 2; `ICvPreGame1::quickCombat` 2 (engine -> DLL);
  `ICvUserInterface1::setCombatFocus` 21 (DLL -> engine).
- `HasBusyUnit()` false and the end-turn blocking type 2 in every sample; the end turn (`turns.py end` + `wait`, AI turns
  included) 18 -> 19 in 6.3 s.
- server.log 0 LOUD (its "FAILED: the engine is gone ... PeerDied" at 260.0 s is the harness's safe stop, as in 0307 and
  0316), server report: 0 scratch Lua errors, 0 methods without remote treatment, database 0 differed; render.log and
  shell_composite.log 0 LOUD. Guard checks 0/0/0, start-up watch ok, harness stop rc 0, restore by SHA-256 ok (15 files),
  provision verify rc 0, save copy removed.
- A first run of the same driver (06:24-06:31) asked the city for `IsMadeAttack`, which VP's Lua does not expose; its city
  samples failed (Lua errors), so the run is not the evidence. Its proxy log agrees: `GameplayUnitCombat` 2,
  `GameplayCityCombat` 4, 0 borrowed-argument lines (`shell/evidence/0329/data/run1/`).
- 10 pictures (two per attack), DIAGNOSTIC (D138), in Visual Checks under issue 0329 with what each shows; the first run's
  10 too. They were not examined for the camera or for combat animation.

Check: `shell/evidence/0329/check_0329.py OUT` (13 checks: run and stop, Quick Combat off, the five attacks resolved without
Lua errors, at least 2 + 3 calls of the pair, the three log counts, the turn ended): PASS on this run; on 0316's data
(negative control: LOUD lines, no city attacks) FAIL, exit 1. Evidence `shell/evidence/0329/` (`acceptance.txt`, `data/`).

Not covered by 0329: air (`AttackAir` -> `GameplayCityCombat` on a city, `GameplayUnitCombat`), air sweep, nuclear and naval
combat; Quick Combat ON (the pair is not called then, section 1); the stock game for comparison; a moving picture.
