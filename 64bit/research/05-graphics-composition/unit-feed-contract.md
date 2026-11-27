# The unit feed contract: what the server's unit renderer receives, in state (ii-d) (N5, epic 0327)

Written 2026-11-15 03:29-03:35 EST by lead-0327 (epic 0327, item N5 of the Architect's increment 3 sheet,
`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md` sections 1a, 4.5-4.7, 5). A
contract for the code tickets (A1 and U6 of the sheet's section 7, then C1/C2 of 0042), not code. **Status: accepted
by the MC 2026-11-15 03:36 (queue #1467) as the contract for A1 (0040) and U6**, with premises F2, F3, F6 and F7 checked
first by the code ticket. This file is the single register of the feed event kinds 11-31 and the bridge kinds 6-7
(accepted as proposed); the `feed.bin` record types of 2.4 are 0301's bundle format, confirmed by lead-0301 (queue
#1469, relayed on thread 0327 as #1471). Nothing here changes the product.

**Contract revision 2026-11-16 (issue 0439, MC #2908, #2941):** kind 21 `kUnitEmbark` with a[2] = 1 carries in a[3]
the inputs of the EXE's embarked art read at the call (the row of the table below; `server/src/feed/srv_feed.h`
`EmbarkArtBits`). The event's size and every other kind are unchanged; a recording made before the revision has a[3] = 0
there, which a reader takes as "not carried" (the older rule). Was: a[3] = 0.

The state it is written for (D144; the MC's state (ii-d) note of 2026-11-15 02:14): the EXE's Unit system fully on,
its unit draws dropped by the compositor, the server drawing the units. True absence (0328) is later and optional; the
names it needs are reserved in section 7 and nothing else is designed for it.

Marks: `[C]` read in this repository's code or model files; `[L]` measured live by the issue named; `[I]` inferred or
proposed. The event and bridge kind numbers are accepted (#1467); field names and record layouts are proposals `[I]` until a code ticket takes them.

## 0. In short

The renderer receives three streams, all through the server's existing feed (`server/src/feed/srv_feed.h`, one
feed-wide sequence number), so all three are recorded in the scene bundle's `feed.bin` with no new mechanism (D137):

1. **The DLL's 25 unit and city calls** (`proxy/model/policy-table.tsv` rows 354-378), taken in the server where they
   arrive first, as feed events, plus a **unit table** read through pure getters, recorded like the plot table.
   Moves carry a **path record** and combats a **combat record** (both longer than an event's four values).
2. **The EXE's unit positions** from `LocalMachineUnitPositionChanged` and the arrival from `UnitMoveQueueChanged`,
   through the proxy's Lua bridge (0178), as dedicated bridge record kinds, with a per-frame **EXE frame mark**.
   The bridge's generic listener cannot carry them (it zeroes table arguments and truncates fractions, section 3.2).
3. **Nothing new from the compositor's side to the server**; to the compositor goes one **mode word** with a unit bit
   (section 5).

The position source is a renderer option `exe | own` (section 4). Every record carries the feed's sequence number,
which orders a DLL event against the EXE's positions (section 6).

## 1. Sources and premises

| # | Premise | Mark, source | Test before code |
|---|---|---|---|
| F1 | Every DLL-to-engine unit and city call passes through the server before the proxy forwards it to the EXE | [C] the server hosts the DLL; `policy-table.tsv` direction `dll_to_engine`, consumer `exe`/`both` | none |
| F2 | The unit getters used in section 2.2 are pure (no side effect, safe on the server's thread at Push time), as 3303 required of the plot getters | [I] | read each in the DLL fork, 15 min |
| F3 | Unit ids are unique per player and stable for a unit's life; `(player, id)` keys a unit | [I] (`ICvUnit1::GetID`, `GetOwner`; F2 saw ids like 17542) | one save: ids before and after a save / load |
| F4 | `LocalMachineUnitPositionChanged` fires in state (ii-d) as in state (i): the Unit system is on | [L] in state (i) (0317 base arm: 52 per two-plot walk, 2 per hex with quick movement, 3,122 in two AI turns for all players' units); [I] for (ii-d), never run | A1's acceptance (sheet 4.5) counts it |
| F5 | Its arguments are `(playerID, unitID, position)`, `position` a table `{x, y, z, w}` in world units | [L] 0317 section 3 | none |
| F6 | `UnitMoveQueueChanged` fires once at a walk's end (quick movement off) | [L] 0317: 1 per walk; its arguments were not logged | log its arguments in A1's run |
| F7 | A record made by the bridge on the EXE's main thread reaches the server after every DLL call that caused the move it describes | [I] causal: the EXE moves the model only after the forwarded DLL call | section 6 test |

## 2. From the DLL, unchanged (sheet 4.7 item 1)

### 2.1 Events: the 25 calls

The server pushes one feed `Event` (32 B, `srv_feed.h`) per call, after the DLL call returns to the server and before
the call is forwarded (so its sequence number precedes anything the EXE does in reaction). `plot` is the unit's plot
index at Push (`GetX`, `GetY`), `a[0]` the owner and `a[1]` the unit id for every unit kind; `a[2..3]` as listed. Kinds
continue after `kBorderChanged = 10`; the three city kinds 4-6 exist already.

| Slot (x86 = x64) | Call | Proposed kind | a[2], a[3] | What the renderer does with it |
|---|---|---|---|---|
| 146 | `GameplayUnitCreated(unit)` | 11 `kUnitCreated` | unit type, 0 | add the unit to its scene; art from the unit table (2.2) |
| 147 | `GameplayUnitMoved(unit, path)` | 12 `kUnitMoved` | path record index, path length | queue a walk along the path (visual queue C2); in mode `own` the motion itself |
| 148 | `GameplayUnitTeleported(unit, plot)` | 13 `kUnitTeleported` | target plot | place at once |
| 149 | `GameplayUnitDestroyed(unit)` | 14 `kUnitDestroyed` | 0, 0 | remove after the unit's queued visuals (combat-choreography.md 8) |
| 150 | `GameplayUnitDestroyedInCombat(unit)` | 15 `kUnitDestroyedInCombat` | 0, 0 | death after its combat plays |
| 151 | `GameplayUnitSetDamage(unit, damage, previous)` | 16 `kUnitSetDamage` | damage, previous damage | figure count (deaths from the back of the formation, `unit-formations.md`) |
| 152 | `GameplayUnitFortify(unit, on)` | 17 `kUnitFortify` | 0/1, 0 | the fortify clips (4151: 3901's warriors stood in `warrior_fortify_idle`) |
| 153 | `GameplayUnitWork(unit, build)` | 18 `kUnitWork` | build type, 0 | work animation |
| 154 | `GameplayUnitParadrop(unit)` | 19 `kUnitParadrop` | 0, 0 | paradrop visual |
| 155 | `GameplayUnitActivate(unit)` | 20 `kUnitActivate` | 0, 0 | [I] selection-time visual; meaning to be read |
| 156 | `GameplayUnitEmbark(unit, on)` | 21 `kUnitEmbark` | 0/1, embarked art inputs (0439; 0 for `on` 0) | swap to the embarked model: the art row by the EXE's rule (`unit-server-scene.md` 3.2) on a[3] = `EmbarkArtBits` read at the call: bit 0 read, bit 1 the owner's team's `ICvTeam1::CanEmbarkAllWaterPassage`, bit 2 an override, bits 8-31 the 24-bit FNV-1a hash of `ICvPlayer1::GetEmbarkedGraphicOverride` (`srv_feed.h` `EmbarkArtHash24`) |
| 157 | `GameplayUnitGarrison(unit, on)` | 22 `kUnitGarrison` | 0/1, 0 | hide or show in a city |
| 158 | `GameplayUnitShouldDimFlag(unit, dim)` | 23 `kUnitShouldDimFlag` | 0/1, 0 | none for the model (the flag is the EXE's); recorded for completeness |
| 159 | `GameplayUnitMarkThreatening(unit, on)` | none | | never called by the DLL [C, policy table "unused"] |
| 160 | `GameplayUnitVisibility(unit, visible, checkPlot, blendTime)` | 24 `kUnitVisibility` | visible + 2 x checkPlot, blendTime as float bits | visible to the active team; fade over blendTime |
| 161 | `GameplayUnitCombat(combatInfo)` -> uint | 25 `kUnitCombat` | combat record index, 0 (Q7, below; was "the returned value") | the combat's visuals (C3); the return value is the server's answer as today |
| 162 | `GameplayCityCreated(city, era)` | 4 `kCityCreated` (exists) | | cities (0037), outside units |
| 163 | `GameplayCityDestroyed(city, player)` | 5 (exists) | | |
| 164 | `GameplayCityCaptured(city, player)` | 6 (exists) | | |
| 165 | `GameplayCitySetDamage(city, damage, previous)` | 26 `kCitySetDamage` | damage, previous | city damage visuals |
| 166 | `GameplayCityPopulationChanged(city, population)` | 27 `kCityPopulationChanged` | population, 0 | city size models |
| 167 | `GameplayCityCombat(combatInfo)` -> uint | 28 `kCityCombat` | combat record index, then the attacking city's plot + 1 (0 = not known; was 0 until ticket 5062, 2026-11-23, unit-combat-server.md 18.37.3) (Q7, below; was "the returned value") | city bombard visuals |
| 168 | `GameplayUnitMissionEnd(unit)` | 29 `kUnitMissionEnd` | 0, 0 | end of a mission's visual |
| 169 | `GameplayUnitRebased(unit, from, to)` | 30 `kUnitRebased` | from plot, to plot | air rebase |
| 170 | `GameplayUnitResetAnimationState(unit)` | 31 `kUnitResetAnimationState` | 0, 0 | back to idle |

The existing city kinds 4-6 keep their layout (`srv_feed.h`: argument, owner, city id; plot -1). The new city kinds
26-28 use the unit pattern with `plot` -1: `a[0]` owner, `a[1]` city id, `a[2..3]` as listed [C, `server/src/feed/srv_feed.h`,
issue 4452]. (Corrected 2026-11-16 by W-4470 for epic 0375, the MC's C1 answer #2105 Q6 / Q7 granting the text drafted
in `unit-server-scene.md` 2.1: no pure getter gives a city's plot. Was: "`plot` = the city's plot ... [I: the code
ticket settles the four values]".)

**Amendments by the MC's C1 answer to epic 0375 (#2105, Q6 and Q7; applied 2026-11-16 by W-4470 from the text drafted in
`unit-server-scene.md` 2.1; `srv_feed.h` `Kind` comments are the reference) [C]:**

| Source | Kind | `plot`, `a[0..3]` | What the renderer does with it |
|---|---|---|---|
| the server's hook of `ICvPlayer1::SetupGraphical` (`ServerHook_ICvPlayer3_SetupGraphical`, `server/src/remote/srv_remote_main.cpp`): the first call of a load (a player id not above the previous call's; 64 calls 0..63 at every logged load), before it is forwarded and before any unit call of the load | 32 `kLoadBegin` | -1 (`turn` -1); new `unitTableGen`, that call's player id, 0, 0 | the unit table is emptied in the same lock (`SrvFeed::ResetUnitsAt`); the seed allocator's load clear (S5, `unit-server-scene.md` 4); the controller removes every held unit |
| slot 144 `GameplayEraChanged(player, era)` | 33 `kEraChanged` | -1; player, new era, 0, 0 | the player row's era is set from `a[1]` at once (published with the batch); the allocator's era window (remove + create of the units whose art changes, S3) |

- Kinds 25 / 28 (Q7): `a[3]` = 0. The event is pushed before the call is forwarded, when the value the EXE returns does not
  exist yet; the value is `CombatRecord::returned` (record 5), valid when flag bit 7 (`kCombatReturnedKnown`) is set, i.e.
  when the batch had not been drained before the forwarded call returned; otherwise counted `combatResultsLate`. The
  renderer does not use it.
- City kinds 26-28: `plot` -1 (above).
- **Proposed, not built: a mark of the DLL's update boundary** (from 4453's frame grouping, `unit-server-scene.md` 4,
  2026-11-16). The seed allocator groups the events of one drain as the EXE groups one frame's records (a creation after
  the frame's removals of other units); a drain is only the server's stand-in for an EXE frame, so an exact grouping
  wants a feed event at each end of the DLL's update (where the server's `LeaveUpdate` already re-reads changed plots,
  `srv_rt.cpp`). No kind number is reserved for it; the MC's (an N5 amendment).

### 2.2 The unit table (state, not events)

The renderer also needs each unit's state when it starts (a load, a reload, a renderer restart, the first frame of an
offline scene), not only the events since. As the plot table does (`SrvFeed::CopyPlots`), the server keeps a unit table
filled through pure getters (premise F2) at each unit event, and the renderer copies it. Proposed `UnitRecord`, 32 B:

| Field | Getter (inside the server) |
|---|---|
| `owner` i8, `domain` i8, `facing` i8 (DirectionTypes), `flags` u8 | `GetOwner`, `GetDomainType`, `GetFacingDirection`; flags: embarked (`IsEmbarked`), garrisoned (`IsGarrisoned`), fortified (from the last `kUnitFortify`; no getter on `ICvUnit1` [C]), visible to the active team (from the last `kUnitVisibility`), invisible (`IsInvisible`), delayed death (`IsDelayedDeathExported`), dead |
| `id` i32 | `GetID` |
| `unitType` i32 | `GetUnitType` |
| `plot` i32 | `GetX`, `GetY` |
| `damage` i16, `maxHP` i16 | `GetDamage`, `GetMaxHitPoints` |
| `artStyle` i16, `era` i16 | the owner's `GetArtStyleType`, `GetCurrentEra` (the unit's art variant by era and style) |
| `seq` u32 | the feed sequence number of the last event that changed the record |
| `reserved` u32 | 0: the fields above are 28 B; the record is 32 B (added 2026-11-16 by W-4470 from `srv_feed.h` `UnitRecord` [C], issue 4452; the table listed only the 28 B of fields) |

The art row, its members, `Scale`, formation and clips come from the database the server already holds
(`Units.UnitArtInfo` and the era variations, `ArtDefine_UnitInfos`, `ArtDefine_UnitInfoMemberInfos`,
`ArtDefine_UnitMemberInfos`; `model-reader.md` 3). The team colour is the owner's `PlayerColors.SecondaryColor`
(3909, `unit-draws.md` 5, branch `epic-0301-scene-harness`) [C, L].

### 2.3 Path and combat records

- **Path** (`kUnitMoved`): the `CvPlotIndexVector` as plot indices, kept in a per-batch side table; the event's `a[2]`
  indexes it. Needed by the visual queue in both modes and by mode `own` for the motion.
- **Combat** (`kUnitCombat`, `kCityCombat`): the combat information as the EXE reads it (`combat-on-the-product-path.md`
  1, `combat-choreography.md` 2.2): attacker and defender (unit `(player, id)` or city id), plot, `GetAttackerAdvances`,
  `GetDefenderRetaliates`, `GetAttackIsRanged`, `GetAttackIsBombingMission`, `GetAttackIsAirSweep`,
  `GetDefenderCaptured`, `GetVisualizeCombat`, `GetNuclearDamageLevel`, `GetDamageInflicted` and `GetFinalDamage` per
  side, `GetDamageMemberCount` and the damage members. Read inside the server before the call is forwarded (the EXE's
  handler borrows the argument, `combat-on-the-product-path.md` 4, issue 0329).

### 2.4 File form (D137)

In `feed.bin` (3902's form, `offline-scene-harness.md` 2.5 on branch `epic-0301-scene-harness`): the events are part
of every DRAIN record unchanged (32 B each). Three record types are added after 1 DRAIN and 2 PLOTS (confirmed by lead-0301, #1469: they land after 0301 is on
`main`, with `kFormatVersion` raised): **3 UNITS** (the unit table, full or diff, as PLOTS: `loopIter`, `caller`, count, then
`UnitRecord`s), **4 PATHS** (`loopIter`, then per path: event seq u32, length u32, plot indices), **5 COMBATS**
(`loopIter`, then per combat: event seq u32 and the fields of 2.3 in a fixed layout). Each is written only when its
batch has entries. The checker's rules of 2.5 apply (`loopIter` never goes back, sizes agree with counts).

## 3. From the EXE, new (sheet 4.7 item 2)

### 3.1 What is taken

- `LocalMachineUnitPositionChanged(playerID, unitID, position)`: a position record `(player, unit, x, y, z, EXE
  frame)`. It is a world position of the unit (the EXE sets the flag 35 above it, hand-over designs 6.1) [L, C].
- `UnitMoveQueueChanged(...)`: an arrival record `(player, unit, EXE frame)` (premise F6 for its arguments).

### 3.2 Why the generic bridge record does not carry them [C]

The bridge's mirror record (`proxy/src/proxy_ui_bridge.h`, `Record`, 32 B: version, kind, flags u32, `a[4]`, seq) sets
`a[i] = 0` for a table argument (`kFlagNotNumber0`) and truncates a fraction toward zero (`kFlagFraction`). The position
is a table of fractional floats, so a generic listener on `LocalMachineUnitPositionChanged` would deliver
`(player, unit, 0)`.

### 3.3 Proposed records

Two new bridge kinds after `kKindUnitMovingCalled = 5` (proposed, `proxy_ui_bridge.h`):

| Kind | Made by | flags | a[0] | a[1] | a[2] | a[3] |
|---|---|---|---|---|---|---|
| 6 `kKindUnitPosition` | a listener that reads `position.x/y/z` from the table | bits 16-23 = player, bit 24 = arrival | unit id | x as float bits | y as float bits | z as float bits |
| 7 `kKindFrameMark` | the bridge's `LocalMachineAppUpdate` listener, once per EXE frame that made any record | 0 | EXE frame number (the compositor's Present count) | number of records that follow | 0 | 0 |

The EXE frame of a position record is the last `kKindFrameMark` before it. An arrival is a `kKindUnitPosition` with bit
24 set and the unit's last position. Volume: the 3,122 position events of two AI turns (0317) are 100 KB at 32 B; a
frame of 100 walking units is 3.2 KB [arithmetic]. They reach the server once per frame as the bridge's records do
today (`kUiMirror`, `srv_feed.h`), so their file form is the DRAIN record's events with no change.

**As built (issue 4751, epic 0040, 2026-11-17):** the two kinds are in `proxy/src/proxy_ui_bridge.h` as proposed:
`kKindUnitPosition = 6`, `kKindFrameMark = 7` (`kKindCount` 8), with the field comments of the table above and the
kind-6 flag constants `kPosPlayerShift = 16`, `kPosPlayerMask = 0xFF << 16`, `kPosArrival = 1 << 24`. The listeners
that make them and the server's mapping of player and arrival into the feed event are issue 4754's.
**Built by 4754 (2026-11-17, merged into the team branch epic-0040-server-units; corrects the 4751 line "nothing makes
these records yet"):** the bridge's own listeners make kind 6 from `LocalMachineUnitPositionChanged` and, as the
arrival, from `UnitMoveQueueChanged(playerID, unitID, bRemainingMoves)` when `bRemainingMoves` is false (F6 [L]: four
calls, all (number, number, false)); kind 7 is made once per drain that made records, its frame number the drain count
of the current generation (the proxy maps no shell section). The server keeps kind 6's player in `Event.plot` and the
arrival in `Event.turn` (1 arrival, 0 position). A quick move fires no `UnitMoveQueueChanged`, so it has no arrival
record (F9's input for 4755). Live check r1 (c5h-a, Tiny copy, product path, positions-only install): server counts
equal Lua's in every window (654 positions, 4 arrivals). Details: `research/03-dll-engine-boundary/proxy-design.md`
24.6, evidence `server/evidence/0040/4/r1/`.

## 4. The position source as a mode (sheet 4.7 item 3)

A renderer option `unit_positions` (in the patch options file and in a bundle's `scene.json`), values:

- `exe`: the first product. A unit's root position is the newest `kKindUnitPosition` for it; between records the unit
  stands where the last one put it, and an arrival snaps it to its plot. The model may lag the flag by one frame [I].
- `own`: the server places the unit itself: at its plot, and along the path record of `kUnitMoved` at the EXE's walk
  speed (the member's `MoveRate`; 0328's work, sheet 7 A2). Offline scenes without position records use `own`.

**As built (issue 4755, epic 0040, 2026-11-17; `unit-server-scene.md` 17):** the option is server.cfg
`render_units_positions` (own default | exe; in a bundle's `scene.json` `render_config`, which unitfeed reads); in the
patch options file it is not a key of its own (4759's `[Renderer] ServerUnits` will set exe). Mode exe as above, with two
additions: an arrival snaps the unit to its plot centre, and a walking unit stands at the plot it left until its walk's
first position (the DLL moves the record to the destination before `GameplayUnitMoved`). The controller's `move` comes
from kind 12, its `arrive` from the arrival record in a later frame, or from F9's rule (a walk with no position for 30
drawn frames, 120 before its first) [C]. A quick move is kind 13 per plot (the DLL's `SetPosition`), so it opens no walk
and needs no arrival [C, the DLL fork at the pin]. The lag of the model behind the flag is not measured.

The feed is the same in both modes; 0328 switches the product to `own` without a feed change. In either mode the
figures stand in their formation slots around the root (`unit-formations.md`) and face by the unit's facing
(yaw 30.000 degrees for the default facing on every measured figure, 4151 and 4153).

## 5. To the compositor: the mode word (sheet 4.7 item 4)

One u32 in the shell's shared block (`shell/common/shell_shared.h`), written only by the server, latched by the
compositor once per frame at Present:

| Bit | Meaning when set | Now |
|---|---|---|
| 0 | the server draws the units: drop the EXE's unit draws | A1 |
| 1 | the server draws the cities: drop the EXE's city draws | reserved (0037) |
| 2 | the server draws the landmarks | reserved (0037) |
| 3 | the server draws the path models | reserved (0300) |
| 4-31 | 0 | free |

The server sets bit 0 only while its unit scene is ready and its renderer state is OK, and clears it on any failure
or restart (sheet 4.5). **The MC's ruling on D118 (2026-11-15 03:36, queue #1467):** a short interruption that the
compositor already covers by holding the last server frame keeps bit 0 set (the held server frame shows the units; the
EXE's units stay dropped); a real failure of the server's renderer goes to D118's error popup and diagnostic report.
There is no lasting pass-through mode with the EXE's units: pass-through is allowed only between detecting the failure
and the popup. *(Replaces the draft's "so the fallback is the EXE's units" and its open question on D118.)*

**As built (issue 4751, epic 0040, 2026-11-17):** the word is `VPShellMode::word` at `VPSHELL_MODE_OFFSET` = 512 of
the main section (`shell/common/shell_shared.h`, in unused zero-filled bytes after the 376-byte header; version stays
3), bits `VPMODE_UNITS` 0x1, `VPMODE_CITIES` 0x2, `VPMODE_LANDMARKS` 0x4, `VPMODE_PATHS` 0x8 as the table above. The
64-byte block also carries the writer's pid (written before the word: the compositor uses the word only when it equals
the header's `serverPid` and the heartbeat is fresh), a change counter, a readiness-loss counter, the QPC of the last
change, and the compositor's latched word and drop counters. Field list and owners:
`research/05-graphics-composition/unit-draw-filter.md` 2.2. The compositor's hold of the last server frame has no
time limit in the product (`hide_when_stale=0`), so O5's bound on a lost readiness is 2 s (unit-draw-filter.md 2.4).

**The recogniser's shader set is larger than material 90 [C, 3909].** The catalogue's units are drawn by
`UnitShader_Skinned_CODE` (material 90, three passes), one ship by the alpha set (material 89) and non-skinned unit art by
`UnitShader_CODE` (material 87). 4152 found one more draw over the selected unit (VS `da2382af` / PS `8d853988`,
premultiplied, not in the material table) [L]. Premise S1 of sheet 4.5 has to take the set from the catalogue, not
from 3901's one capture (the MC puts this set into 0040's premise S1, #1467).

## 6. One order across both sources (sheet 4.7 item 5)

Every DLL event and every bridge record enters the server's feed through `SrvFeed::Push`, which assigns the feed-wide
`seq` (`srv_feed.h`) [C]. That is the order the visual queue (C2) uses: a combat event with seq 1000 holds the
positions with seq > 1000 of its two units until its visuals have played. Premise F7 says the arrival order is the
causal order; its test: in one recorded walk with quick movement off, every `kKindUnitPosition` of the unit has a
higher seq than its `kUnitMoved`, and the first arrives within two EXE frames of it. The bridge record's own `seq`
(per proxy process) and the EXE frame number stay in the record for diagnosis.

**Answered (issue 4755, 2026-11-17, run r1 on c5h-c; `unit-server-scene.md` 17.4) [L]:** F7 held on a recorded two-plot
walk with quick movement off: kind 12 seq 223, the unit's first kind-6 position seq 225, no position of the unit before
the kind 12 since its creation; the first position came **1 EXE frame** after `SerialEventUnitMoveToHexes` (Lua frame
count) and in the kind 12's own drained batch. One addition: the EXE posts one more position after the walk's arrival
record (`UnitMoveQueueChanged` before the last `LocalMachineUnitPositionChanged` of the same frame), so the arrival is not
the walk's last record. The server counts a position whose seq is below its walking unit's kind 12 (`before_move`): 0.

## 7. Reserved for 0328, not built now (sheet 4.7 item 6)

Names only; no layout is fixed here:

- `VPShellUnitAnchors`: the anchor table of hand-over designs 6.3 (`(player, unit, x, y, z, moving or arrived)` per
  server frame), in the shared block.
- Raised events (the five that stop with the Unit key, 0317): `LocalMachineUnitPositionChanged`, `UnitHexHighlight`,
  `UnitMoveQueueChanged`, `SerialEventUnitSetDamage`, `SerialEventUnitDestroyed`, raised by the bridge with `Dispatch`
  (0178's limit).
- `kUnitSoundTrigger`: the server's trigger moments (`.ftsxml` Sound triggers, `model-reader.md` 6.4 (d)) to the EXE's
  audio.

## 8. What the renderer takes from 0327's results beside the feed

- Mesh: the EXE's 28-byte stream from the `.gr2` by one rule (`unit-playback.md` 3, 4152) [L].
- Pose: one clip at one t per figure, palette `[Rz(yaw) R_k | s Rz(yaw) t_k + T]` with s the member's `Scale`
  (`unit-playback.md` 1, 4151) [L]; the fortified state plays the fortify idle clips [L].
- Placement: `UnitFormations.xml` slots with a per-unit seed (`unit-formations.md`, 4153) [C, L]; the seed's source is
  4153's wave 2.
- Clock: clip time advanced 0.200 s per captured frame while the captures were 1.25-1.47 s apart, so the EXE's frame
  step is probably clamped at 0.2 s (K6, not tested) [I].

## 8a. The renderer's side: the module input (epic 0041 C1, accepted by the MC 2026-11-15 14:08, queue #1845)

The renderer's unit module `c5r::units` takes one frame's unit list `UnitScene` (`render/include/c5r/units/unit_scene.h`) and has a file form, the unit-scene file `.c5units`. Its fields, the
field-by-field mapping from this contract's `UnitRecord`, position records and art tables, and the file form are in
`unit-renderer.md` sections 2-4; U6 (ticket 0375) maps the feed onto it with no change to the kinds or records above.
Two inputs the feed does not carry yet, both U6's to close [I]: the **formation seed** (the EXE's art-record index,
`unit-formations.md` 12.1: U6 keeps one allocator with the EXE's rule over `kUnitCreated` and `kUnitDestroyed*` in feed
order) and the **player's colour Type** (`PlayerColors.Type`, for header row 0; one player record per player in the
bundle, or the save's player slots). The unit art row is resolved by the module from `unitType` and the owner's art
style and era (`unit-renderer.md` 6.1).

**Closed 2026-11-16 (epic 0375, U6; recorded by W-4470) [C, L]:** both gaps are closed without a new event kind. The
**seed** is derived in the server by the allocator `srvunits::SeedAllocator` (`server/src/units/srv_units_seed.{h,cpp}`,
issues 4453, 4462) from the creation, removal, embark, combat, load and era-change events in feed order (`unit-server-scene.md` 4; checked against
the EXE's record index by the test-only reader, 4458 / 4465, 12.1-12.2), so a recorded bundle reproduces it. The
**player's colour Type** comes from the player table, `feed.bin` record **6 PLAYERS** (`SrvFeed::PlayerRecord`, 32 B:
colour index, civilization, era, art style, team, the active team; the MC's decision 2, #2105; `unit-server-scene.md` 2.4,
11.1) through `PlayerColors` (row 0 bit-equal to stock, `unit-server-scene.md` 5). The bundle's records 3-6 are format
version 4 (`offline-scene-harness.md` 20).

## 8b. The animation controller's input (epic 0374 C1, accepted by the MC 2026-11-15 #2000)

The controller of U5 (`unit-controller.md`) takes per frame a time step and the unit events that arrived since the
previous frame, in feed order (section 6), in renderer terms; its file form is `.c5anim` format 1
(`unit-controller.md` 2). It consumes: the unit table (record 3) at load as `create` events; kinds 11-18, 20-22, 24,
30, 31 one-to-one (`create`, `move`, `place`, `destroyed`, `damage`, `fortify`, `action work`, `action activate`,
`embark`, `garrison`, `visibility`, `reset`); kind 19 as `action paradrop` [I]; the arrival bit of
`kKindUnitPosition` as `arrive`; the frame mark as the frame's `exe_frame`. It does not consume kind 23 (the flag is the
EXE's), the city kinds, or kind 25 directly: a combat (kind 25 with record 5) reaches it only as the requests 0042's
choreography makes from it (`action attack` and its kin per figure set, `kill`, `victory`). Kind 29 is open [I]. The
full table is `unit-controller.md` 3. Nothing here changes the kinds or records above.

**Gap for 0042 (MC #2017, 2026-11-15):** the controller's `steer` event carries path options 7 (a blend weight) and 8
(left / centre / right), which only the EXE writes (turns while running, a ship's broadside side, aim layers;
`unit-controller.md` 6.3). Nothing in this contract carries them yet: the feed (or 0042's own turning model) must
supply them before 0042 can drive turns and aims.

**Updated 2026-11-16 00:53 (lead-0374, from 4404 B and 4405 B):** what the built bridge does with these kinds
(`unit-controller.md` 14.1): `damage` takes the figures that fall by the 0x4B4B80 rule; 4405 B's capture Q7 shows the
EXE removes them with no death clip when the damage comes outside combat [L, one unit] (premise change 5 to the MC,
#2141; until the answer the bridge's default is unchanged). `action work` is refused as `BadEvent` (no source table for
the work clips). `fortify` on a non-active player's unit did not start a fortify clip within the active turn in 4405 B
(the mission does not run until that player's turn), so the fortify mapping stays [I]. (Heading: was "draft
2026-11-15, before the MC's review".)

## 8c. Extras kinds 1000-1999: messages the DLL sends itself (ticket 4932, epic 0454 X2, 2026-11-19)

Not one of the 25 calls of 2.1: the DLL built with `VP64_RENDER_EVENTS` (fork branch `x64-render-events`) calls
server.exe's export `VP64RenderEvent` after a state change the EXE interface never carries, and the export pushes the
event into the same feed, ordered by `seq` with every other event. Unit keys are the unit kinds' `(owner, id)`.

| Kind | plot | a[0] | a[1] | a[2] | a[3] |
|---|---|---|---|---|---|
| 1000 `kUnitActivityChanged` | the unit's plot (-1 none) | owner | unit id | new `ActivityTypes` (0 awake, 1 hold, 2 sleep, 3 heal, 4 sentry, 5 intercept, 6 mission) | old; == a[2] when sent at the unit's setup for display (a load or a creation) |
| 1001 `kUnitCarrierChanged` | as 1000 | owner | unit id | transport unit id (-1: no longer cargo) | transport owner (-1) |

- Sent from `CvUnit::SetActivityType`, `setTransportUnit`, `setupGraphical` (the state at setup; only with graphics)
  and the upgrade's direct copy of the activity [C, fork 1fd51353e + addd57b8b].
- The unit scene (`srv_units_core.cpp`) skips them (`IsUnitKind`), so they do not end 0502's load-setup run although
  site 3 sends them inside it; a consumer that makes them unit kinds adds them to `LoadSetupGoesOn` [C].
- E7 is the consumer's: the DLL sends every unit's change; only what the active player may know is shown.
- Contract, server side and evidence: `research/03-dll-engine-boundary/render-events.md`.

## 9. Not verified

- F2 (pure getters), F3 (id stability), F6 (the arguments of `UnitMoveQueueChanged`), F7 (order) are untested.
  *(2026-11-17: F6 answered by 4754 [L], `(player, unit, false)` at a walk's end (3.3); F7 answered by 4755 [L] on one
  recorded walk (6). F9, the arrival with quick movement on, settled by 4755 [C, L]: a quick move is one kind 13
  (`SetPosition`) per plot and never a kind 12, so it opens no walk and needs no arrival (`unit-server-scene.md` 17.2).)*
- `LocalMachineUnitPositionChanged` in state (ii-d) (F4): measured only in state (i).
- The meaning of `GameplayUnitActivate` and `GameplayUnitMissionEnd` for the picture.
