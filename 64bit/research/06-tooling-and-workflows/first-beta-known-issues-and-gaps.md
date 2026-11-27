# The first beta (0.1b): known issues, what is not in it, what is not tested, where documents disagree

Ticket 9082 (the owner's order, 2026-11-21 shortly after 23:00 EST). Written by the one-off 9082 (Sonnet), read only: no build,
no game, no test instance, no install was touched.

**State read.** `main` at `3fc921def` (2026-11-21 23:10 EST); this branch grew from `b44392f95`. Read between 23:10 and
about 23:45 EST on 2026-11-21. The state moves tonight (live land/water terrain part 2, the charge change, the candidate's
option set), so every line below says what was true at that time; check `docs/MC-STATE.md` before relying on a line.

**What "the first beta" means here.** The release candidate that the MC builds from `main` on Sunday 2026-11-22 (decision
D232: the 1 h hotfix suite first and alone, then the candidate goes to the owner, then the usual plan runs on the same build),
with the DLL pins x64-0.1b `1b87dd185` and i0-0.1b `f33fc46a4` and the option set of `docs/RELEASE-0.1b.md` section 5 and
`research/05-graphics-composition/unit-combat-server.md` 17.1. The candidate does not exist yet. Everything below is the
state of `main` plus what the MC has ruled will be in it.

**Marks.** `[C]` read in a file, ticket or code; `[L]` seen live in a run that is recorded; `[I]` inferred by me;
`[?]` I could not establish it. "BT" = `docs/BETA-TESTERS.md`, "RB" = `docs/RELEASE-0.1b.md`, "KC" =
`research/06-tooling-and-workflows/known-crash-causes.md`, "OPT" = `docs/OPTIONS.md`, "MCS" = `docs/MC-STATE.md`,
"UCS" = `research/05-graphics-composition/unit-combat-server.md`, "LT" = `research/05-graphics-composition/live-terrain.md`.
All dates are 2026 unless written out. Times are EST.

**Counts.** A: 34 items (A1 crashes and hangs 8, A2 fights 9, A3 land and water changes 4, A4 pictures and speed 8, A5
install, start and operation 5). B: 30 items (B1 drawn by the old program 6, B2 switched off by default 6, B3 waiting for a
later beta 14, B4 not in any beta soon 4). C: 17 items. D: 17 items.

---

## A. Known issues a player of the first beta will or may meet

"Tells testers?" says whether BT already tells them: **yes** (BT line), **partly**, **no**, or **pending** (BT holds a
PENDING block for it).

### A1. Crashes, hangs and data loss (no fix known, or a fix that is only partial)

| # | What the player sees | When | How bad | Source | Tells testers? |
|---|---|---|---|---|---|
| A1.1 | The game closes (crashes) while loading a map of more than about 20,000 tiles (for example 200 x 120). With or without the patch. | Only oversize custom maps | Crash, no fix possible (EXE code) | KC K11 (line 48) [L limit, I mechanism] | yes (BT:150) |
| A1.2 | The game closes while a save loads. Seen once (2026-11-16); never seen without the patch. Cause not found. | Rare, at load | Crash, no fix | KC K9 (line 46); tickets 0539, 0543, 0544; D185 open | yes (BT:152) |
| A1.3 | With the mod "Squads for VP", the game crashed once when going back to the main menu after a game. | Rare, with that mod | Crash, no fix | KC K10 (line 47); ticket 0545; seen once 2026-11-13 | yes (BT:154) |
| A1.4 | Multiplayer and hot seat have never been tried with the patch, and the speed option "hidden embarks" is untested there (a desync risk). | Any multiplayer or hot-seat game | Unknown | KC "Not covered" (line 132); OPT HiddenEmbarks (OPT:365); ticket 0400 (idea only) | yes (BT:149, BT:134) |
| A1.5 | With the patch switched off the player runs their own Vox Populi DLL. Then Vox Populi's own limits apply: about 450 unit flags in view crash the game; very large late games run out of memory; the DLL fixes the patch carries (bomber freeze, religion crash and the others) do not apply; a save of the other civilisation variant is read badly. | Only with the patch off | Crash; the patch is not the cause | KC K4, K12, K13, K19, K20 (lines 41, 49, 50, 56, 57) and "Rows the MC must rule on" 5-6 (lines 116-120); D225 (3) | partly (BT:163 names flags and memory; the bomber and DLL-fix gap is in BT:92 only as "the fixes the patch's own DLL carries do not apply"; the other-variant save case is not told) |
| A1.6 | A rare missing farm field or road curve on large late games after one of the patch's guards acted (the guards stop a game crash by dropping a message or a farm layout). | Rare, large late games, on or off | Cosmetic (a missing picture part until the tile is redrawn) | OPT GuardsWhenOff (OPT:395); KC K1-K3; ticket 0635; BT N6 draft | yes (BT:188) |
| A1.7 | The first line of defence against the farm-field crash (K1, about 1 in 31 huge loads on stock) is the option `ServerFarmFields = 1`, which the first beta sets (D230/D231). It was measured on both EXE builds (0 crashes in 28 launches, 2026-11-18; 9 + 2 huge loads, 2026-11-21). On the patch-off path only the after-the-fact guard exists, whose effect on the crash rate was never measured. | Huge loads with farms | Crash, mitigated | KC K1 (line 38); ticket 0637 (merged 4097a7271); `research/02-exe-internals/farm-field-double-free.md` 4.8 | yes for the cause and the guard (BT:95-101), not for the "unmeasured rate with the patch off" |
| A1.8 | If the installer's "Put in Vox Populi's own fixes" box is unticked, VP 5.4.8's own three crashes stay (load from the game menu while a game runs; start a game after changing mods; maps wider than 181 tiles). | Only with the box unticked or `/VPFIXES=0` | Crash, not the patch's | KC K5-K7 (lines 42-44); ticket 0638 (main 285e98418); BT:57-62 | yes (BT:160) |

Not listed by choice (D223: a crash cause with a known fix is fixed, not listed): K2, K3, K5-K7 with the box ticked, K14-K18,
K21-K27 (KC lines 39-40, 51-55, 58-64; RB:123-125). K27 (the live-terrain edge-row crash) was fixed and merged tonight at
21:36 (`9326f6782`; MCS 21:36 entry).

### A2. Fights (route S, the server plays the fights) and units

Context: the first beta has the server draw the units and play the fights (`ServerUnits = 1`, `ServerCombat = 1`;
UCS:4069-4073). The owner decides the final values after playing it (RB P2, RB:132). Everything in this block is "pending" in
BT (the P2 block, BT:199-218), except where noted.

| # | What the player sees | When | How bad | Source | Tells testers? |
|---|---|---|---|---|---|
| A2.1 | **A fight that does not end until a 12-second limit.** One unit stays in its "ready to fight" pose and the last strike never lands; the fight waits 12 s and then ends by itself. Cause found 2026-11-21 22:59: the new charge rule (ticket 4737) asks for the charge strike without checking that the unit took the charge; with a one-frame hitch the machine is stuck. Harness: 1 of 15 fails with the rule on, 15 of 15 pass with it off. Seen live once in an AI turn (a Shoshone Pathfinder charge against a Barbarian Warrior). **Ruling (MC, 22:59): `charge_any` is switched OFF by default for the candidate**; branch `w46-chargeoff` (built green 23:06, tests running) is not on `main` yet. The real fix is written for the next beta. **The owner's tonight preview installer carries it ON.** | AI or human fights of units with a charge (melee) | A wait of up to 12 s per such fight; not a crash | MCS 22:37 and 22:59 lines (MCS:1935, 1938); queue 0042 notes #9923, #9927; D234 | **no** (the MC owes a line "count of rush members at the off default by 01:15, else a known fault in BT") |
| A2.2 | **Fights are shorter than stock.** Series 56eb (oldpc, 2026-11-21, huge save, AI turns 350-359): melee 0.87x, ranged 0.69x, naval 0.79x (siege 0.65x from series fe44). Job 082e (22:10-23:11): melee 0.81x, ranged 0.71x, naval 0.79x. No fight ran to the limit in those series, all 168 of 168 holds ended normally. The shorter volleys are ticket 4741 (after the beta). | Every ranged, siege or naval fight; melee less | Cosmetic and a timing difference | BT:199-210 draft (a); RB:132; queue 0042 #9926 | pending |
| A2.3 | **Sound differences.** Per fight, the server plays 1.6 to 2.7 times as many one-shot sounds as stock (route S against stock: melee 34 against 18, ranged 43 against 17, kill 49 against 18, air 57 against 36, after ticket 4737; before it: melee 80, kill 63, ranged 98, air 72). When ranged, siege or naval units reposition in a fight, their run and ground-surface loops are heard less often (stock 32 loop lives against 4 in 17 fights); ticket 4735, not a gate for the first beta. The old 3-second sound cut is gone (ticket 4734). Some one-shot unit scripts are refused by the EXE's play rules ("played 0" for the victory tail). | In fights | Cosmetic | RB:132; BT:199-210; UCS 10.10; queue 0042 4730 closing text; ticket 4735 (MCS 22:16: merges before the candidate only with its own re-run, else next beta) | pending |
| A2.4 | **Some of stock's fight effects are not drawn**: a destroyer's long-range fire, the splash and the sinking effect are missing in route S; hits at the defender's figures do show. Effects use the EXE's own particle system at the server's command (stage A, D211). | Naval and city fights | Cosmetic | UCS 15.9 table row "P2 (the EXE's effects on the server's figures)" (2026-11-21 15:35, W38, by eye) | **no** |
| A2.5 | **An atomic bomb's fight runs to a 20 s air-combat limit.** On stock too (ticket 0560). On route S a nuke fight ran to the server's 20 s limit and logged "no pose program" (ticket 4729, fix on the route S branch, merged with stage 5 [I: its ticket front matter still says in-progress]). | Nuke strikes | Long wait, no crash | ticket 0560 (backlog p2); ticket 4729 | no |
| A2.6 | **The melee combat sound repeats over and over long after the fight [reworded for privacy].** The owner's report (D210). Not reproduced on stock in 32 combats (2026-11-20 13:58); the owner says it happens mostly with swordsmen early in a game. Route S's sound hand-over (every sound dies with its fight) is meant to remove it. The fix is by construction, not shown against the owner's case. 2026-11-23 (W105, `../02-exe-internals/exe-audio.md` 11.1): not reproduced on the product path (route S as main ships it) in 30 fights under the same camera variants; no fight at the 12 s limit, no replay on the camera's return, the owner's case by Lua order (mouse and keys not run). | Early game, swordsmen, camera moved | Annoying, no crash | ticket 0599 (backlog p7; log 2026-11-20 11:14, 13:58); UCS 15.9 (sound row: "The owner's case" by mouse and keys not run, A2.7) | no |
| A2.7 | **The owner's own case of fights, played by mouse and keys on the product, has never run** (the driver `art_hi` was fixed, not run live). Other live sessions (S1-S12) passed on both EXE builds. | - | Unknown | UCS 15.9 table (sound row), ticket 4779 log 2026-11-21 18:05 | no (this is a test gap, also C7) |
| A2.8 | **The EXE's fight timing and the server's figures in step.** With `ServerCombat = 0` the game plays fights itself while the server moves the figures on its own clock; figures, projectiles and sounds may not be exactly in step. Checked live on both builds (sessions S8 and S9, 2026-11-21): no error report, every fight audible. | Only if the tester sets `ServerCombat = 0` | Cosmetic | BT:212-216 draft (b); UCS 15.9 | pending |
| A2.9 | **An embarked unit was invisible; figures after a heal; a city-struck unit refused** (tickets 4739, 4736). 4739 and 4736 are merged (MCS 19:34 entries; `0a3785cbd`, `88beba841`). The acceptance row "no doubled, frozen or early-vanishing unit" read "not met" at 15:35 for the two cases; the after-checks S11 and S12 passed (MCS 18:05 entry). A "survivors' run back" difference is ticket 4742, filed by the lead, backlog; no ticket file is on `main` [?]. | Ships and units in boats, fights | Was wrong picture; now believed fixed | UCS 15.9; MCS:1904, 1932 | no |

### A3. Land and water changes during play (epic 0615)

Epic 0615 is **partly on `main`** (an early milestone merged 2026-11-21 15:53 as one squash commit; the h18 edge-row crash
fix `9326f6782` merged 21:36) and its second request is planned about 03:30 on Sunday (MCS 21:44-21:52 entries, lines
1925-1926). The first beta contains live land/water changes with the option default on (D216; MC ruling on I3, MCS 18:48,
line 1908). BT holds two wordings and waits for the merge (BT P3, BT:220-230).

| # | What the player sees | When | How bad | Source | Tells testers? |
|---|---|---|---|---|---|
| A3.1 | When a tile changes between land and water, the shore right beside a river keeps its old outline for about one tile (a notch of old water or a strip of the old bank) and the river still ends where it did at load. A reload draws it right. Fix: ticket 0631 (after the beta). | A mod, a wonder or a script turns land into water or the reverse, next to a river | Cosmetic | LT 11.3 item 1 (LT:4906-4924, the wording "MC #8770 (2)"); ticket 0631 | pending (BT:222-229 (a)) |
| A3.2 | **Changes after an ice change were not drawn until the next turn** (found 2026-11-21 18:47). Cause: the game's DLL raises no "plot changed" signal for a type or terrain change with equal yields. The fix (a rolling plot refresh, `epic-0615-lw-2` @a8f6c508d, 229 tests) is **not on `main`**; it comes with the second request. A DLL notification for the rest is ticket 0656 (file comes with 0615's branch). | Land/water changes beside polar ice | Delayed picture (until next turn) | MCS 18:47 (line 1908), 19:17 (1909); ticket 0615 log 2026-11-21 18:47 | **no** |
| A3.3 | White patches can show on the shore where ice becomes land, and each change beside the ice costs one short hitch while the ice edge catches up about 1.2-1.3 s later. A few small dents or bumps on a new beach (0.06 % of covered samples on Tiny, 0.14 % on huge). A change next to a natural wonder keeps the load's ground on the wonder's own tile. | Land/water changes, ice, wonders | Cosmetic | LT 11.3 items 2-4 (LT:4952-4976); item 4's hitch is [I], "not run live yet" | no |
| A3.4 | The EXE's minimap and Strategic View colours follow a land/water change only after a reload (stock behaviour). A reload re-rolls the pieces of distant plots, so "live equals reload" holds only up to that. | Any land/water change | Cosmetic, stock | LT 11.3 items 5 and 7 (LT:4977-4988); ticket 0552 (stock minimap bug) | no |

### A4. Pictures and speed differences from the unpatched game

| # | What the player sees | When | How bad | Source | Tells testers? |
|---|---|---|---|---|---|
| A4.1 | The pattern of fields around a farm is the patch's own, not stock's, and the same on every load. Each farm building (barn, silo) stands at the centre of the farm's tile among the fields, where stock puts it at a field's edge. Before ticket 0655 (merged 21:34) no farm building was drawn at all with this option. | Every farm | Cosmetic | OPT:219; BT:129, 191-194; tickets 0655 (done), 0657 (field-edge placement, backlog p4, after the beta); D230, D231 | yes |
| A4.2 | AI turns look different by design of the speed options: the AI's moves are shown further behind the game, some may still be shown when your turn starts, and the AI's last moves appear together at the end. | Every AI turn | Cosmetic | BT:183-185; OPT AiSkipVisualDrain, AiDrainUnordered; D197 (the owner [reworded for privacy]: option 4 may be revisited later) | yes |
| A4.3 | AI units that board or leave boats out of every human player's sight are not shown doing it. | AI turns | Cosmetic | BT:186-187; OPT HiddenEmbarks (OPT:360-365) | yes |
| A4.4 | Frame rate: drawing the cities and improvements in the 64-bit program costs about 2 ms a frame (193 to 141 fps, RTX 2080, huge map, ticket 4887). RB:105 says nothing changed since; the job of 2026-11-21 (oldpc, cities OFF) read stock 212 fps, route S 214 fps. | Every frame with cities on | Performance | BT:195-197; RB:105 (O7); queue 0042 #9926 | yes |
| A4.5 | Memory: the server holds about 173 MB more with the unit scene and fights, a one-off (huge save, 2026-11-21); the farm option adds about 113 MB in the server and removes 14 MB from the EXE; the terrain keeps the whole map ready by default (`BakedTileCameraRadius = -1`; a card short of video memory should try 5, with grey tiles after fast pans). | Huge maps | Memory | UCS 20.6; ticket 0637 (`farm-field-double-free.md` 4.9); OPT:60-69; BT:125 | partly (BT:125 only) |
| A4.6 | A picture freeze of 1.7 to 3.2 s while a game loads (the server's render loop pauses at load; 2459/365 ms and 2951/391 ms seen ~43 s after launch; up to 3235 ms on a 128x84 load). The lead found it harmless; "no first-beta item". | Game load | Cosmetic | MCS 18:19 (line 1903); queue 0042 #9927; LT 11.6 | no |
| A4.7 | **After a far camera jump on a wrapping map, the cities and improvements are not drawn for one frame** (try a minimap click). Ticket 0659 (backlog p5, opened 21:11). | Wrapping maps, jumps | Cosmetic, one frame | ticket 0659; MCS 21:11 (line 1919) | **no** |
| A4.8 | Stock bugs that players report and that the patch does not cause: a coastal city drawn about 3 times too small (0555); a tile that looks like a mountain although it is flat, after mods change plots (0554); Lake Victoria's thick river turning thin (0556). All reproduced on stock and closed 2026-11-18 as stock behaviour (D187). | Mod games | Cosmetic, stock | tickets 0554, 0555, 0556 | **no** (the "What not to report" list BT:263-272 does not name them) |

### A5. Install, start and operation

| # | What the player sees | When | How bad | Source | Tells testers? |
|---|---|---|---|---|---|
| A5.1 | The first start takes a few minutes (mod activation). | First start | Waiting | BT:158; RB:100 (O2, [I], "not re-measured") | yes |
| A5.2 | On a very slow or busy disk the city screen may not open by itself after founding a city; click the city. Same on stock. | Slow disks | Minor | BT:156; ticket 0569 | yes |
| A5.3 | The patch ends the game rather than show a broken map, with a window "Vox Populi 64-bit patch: error report" and a report folder. With ticket 0641 (merged, `5bf48a276`) a short stall is waited out (counted in frames that ran, up to 30 s) and every deliberate end shows the reason. BT still holds both wordings (P4) and has not been updated. | Heavily loaded PC; failed start | Game ends on purpose | BT:72-73, 167-176; RB:134 (P4); KC K25 (line 62); ticket 0641 | pending (stale: A5.3 is now case (a), see D5) |
| A5.4 | Switching the patch off by editing `vpproxy.ini` by hand ends the game before the map with a window and puts `config.ini` back; use `vp64helper off`. | A tester who edits the file | Window, no harm | BT:108-112; ticket 0635; KC K14-K16 | yes |
| A5.5 | A later Civilization V update the installer does not know is refused ("look for a newer version of the patch"); at run time an unknown build gets the popup and report, not a silent 32-bit game. EUI tooltips may fail on the 19 November build with or without the patch (Firaxis removed `loadstring`; [I], not seen live). Updating Vox Populi deletes the patch's files with the old Vox Populi. | After a game or VP update | Blocked install | BT:29-31, 78-79, 271-272; ticket 0588 (popup seen live on 26577625, MCS 2026-11-19 entry); `new-exe-build-26577625.md` B.1 | yes |

---

## B. What is not in the first beta

### B1. Drawn by the old 32-bit program (the EXE), by design or for now

| # | What | Why | Source |
|---|---|---|---|
| B1.1 | The **minimap** (the EXE's own). The server-drawn minimap is ticket 0377. | The owner 2026-11-18 21:50 (D195 amended) [reworded for privacy]: later is acceptable; the EXE's minimap equals stock on the product path | D195 (decision log line 1605, amendment); ticket 0377 (backlog p6) |
| B1.2 | The **interface**, tooltips, flags and all Lua. | Design (D21): all Lua, UI and gameplay stay in the EXE so mods work unchanged | STATUS.md D21; BT:12-14 |
| B1.3 | The **arc** for ranged attacks and the **path markers** for planned moves. | `ServerArc` and `ServerPath` stay 0; their server-drawn pictures were diagnostic only (MC #6561); turning ServerArc on by default is ticket 0525 | RB:21, 164; OPT:178-209; tickets 0525 (backlog p3), 0574 |
| B1.4 | The **particles and effects** of fights (smoke, fire, impacts) by the game's own Particle system at the server's command (stage A). The server draws the particles itself in stage B (tickets 4787-4794, all backlog p9). | The owner, D211 (2026-11-20): 0.1b may ship with stage A; stage B returns about 43 MB to the EXE and changes nothing the player sees | D211; ticket 0042 children 4787-4794 |
| B1.5 | **Roads and road layout**: the server draws the roads from the EXE's own layout tap. The server's own, tidier layout (`FixRoadLayout`) is off. [I: I did not find a file that states in one place which road source the first beta uses; this reading is from OPT:457-475 and ticket 0649.] | Maintainer's decision 2026-11-20: off for now; the three-way layout choice is ticket 4940 (D215) | OPT:457-475; D209, D212, D215; ticket 4940 (backlog p6) |
| B1.6 | The **EXE still loads every unit's art and keeps its own unit system** ("state (ii)"); true absence of that system (ticket 0328) is optional and later, so the memory it would save is not saved yet. For the 43-civilisation variant, the **3D leader scenes for majors 22-42 are not drawn** (the shipped configuration writes no key for them); VP's Lua shows a static picture (ticket 0432). | D144 (state ii); the AI turn does not stall without them | D144; ticket 0328 (backlog p3); KC lines 76-78; ticket 0432 |

### B2. Written, but switched off by default or not in effect

| # | What | State | Source |
|---|---|---|---|
| B2.1 | `ServerArc`, `ServerPath` (see B1.3). | 0 | OPT:178-209 |
| B2.2 | `AiSkipVisualDrain = 2` (the hybrid of ticket 0571): merged, off by default; the beta's set is value 1. | table 0; the set writes 1 | OPT:298; UCS 17.1 (UCS:4077); ticket 0571 (done) |
| B2.3 | `GameEventsOneCrossing` (a speed-up of AI turns). | 0 in the set | OPT:338-349; UCS:4080 |
| B2.4 | **All extras.** `EnableBugfixes = 1` but every fix is off: `FixRoadLayout 0`, `FixUnitPlacement 0` ("not yet in effect in this version"), `FixAirBases 0`; `EnableVisualAdditions = 0`: `VisibleAreaOutline`, `GarrisonVisible`, `ShipsMoored` (D207: off), `AirInterceptPatrol` ("not yet in effect"). | Effectively no extra is on in the first beta. | OPT:411-577; BT:136 ("all off ... except the bugfix switch", which has nothing to switch on [I]) |
| B2.5 | The table default of `ServerUnits` stays 0; the candidate gets 1 through the installer's option set. The default-1 change waits on a branch until the owner has played the candidate. | The default is the owner's approval | UCS 17.1 (UCS:4106-4114); RB:132 |
| B2.6 | "Full EXE Optimizations" (code writes to the EXE, D228): nothing of it is in any beta; the patch writes only data, import slots and vtables. | By decision | AGENTS.md section 5 (D228); epic 0647 (backlog p2) |

### B3. Planned or half built, waiting for a later beta

The owner's order of 2026-11-20 (D216): everything the first beta needs first; tier-2 performance and the rest of the enhancements later.

| # | What | Why not now | Source |
|---|---|---|---|
| B3.1 | Server-drawn minimap | After the beta (the owner) | ticket 0377; D195 |
| B3.2 | The farm buildings drawn by the server on a field's edge, and the server drawing the farm buildings through the model layer (ticket 0654 is parked). The beta uses the EXE's own farm-end post instead (D231). | Time; Architect: "less than an even chance for Monday" | tickets 0654 (backlog p2, parked), 0657 (backlog p4); D230, D231 (decision log lines 1994-2022) |
| B3.3 | The real river edges for live terrain; the load-time pass; the rule-A water-copy switch; a DLL notification for plot type changes; a far-jump fix for the wrap seam. | After the first beta | tickets 0631, 0613, 0622, 0656 [file with 0615's branch], 0659 |
| B3.4 | Fixes in the fights: shorter volleys (4741), run and surface sound loops (4735), survivors' run back (4742), fix of the charge change (4743, the real fix after the candidate), nuke fight length (0560, stock). | After the beta, except where a re-run is shown | tickets 4741, 4735; MCS 22:16, 22:59 |
| B3.5 | **Stage B effects**: the particle file reader, the particle tap, simulation, drawing, model effects, projectiles, trigger streams for stage B (4787-4794). | D211 | tickets 4787-4794 |
| B3.6 | The full-game soak with units drawn by the server (4780) and the acceptance of `ServerUnits` default 1 (4781). Both are backlog; the acceptance kit was planned for Sunday 10:00-15:00. | Waiting for the candidate | tickets 4780, 4781; UCS 17.3-17.8 |
| B3.7 | Extras of increment 4 that are not merged or still in a last check: road layouts 4940, road clearance 4936, sharper terrain 4937 (experiment), unit placement 4982, garrison 4983, moored ships 4984, air bases 4985-4987, air animation, smoke items 4988-4991, the contribution guide 4938, the extras flags 4931 (in review). | D216 | tickets 4931, 4934, 4936, 4937, 4938, 4940, 4982-4991; epics 0454, 0455 |
| B3.8 | Tier-2 exact speedups (epic 0562), the third list (Full EXE Optimizations, epic 0647 with tickets 5030-5044), PGO in release builds (D184, ticket 0513, backlog), the trade package (4808). Whether the beta's DLL pins are PGO builds: [?] I did not find it written. | D216 | tickets 0562, 0647, 0513, 4808; D184 |
| B3.9 | Server-side memory savings S1-S8 of the unit scene (0633); the server terrain bake reading decal textures from disk on the render thread (0642, a stall cause on a loaded disk); first map frame waits for the fog after load (0529). | After the beta | tickets 0633, 0642, 0529 |
| B3.10 | Release tooling repair after the candidate: release driver stop handling (0634, 0640, 0650); series rows for the 0.1b pins (0639); purity re-audit (0628). | After the candidate | those tickets |
| B3.11 | The upstream export (epic 0468 with 4702-4707): nothing is sent upstream and **no export to the fork's `x64` branch** (the owner, D195). No GitHub release before the owner has played the build and said OK (D150). | The owner's order | D195, D150; ticket 0468; RB:70-72 |
| B3.12 | Bug fix of a DLL fault found in the pins' neighbourhood: the pantheon's holy-city coordinates (ticket 0508, in progress). [?] I did not establish whether this is in the 0.1b pin. | - | ticket 0508 |
| B3.13 | Open DLL question marks on a newer Vox Populi: the proxy Lua bridge on master (0314, 0315). Pull requests upstream for VP's two crash fixes (0081, waiting for the owner). | Not part of the beta | tickets 0314, 0315, 0081 |
| B3.14 | The test harness fixes (0126 and others) and the upstream draft for the new EXE addresses (D218). | Not product | ticket 0126; D218 |

### B4. Not in any beta soon (ideas, no ticket scope yet)

| # | What | Source |
|---|---|---|
| B4.1 | Maps far above stock size (about 32,000 plots, 360 x 180, up to 1000 x 500) | ticket 0594; D208 |
| B4.2 | More than 62 civilisations, up to about 100 | ticket 0595; D208 |
| B4.3 | One release for the 22- and 43-civilisation variants, one menu | ticket 0469 (idea) |
| B4.4 | Hot-seat play under human-input testing; a map picture export; a rule-changing extras category | tickets 0400, 0457, 0458 (ideas) |

What *is* in it, for contrast (BT:66-70, RB 5, UCS 17.1): the server draws terrain, units (and plays fights), cities, improvements,
resources, natural wonders and farm fields; the AI-turn speed options (`AiUpdateTickMs 0`, `AiSkipVisualDrain 1`,
`AiDrainUnordered 1`, `HiddenEmbarks 1`); live land/water changes; the guards and the error window and report; VP's three Lua
fixes by the installer; both EXE builds; the standard and the 43-civilisation variant; the mods and modpack forms.

---

## C. What is not tested or not verified for the first beta

| # | What | State | Source |
|---|---|---|---|
| C1 | **The candidate itself.** It does not exist yet. Its chain (D232) is: hotfix suite (about 90-105 min, the candidate's own take to pack first, about 45 min) alone, then the candidate goes to the owner; then the validated tier (build, save test, whole games, instance stage, about 5 h 45 min), both human-input release journeys on the installer-laid layout for the standard, the 43-civilisation and the modpack form, and the metrics ledger record. What has NOT run when the owner gets it is named to the owner by the MC (D232 (c)). | Planned | D232 (decision log lines 2023-2040); MCS 21:39 (line 1923), 22:03; RB 5.1 rehearsal wall times (RB:234-246) |
| C2 | **The candidate's option set** is in no committed file: `tools/release/i3-options.json` (planned in UCS:4088-4105) is not on `main` [C, `ls`, 2026-11-21 23:14]; the descriptor carries it. "To check at the candidate take: its option descriptor carries `ServerFarmFields = 1`" (RB:131). | Not yet checked | UCS:4086-4087; RB:131; MCS 21:34 (line 1921) |
| C3 | **Everything tested with `charge_any` ON.** The hold bars, fight lengths and sound counts of tonight were measured on main's build, charge rule on. The off build (`w46-chargeoff`) is built green; its series on c5h-new-b was planned about 00:45-01:30; the count for rush members at the off default is owed by 01:15. | Pending | queue 0042 #9923, #9927; MCS 22:59 (line 1938) |
| C4 | **Live land/water part 2** (rolling plot refresh, late-coast fix): rule A run (R2) planned 23:40-01:30, huge-map cost 01:45-02:25, request about 02:40; "AI-built road unchecked"; `Waves_Reload` on build 26577625 "not yet verified live" as of 2026-11-20 (KC:80) although I3 part 1 passed on 26577625 at 18:19 (MCS:1903). Ice reload hitch not run live [I]. | Pending | MCS 20:13, 21:44, 21:52 (lines 1916, 1925-1926); KC:80; LT:4970-4976 |
| C5 | **Game versions.** Both EXE builds (4390913 and 26577625) are covered by journeys and loads, but the 4390913 build gets one journey only (D221 (c)); the installed layout plays the **product route only**: its stock route would load VP's 32-bit DLL (no Lua channel for the judges), so it is not played. Vox Populi **5.4.8 only**; what happens with another VP version [?] (BT:32 says 5.4.8). | Partly | RB:213-215 (MC #9064); D221; BT:32 |
| C6 | **Install forms.** Mods form and modpack form are tested on Goldberg test instances (install, load, 10 turns, exit, removal by hash). **Never on a real Steam install by an agent** (D26); the owner's first real install is tonight's unvalidated TEST preview (D234), which the MC has not played. A path of more than 260 characters in the installer: never tried (installer-notes 13.7, KC:139). Non-English Windows, user names with special characters, folders on other drives: [?] not found. The installer's own claim "asks you about visual additions" is not in the installer (D9). | Partly | RB:230-232; D234; KC:139 |
| C7 | **Human play.** The HI journeys (release-basic, release-large) pass on the rehearsal's layout; fights by mouse and keys on the product (`art_hi`) never ran; the flag-model offset (the units' flags against the models) is not measured; the full-game soak with units (4780) is not run. | Gap | UCS 15.9; tickets 4779, 4780 |
| C8 | **43-civilisation variant.** The large and basic journeys passed on an installer-laid layout on 26577625 (ticket 0643, main `18e6d3774`); the oversize 43-civilisation crash tests (0528, 14 loads) ran on 4390913 only; ServerFarmFields on oversize 43-civ: the probe was running 2026-11-21 20:55 (MCS:1918). The 43 variant's leader scenes (B1.6). KC:134 still says "not yet on record" (D2). Live land/water on 43-civ: [?] not found. | Partly | ticket 0643; KC:128, 134; MCS:1905, 1918 |
| C9 | **Hardware.** GPUs in my sources: RTX 4080 (workstation), RTX 2080 (oldpc), a Quadro P3000 with Intel HD 630 (the laptop), an RTX 5070 on a remote host in one survey. No AMD card and no Intel-only machine found in any record [?]. BT:18-19 says the beta was tested "on a few PCs only". Windows 11 is what the test machines run; Windows 10 is named as supported (BT:23) and I found no Windows 10 run [?]. | Gap | research 06 remote-test-machines.md lines 26, 125; research 05 city-renderer.md, extras.md; BT:23 |
| C10 | **Maps.** Tiny, Standard, Large, Huge (turn 350 and the 18-turn Tiny saves) and the 43-civ large saves. Custom maps 182-198 plots wide (the VP crash K7) are not reproduced; maps over about 20,000 tiles fail before the patch (A1.1). Wrap-seam behaviour has had one fix (0652) and one open item (0659, A4.7). | Partly | KC K7, K11; tickets 0652, 0659 |
| C11 | **Multiplayer, hot seat.** Never tried. | Gap | KC:132; BT:149 |
| C12 | **Memory and speed claims.** The metrics ledger and its acceptance (D151, D154) for the candidate: the stress series for "Increment 2.5" (the candidate's build against stock, 43-civ, turn 250) is one-off 9079 and was still running at 22:08; no result found. Performance claims need statistics (PERFORMANCE.md); the figures in BT:195-197 are from ticket 4887 on an RTX 2080. | Open | MCS 22:07-22:09 (lines 1928-1929); BT:195 |
| C13 | **Crash history.** Test-machine crash ledgers were pruned before 2026-11-15/16 and audited only to 2026-11-18 07:10 on the workstation; sightings after that are known only from tickets and MCS. Epic 0615's and route S's own crash history (4720/4725 spikes, 4727) was not audited. | Gap | KC:125-139 |
| C14 | **EUI.** BT asks testers whether they use EUI; the EUI tooltip failure on the 19 November game build is [I] and "not seen live"; whether any removed Lua function raises an error that stops play was not found written. | Gap | KC:135-136; BT:271 |
| C15 | **The sound with a human ear.** Sound checks are meters and logs (every fight audible, no peak after a fight above 1.25 x stock + 0.01); the owner plays the candidate. | By design | UCS 15.9 |
| C16 | **Stock on the new EXE for the stock-route arm.** The `-ia` test DLL's UI queue guard logs "off" on 26577625; not on a player path. A like-for-like stock reference for the sound bar on 26577625 does not exist (S9 is a different arm). | Test gap | KC K24 (line 61); UCS 15.9 (S7 text) |
| C17 | **What I did not read.** Ticket texts of children 4720-4799 beyond their front matter and last log lines; the 0615 branch (`epic-0615-lw-2`); `research/05-graphics-composition/live-terrain.md` 11.4-11.7; queue threads other than 0042. | My limit | - |

---

## D. Where documents disagree or are out of date (for the MC)

| # | File and line | What is out of date | Against |
|---|---|---|---|
| D1 | `research/06-tooling-and-workflows/known-crash-causes.md:137` | "Epic 0615 ... and epic 0042 stage 5 (route S) are not merged." | Stage 5 merged (`a4e407ca8`, MCS "4730-4732 merged"); 0615's milestone merged 15:53 and the h18 fix 21:36 (ticket 0615 log lines 319 and 322) |
| D2 | `known-crash-causes.md:134` | "The 43-civ variant on build 26577625 through the installer: its live case is ... not yet on record." | RB:135 (P5 settled), ticket 0643 (main `18e6d3774`) |
| D3 | `known-crash-causes.md:79` | "route S is off by default" | OPT:171, 176: `ServerCombat` default 1 (in effect only with `ServerUnits 1`, default 0); UCS:4072-4073 candidate: both 1. True only because `ServerUnits` defaults 0; the sentence should say so |
| D4 | `known-crash-causes.md:80` | "`Waves_Reload` on 26577625 not yet verified live" | MCS:1903 (0615 I3 part 1 PASS on 26577625, 18:19); not enough to say it is checked: needs one line from the lead |
| D5 | `docs/RELEASE-0.1b.md:134` (row P4) and `docs/BETA-TESTERS.md:167-176` (PENDING P4) | P4 "waits for ticket 0641's merge" | Ticket 0641 merged (`5bf48a276`, state done, KC K25 says "in"): wording (a) applies, (b) can be deleted |
| D6 | `RB:104` (row O6) and `RB:105` | "epic 0615 is not merged: state ready at f88c3e05f; main has 0465's form" | Milestone merged 2026-11-21 15:53; live land/water is partly on `main` |
| D7 | `RB:102` and `RB:139-141` (5.4 first bullet) | "`docs/OPTIONS.md` ServerUnits row still says the fighters stand still" | OPT:155 now describes route S (the row was corrected) |
| D8 | `RB:141-142` (5.4 second bullet) | OPTIONS ServerFarmFields row contradicts 0637 | OPT:219 was corrected at 0655's merge; the bullet is spent |
| D9 | `RB:143-145` (5.4 third bullet), `OPT:419-422` and `OPT:453` | "The patch's installer asks you whether you want [visual additions]" / "The installer asks you about this." | `tools/installer/patch64.iss` has no such page at `main` `3fc921def` [C, grep]; only `vp64helper visual-additions` exists. One-off 9081 (installer customizations, the owner directs) may add it: check at the candidate |
| D10 | `RB:1-3, 5-8, 10-81` | Header and sections 1-4 are the cancelled take of 2026-11-19 (D200): a Friday target, a Thursday freeze, and an option table (RB:15-23) that has no `ServerFarmFields` or `ServerCombat` row | RB:3 says it is rewritten at the take (ticket 0601, D216); `UCS:4069-4080` and RB 5 hold the current set. The one place for the option set is in no committed file (C2) |
| D11 | `STATUS.md:1-14` and its epic table | "Updated 2026-11-11 12:57", "Increment 3: not started", "Playable builds: none yet" | MCS, tickets, D143/D144/D195: increment 3 is in progress, a preview installer exists (D234) |
| D12 | Tickets `tracker/issues/0590-*` (state backlog), `0588-*` (in-progress), `4730-*`, `4731-*` (ready), `4720-*`, `4722-*`, `4723-*`, `4725-*`, `4783-*` (review), `4732-*`, `4727-*`, `4728-*`, `4729-*` (in-progress) | States older than the work | MCS: migration to 26577625 is done and the popup seen live; stage 5 merged (4727-4732 carried by `ba7225ce5`, `a4e407ca8`). I checked only 4730-4732 and 4729 by `git log`; the others are [?] |
| D13 | `MCS:1935, 1938` and RB:132 (P2 text) against `BT:199-218` | BT's P2 (a) wording does not yet say "a fight can wait up to 12 s" (A2.1) or that `charge_any` is off for the candidate; RB:132's numbers are series 56eb/fe44 (charge on) | MCS 22:59; the off build changes sounds and holds, the counts must be re-read after the off series |
| D14 | `BT:136` (`[Extras]` row) and `OPT:411-577` | "all off in this release except the bugfix switch" | Every fix is itself off (OPT:475, 504, 562); read literally a tester will think a fix runs |
| D15 | `BT:263-272` ("What not to report here") | Does not name the stock behaviours of 0554, 0555, 0556, 0552 and 0560 | Tickets 0554-0556 (closed as stock, D187), 0552, 0560 (A4.8, A2.5) |
| D16 | `BT:92-93` against KC "Rows the MC must rule on" 5 (KC:116-120) | BT tells that the patch's DLL fixes do not apply with the patch off, but does not name the user-visible ones (bomber freeze, religion crash) | KC K19, K20 |
| D17 | `RB:131` (row P1) | Still carries the superseded wordings (a) and (b) beside the settled text | BT:144 (P1 settled); the row is spent once the candidate's descriptor is checked (C2) |

---

## Proposed additions to `docs/BETA-TESTERS.md` (text for the MC; I did not edit it)

For section 6 (Known issues) or section 7 (Known differences), if the MC agrees with the candidate's facts at the take:

1. **Fights that wait up to 12 seconds** (only if the candidate keeps any case; today, with `charge_any` on: yes): "Now and then a fight does not end by itself: one unit stands ready and the fight ends after 12 seconds. If it happens often, set `ServerCombat = 0` and tell us." *(Remove this line if the off build is in the candidate and its series passes.)*
2. **Missing effects in some fights**: "A destroyer's long-range fire, the splash and the sinking effect of ships are not drawn when the 64-bit program plays the fight. Set `ServerCombat = 0` to see the game's own." (UCS 15.9; by eye, A2.4)
3. **Land and water changes next to polar ice** (until part 2 merges): "A tile changed to land or water next to polar ice may be drawn only at the next turn." (A3.2; drop it when the rolling refresh is in)
4. **After a long camera jump on a map that wraps around**, "the cities and improvements may be missing for a moment; move the camera again." (A4.7)
5. **A short freeze while a game loads**: "While a game loads, the picture may stand still for two or three seconds." (A4.6)
6. In section 9, "What not to report here": "A coastal city that looks small, a tile that looks like a mountain although it is flat after a mod changed the map, a thick river that turns thin: these happen without the patch too." (A4.8)
7. In section 9 or 6: "Melee fight sounds that repeat after a fight, an atomic bomb's fight that lasts 20 seconds: both are also seen without the patch (the 20 s one) or not reproduced (the repeat); please say whether it also happens with the patch off." (A2.5, A2.6)
8. Replace the P4 block by case (a) (A5.3, D5).
9. In the Extras row: "no extra is on in this release" (D14).
10. Hardware line: "tested on NVIDIA graphics cards only; please say which card you have" (C9).
11. If the MC wants BT to carry them: the preview-only note for the owner's tonight installer (unvalidated; `charge_any` on; not a candidate).
