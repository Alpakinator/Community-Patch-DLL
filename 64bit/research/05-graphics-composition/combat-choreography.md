# Combat choreography as the EXE plays it (issue 0321)

Queue ticket 0321 (increment 3, package C3, the description half; the Architect's increment 3 sheet,
`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md` sections 5 and 7, item N9 of
1a). This file covers who moves when, the clip order on each side, the trigger and hit moments, the end of a combat,
ranged, melee, city and area attacks, and what Quick Combat changes. The MC's note of 2026-11-15 00:56 asks for one
more thing: how the stock EXE queues several combats on one unit, the order they play in, and how far the picture
falls behind the game state (C2, the visual queue per unit). Sections 3 and 4 answer that.

This is a description only (D83). Nothing from the EXE is committed: no decompiler output, no bytes, no tables
copied out of its data. The code that re-implements it is a later ticket, written from this text.

Written 2026-11-15 01:54-02:35 EST, by an account-B implementer. Marks: `[C]` read in code (the DLL's source, or the
EXE's machine code through a decompiler or a disassembler), `[D]` read in the install's data files or in the game
database, `[I]` inferred, with the reason given. Nothing here was run live. The live tests are listed in section 10.

Sources read:
- **The DLL:** the x64 pin 04b1211b8 (`deps/dll.lock`): `CvUnitCombat.cpp`, `CvUnit.cpp`, `CvPlayer.cpp`,
  `CvDllCombatInfo.*`, `CvGameCoreStructs.h` (`CvCombatInfo`), `CvGameCoreDLLUtil/include/CvStructs.h`
  (`CvCombatMemberEntry`), `CvDllInterfaces.h` (interface slot order).
- **The EXE:** `CivilizationV_DX11.exe`, the analysis copy of
  `research/06-tooling-and-workflows/reverse-engineering-setup.md` (SHA-256 b2ce96ae..., PE time stamp 0x546CD0A8).
  It was read through a read-only headless Ghidra 12.1.4 pass on a scratch copy of the `civ5` project, with
  llvm-objdump 19 alongside. Addresses are DX11 virtual addresses (VAs) at ImageBase 0x400000.
- **Data:** the install's `Assets/Units/UnitFormations.xml` (SHA-256 bf164b8a...) and
  `Assets/Units/Civ5ArtDefines_UnitMemberTemplates.xml` (915a17f1...). The art tables come from a copy of c5h-b's
  `Civ5DebugDatabase.db` of 2026-11-15 01:55 (SHA-256 90075fa4..., Vox Populi loaded). The stock and EUI Lua
  (`UI/InGame/UnitFlagManager.lua`, `DLC/Expansion2/UI/InGame/UnitFlagManager.lua` and `CityBannerManager.lua`,
  `DLC/UI_bc1/...`) was read in the Steam install.

## 1. In short

1. **The DLL decides everything before the EXE draws anything** `[C]`
   (`research/03-dll-engine-boundary/combat-on-the-product-path.md`, 0316). With Quick Combat off the DLL calls
   `GameplayUnitCombat` / `GameplayCityCombat` with the result already computed: final damage, kills and capture.
   It then resolves at once, and that sends the damage, death and advance calls for the same units straight after.
2. **The EXE turns each engine call into a message on its event queue** `[C]`. `GameplayUnitCombat` posts
   `RunCombatSim` (type 0x59) and `EndCombatSim` (0x5A) back to back, and returns the first one's event id; the
   DLL passes that id to its combat messages as their parent.
3. **The queue is what makes the picture run behind the game** `[C mechanism, I semantics]`. Each message carries
   a 13-word header naming the units it concerns and a few flags. A message waits while an earlier message that
   names the same unit is still held. The combat simulation holds the `RunCombatSim` record for as long as it plays,
   so `EndCombatSim` waits behind it, and so do the unit's `SetDamage`, `DestroyedInCombat` and `Moved` messages
   and any later combat of that unit. Messages about other units pass. One flag makes a combat a barrier for every
   later message: a combat that the local human player watches outside their own turn (the AI turns), or a nuclear
   attack.
4. **A combat plays for at most 12 s, or 20 s when an air unit takes part** `[C constants, L]` (corrected 2026-11-17 by
   0477, section 13.2; was: "20 s when a side has a projectile weapon": a battleship's ranged strike had the 12 s limit). When
   the limit is reached every member is forced to finish. That is the EXE's only catch-up rule found.
5. **The choreography is a per-figure state machine inside `cvCombatSimSystem`** `[C structure, I meaning]`.
   It has up to 68 participants (attacker, defender, interceptor, then the damage members of an area attack), each
   with up to 32 figures. Every figure finds a target among the other side's figures, approaches (run, run-charge),
   attacks with an action code chosen by its weapon and the target (attack, charge attack, attack city, bombard,
   anti-air), dies (1200) when its side's visible losses call for it, and ends in idle, victory (1800, by chance) or
   dead. Section 5 has the table of states and action codes.
6. **Quick Combat on removes the simulation altogether** `[C]`. The DLL makes no combat call, so there is no
   `RunCombatSim` and no figure duel. The damage and death messages arrive at once, and the unit model shows them
   by itself.
7. **On the product path today (Combat key off) the simulation never starts** `[C]`. `RunCombatSim` returns at
   once, the record is not held, and `EndCombatSim` follows in the next frame. That is why 0316 saw no stall.

## 2. Inputs from the DLL

### 2.1 What a visualised attack sends, in order [C, pin 04b1211b8]

`CvUnitCombat::Attack` (melee unit against a unit), with Quick Combat off and the defender killed. Each line is an
engine call or a state change, in program order, all inside one DLL call:

| # | DLL step | Engine call (`ICvEngineUtility1` slot, x86) | EXE handler |
|---|---|---|---|
| 1 | `lookAt` the defender's plot, if the target plot is visible to the active player | `ICvUserInterface` `lookAt` | camera |
| 2 | `setVisualizeCombat(isTargetVisibleToActivePlayer)` | - | - |
| 3 | combat call with the combat information | `GameplayUnitCombat` (161) | 0x67DD90: posts 0x59 + 0x5A, returns the event id |
| 4 | `setCombatUnit` on both units (combat focus for the active player's attacker) | `SetCombatFocus` | UI |
| 5 | `ResolveCombat`: restore visibility of attacker, defender and interceptor | `GameplayUnitVisibility` (160) | 0x444040 |
| 6 | defender `changeDamage` -> `setDamage` | `GameplayUnitSetDamage` (151) | 0x43BEB0: posts 0x48 |
| 7 | the defender is dead: `kill(true)` = delayed death, no engine call yet | - | - |
| 8 | `GameplayUnitDestroyedInCombat(defender)` (the attacker's own damage goes out as in step 6) | slot 150 | 0x58C9D0: posts 0x47 |
| 9 | `AddMessage(uiParentEventID, ...)`: the combat text, with the event id from step 3 as its parent | UI | message log |
| 10 | `setCombatUnit(NULL)` both; attacker `ClearMissionQueue(false, GetPostCombatDelay())` (1 visualised, 110 quick) | - | - |
| 11 | advance: `UnitMove(targetPlot, true)` queues the plot, then `PublishQueuedVisualizationMoves` | `GameplayUnitMoved` (147) | 0x579080: posts 0x4D |
| 12 | next `CvGame::update` -> `CvPlayer::updateTimers`: `doDelayedDeath` deletes the defender (`!isFighting() && !IsBusy()`) | `GameplayUnitDestroyed` (149, from `~CvUnit`) | 0x68F260: posts 0x46 |

- Step 12 runs within one game update, a few milliseconds later. The DLL keeps no busy state for the combat: 0316
  measured `IsBusy`, `IsFighting` and `IsInCombat` false at every sample from 0.13 s on.
- **Ranged** (`AttackRanged`): the same steps without the advance. Against a city it calls `GameplayCityCombat`
  (167, 0x48CCA0) and `setCombatCity`. **City attacking a unit** (`CvCity.cpp` 33140-33158): `GameplayCityCombat`.
  **Air, air sweep, nuclear**: `GameplayUnitCombat` or `GameplayCityCombat` in `AttackAir`, `AttackAirSweep`,
  `AttackCity` and `AttackNuclear` (`CvUnitCombat.cpp` 3879, 3916, 3979, 4093, 4230). A nuclear or area attack
  fills the damage member list (up to 64 entries for the nuke).
- **Fire support** (`Attack`, VP): before a melee attack the DLL looks for a defending artillery unit
  (`GetFireSupportUnit`). If it finds one, that unit's `AttackRanged` against the attacker runs first. Its result is
  `ATTACK_QUEUED` when visualised: the attacker's mission is flagged `MOVEFLAG_NO_DEFENSIVE_SUPPORT` and waits for
  the supporting unit (`CvUnitMission::WaitFor`). The melee attack follows when the mission runs again. So one
  order can give two combat calls in a row: the support fire on the attacker, then the melee.
- **Several combats on one unit** come from the DLL in the order it resolves them: a blitz unit's attacks, several
  attackers on one defender in one AI turn, fire support, interception. Each is resolved before the next starts.
  The engine gets them as a strict sequence of calls `[C]`.

### 2.2 The combat information the EXE reads [C]

`ICvCombatInfo1` slots (`CvDllInterfaces.h` order, confirmed by the getters 0x67DD90 calls and by 0316's proxy log):
2 `GetUnit(BattleUnitTypes)`, 3 `GetCity`, 4 `GetPlot`, 5 `GetAttackerAdvances`, 6 `GetVisualizeCombat`,
7 `GetDefenderRetaliates`, 8 `GetNuclearDamageLevel`, 9 `GetAttackIsRanged`, 10 `GetAttackIsBombingMission`,
11 `GetAttackIsAirSweep`, 12 `GetDefenderCaptured`, 13 `GetDamageInflicted`, 14 `GetFinalDamage`,
15 `GetDamageMember(i)`, 16 `GetDamageMemberCount`. The EXE copies each damage member as 9 words (36 bytes, the
size of `CvCombatMemberEntry`: player, id, flags with bit 0 = city, damage, final damage, max hit points, x, y,
combat type, era, weapon type, weapon option).

The EXE builds a message of 0x144 words from these. The parts used below are: one 36-byte record in the
`CvCombatMemberEntry` layout each for the attacker (words 1-9), the defender (words 10-18) and the interceptor
(words 0x13-0x1B), the damage member count (word 0x1F) and the damage member records (from word 0x20), the
visualise byte (byte 0x500; that it holds `GetVisualizeCombat` is `[I]`, from the getter's place in the handler)
and two more flag bytes (0x501 and word 0x143). For every participant the simulation is given **damage percent =
final damage x 100 / max hit points**: the int16 at record + 0xC times 100, over the int16 at record + 0xE
(`0x6AE910(side, unit, percent)`, `0x4024F0` for a city) `[C]`. That percent sets the side's visible losses
(section 5.4).

The Lua event `RunCombatSim` carries attacker player, unit id, damage, final damage, max hit points, the same five
for the defender, then `bContinuation`. The EUI city banner version also gets the attacker's and defender's x and
y (`DLC/UI_bc1/CityBanners/CityBannerManager.lua` 1468) `[D]`. `EndCombatSim` carries the same values without
`bContinuation`.

### 2.3 What the DLL's resolve means for the picture [C]

The game state at the end of the DLL call is final: the loser is a delayed death and is deleted in the next
update, the winner stands on the target plot, and the damage is applied. Everything the picture shows after that
moment is history, played from the messages the EXE queued.

## 3. The EXE's event queue: why the picture runs behind

### 3.1 The messages of a combat [C]

Every engine call above becomes a record on the per-context queue of event manager B (`0x2A10E08 + 0x4C * index`,
the context index at `[0x2A10200]`; the same queue as `GameplayNaturalWonderRevealed`, 0316 section 4). A record has
a size word, a type word, then a 13-word header (record + 0x10), then the payload. The payload's first word is
replaced by an event id taken from a counter that only increases (`InterlockedIncrement` on queue + 0x28).

| Type | Lua name | Posted by | Size |
|---|---|---|---|
| 0x46 | (unit destroyed) | `GameplayUnitDestroyed` 0x68F260 | 0x70 |
| 0x47 | `SerialEventUnitDestroyedInCombat` `[I: name order]` | `GameplayUnitDestroyedInCombat` 0x58C9D0 | 0x70 |
| 0x48 | `SerialEventUnitSetDamage` `[I: name order]` | `GameplayUnitSetDamage` 0x43BEB0 (only once the game is final-initialised, `ICvGame1` slot 45) | 0x80 |
| 0x4D | (unit moved along plots) | `GameplayUnitMoved` 0x579080 | 0x490 |
| 0x59 | `RunCombatSim` | `GameplayUnitCombat` 0x67DD90 | 0x560 |
| 0x5A | `EndCombatSim` | the same call, right after 0x59, same header and payload | 0x560 |

The names of 0x59 and 0x5A are confirmed by their Lua binders: the binder of the string `RunCombatSim` (0x5264A0)
binds type 0x59 and the one of `EndCombatSim` (0x785F50) binds 0x5A, both through 0x4A2210 `[C]`. The 0x46-0x4D
names follow the order of the binder table and were not checked one by one.

`GameplayUnitCombat` returns the 0x59 record's event id. That is the DLL's `uiParentEventID` for `AddMessage`, so
the combat text is tied to the combat event `[C]`.

### 3.2 Hold, wait and pass [C for the code paths, I for the meaning of each header word]

- **Hold.** Header word 11 (record + 0x3C) is a hold counter. 0x797E70 finds a record by its event id.
  0x4BA9A0 takes a hold (the combat simulation stores the id at sim + 0x34 and increments the counter) and lets
  the previous one go. 0x488B30 takes and drops one around its deferral. The garbage collection of the dispatched
  buffer (0x457060) erases only records whose counter is 0. A held record stays in the buffer and is not
  dispatched again: the dispatch cursor (0x6DE5D0) starts after the kept records.
- **Wait.** The pump (0x785AF0, under a compare-exchange guard) first moves records from the write buffer to the
  dispatch buffer (0x4AC910). When the queue's ordering byte (+0x30) is set, a record is moved only if no earlier
  record blocks it (0x60EB10). 0x60EB10 checks it against every record still kept (held) in the dispatch buffer,
  then against every record ahead of it in the write buffer, with the predicate 0x727A30. An earlier record blocks
  a later one when the earlier one's flags (header word 12) have bit 0 set and either:
  - the earlier record's flags have bit 1 set (a barrier: it blocks everything behind it); or
  - the later record's flags have bit 1 set; or
  - they share a key: header words 0 and 1 cross-compared, the three key pairs at words 2/4/6 cross-compared
    pairwise (0x684380 compares two words, the unit's owner and id `[I]`), or the pair at words 8/9; or
  - the later record's word 10 (a parent event id) is the earlier record's event id.
  *Corrected 2026-11-17 by ticket 4672 (`../08-performance/turn-time-study.md` "What the drain is made of") [C]: 0x60EB10
  checks against every record in the dispatch buffer, which right after the GC holds the held records **and the
  records moved earlier in the same pass**; so two records sharing a key never move in the same frame, held or not.
  Was: "against every record still kept (held)".*
- **Pass.** A record that is not blocked is moved on and dispatched this frame, even when a record before it
  waits. A blocked record stays in the write buffer in its place. Records behind it with flags bit 2 stop the walk
  (strict order after anything blocked); bit 3, or a blocked record with bit 1, ends the move for this frame.
- **Deferral.** 0x488B30, the Unit system's listener for 0x59, re-posts the message to a second queue (0x86B0B0,
  0x530-byte records) when bit 0 of its own +0x128 is set, holding the event meanwhile. Otherwise it calls the
  simulation (0x731FF0). What sets +0x128 was not found.

### 3.3 The combat record's header [C]

0x67DD90 fills the 0x59/0x5A header as follows. The flags start at 1 (bit 0: this record blocks records that share
its keys). If the active player (`ICvGame1::GetActivePlayer`, slot 3, then `GetPlayer`, slot 2) is human (slot 6)
and alive (slot 20), the flags become 0x11. If, on top of that, it is not that player's turn (`IsTurnActive`,
slot 22, false), they become 0x13: **bit 1, the barrier**. If `GetNuclearDamageLevel() > 0` the flags get bit 1 as
well. The slot numbers are those of `ICvPlayer1` in `CvDllInterfaces.h`; that the object is an `ICvPlayer1` is
inferred from the call chain `[I]`.

The header words, by the stores in 0x67DD90 (stack offsets checked against the final `lea` of the header)
`[C]`:

| Word | 0x59 / 0x5A combat | 0x48 SetDamage (0x43BEB0) |
|---|---|---|
| 0 | a value from the attacker's x and y through slot 9 of a map object, read as its plot index `[I]` | -1 |
| 1 | a value of the combat plot (`ICvPlot1` slot 53), read as the target plot's index `[I]` | -1 |
| 2, 3 | attacker: owner (`ICvUnit1` slot 3), id (slot 2) | the unit: owner, id |
| 4, 5 | defender: owner, id | -1, 0 |
| 6, 7 | interceptor: owner, id | -1, 0 |
| 8, 9 | -1, 0 | -1, 0 |
| 10 | 0 (no parent) | 0 |
| 11 | hold counter, 0 at posting | 0 |
| 12 | flags 1, 0x11 or 0x13 (+2 for a nuke) | 0 |

So `SetDamage` blocks nothing itself (flags 0), but it waits behind any blocking record that names its unit. Two
combats also wait for each other when words 0 and 1 match, which happens when they share a plot `[I]`.
`DestroyedInCombat` (0x47, 0x58C9D0) sets flags 1 and its unit's key `[C for the store of 1, I for the word]`, so
a death also blocks that unit's later messages until it has been dispatched.

### 3.4 How several combats on one unit play [C mechanism, I sequence]

Put together, for one unit U that fights twice in a row (combat A, then combat B), the EXE sees:
`RunCombatSim A`, `EndCombatSim A`, `SetDamage` for U and its opponent, perhaps `DestroyedInCombat` and `Moved`,
then `RunCombatSim B`, `EndCombatSim B`, and so on. Everything has been posted within one or a few game updates.

1. Frame n: `RunCombatSim A` is dispatched. The simulation starts and holds A's record.
2. From frame n+1, every record that names U or A's opponent waits behind the held A: `EndCombatSim A`, the
   damage, the death, the advance, and `RunCombatSim B` with everything behind it. Records about other units pass.
3. When A's simulation ends, its destructor drops the hold (4.2, `[C]`). The collection erases A's record, and the waiting records move on
   in posting order: `EndCombatSim A` (the flags come back), the damage (the bar moves), the death (the model's
   death and removal), the advance (the winner walks onto the plot), then `RunCombatSim B`, which starts the second
   simulation.
4. **Order:** per unit, the order of the DLL's calls is kept, because each record waits for the earlier records
   that share a key. Across units it is not kept: a combat elsewhere plays at the same time unless a barrier is up.
5. **During the AI turns** (the local human is alive and it is not their turn), every visible combat is a barrier.
   Combats play strictly one after another, and every later message waits too, movement of uninvolved units
   included. During the human's own turn only the units involved wait.
6. **How far behind:** the delay of a unit's picture is the sum of the play times of its combats still queued,
   each at most 12 s (20 s with an air unit, section 13.2; corrected 2026-11-17, was "with a projectile weapon"). The game state is already final at frame n. No
   limit on the queue's length and no skip-ahead rule other than the per-combat time limit were found `[C for
   what was read; I that there is none elsewhere]`. During the AI turns the barrier makes the delay the sum over
   all visible combats, not only one unit's. This matches the stock game's slow AI turns with Quick Combat off
   `[I]`.

### 3.5 The outside hold (S-H, ticket 4796)

Premise H1 of the Architect's route S note (`../07-architecture-decisions/architect-2026-11-20-route-s-combat.md` 3.1):
an outside holder (the proxy) takes the counted hold of 3.2 on a visualised fight's `RunCombatSim` record and drops it at
the server's end of that fight, so that everything 3.4 says about a held record follows with Combat off. Read
2026-11-20 03:10-04:00 EST by w4-0042 (account B) with llvm-objdump 19 by hand on the old build's analysis copy (PE time
stamp 0x546CD0A8 checked; `reverse-engineering-setup.md`), addresses and behaviour only (D83). Marks: `[C]` read in
that code, `[L]` live (section 3.5.6), `[I]` inferred.

#### 3.5.1 The counter and who changes it [C]

- **The counter** is header word 11 (record + 0x3C). The header copy of every post (0x490310, called by the posts
  0x87E3D0 and 0x87E4A0) writes it 0. Every later change goes through the EXE's `InterlockedIncrement` /
  `InterlockedDecrement` import slots; no plain store to it was found in the functions below.
- **Find-by-id 0x797E70** (`__thiscall`, `this` = the queue `0x2A10E08 + 0x4C * [0x2A10200]`, one argument the event
  id, `ret 4`): walks **the dispatch half only** (index `(flip - 1) & 1`, flip at queue + 0x20), compares the payload's
  first word (record + 0x44) and returns record + 0x10 (the header) or 0. It takes no lock. **A record still in the
  write half is not found**, so the counter of a record that has not moved yet cannot be reached through it.
- **The writers:** 0x4BA9A0 (the simulation's take: drops its previous id's hold, stores the new id at sim + 0x34, +1;
  its only caller is 0x731FF0 at 0x732833); the simulation's base destructor 0x4067C0 (-1 on the id at sim + 0x34);
  0x488B30, the Unit system's listener of type 0x59 (below).
- **0x488B30, the second holder.** When bit 0 of its object's +0x128 is set it adds 1 to the record's counter and
  defers the call to its own queue at object + 0x118 (0x86B0B0, with itself as the callback; the only caller of that
  post). When the bit is clear it subtracts 1 and then calls the simulation 0x731FF0. So its +1 and -1 bracket the
  deferral, and the simulation's own +1 comes after the -1. Its plain path assumes it was entered with the bit set
  first `[I]`: with the bit clear at the first dispatch the counter would go to -1 and stay held for ever with Combat
  off (0316 and 4771 S1 saw `EndCombatSim` one or two frames later `[L]`), or to 0 with Combat on (4771 P4 saw a held
  record for 4-4.9 s `[L]`). The other listeners of the same object (0x4737B0, 0x4C4330, 0x501CD0, and 0x6B3DD0 for
  AbortCombatSim) defer the same way without a hold. What sets the bit (Q8) is still not found: no `or`/`and`/`bts`/`btr`
  on +0x128 of that object in the whole `.text` (the one hit, 0x7059ED, clears bit 0 of `g_Civ5App + 0x128`).
- **The dispatcher** of type 0x59 is the generic broadcaster 0x63E530 -> 0x8668E0 (set at 0x5264EC with the delegate
  list at `0x2A10208 + 0x600 * index + 0x59 * 8`); it calls every delegate with the payload and touches no counter. The
  queue's dispatch loop 0x63ABA0 takes `{function, context}` per record type from that table.

#### 3.5.2 A hold taken before the record moves does not stop its move or dispatch [C]

- **The move 0x4AC910** (inside the pump 0x785AF0, under the queue's critical section at queue + 0x34) copies each
  movable record whole (`memcpy` of its size word: the counter goes with it) from the write half to the dispatch half.
  Its block test (0x60EB10 with the predicate 0x727A30) reads header words 0-10 and 12 of both records, never word 11:
  a record's own counter does not decide whether it moves.
- **The collection 0x457060** (first step of the pump, on the dispatch half, no lock) keeps every record whose counter
  is not 0 (any value, negative too) and erases the runs of records at 0, then sets queue + 0x2C (the dispatched
  offset) to the half's new size. **The dispatch cursor** (0x6DE5D0) is half begin + queue + 0x2C, so the dispatch loop
  sees only the records the move appended this pump: a kept record is never dispatched twice, and a record moved with
  counter 1 is dispatched once and then kept.
- So an outside +1 taken while the record is in the write half lets it move and dispatch as usual (the flags hide on
  `RunCombatSim`) and keeps it in the dispatch half until the holder's -1. Everything that shares its keys waits behind
  it (3.2): `EndCombatSim`, the unit's `SetDamage`, `DestroyedInCombat`, `Moved` and its later combats. A barrier record
  (flags 0x13, 3.3) blocks every later record, and 0x4FB9F0 sees the queue non-empty, so the AI-turn gate of 12.3 waits.

#### 3.5.3 Nothing assumes that the holder is a simulation; AbortCombatSim [C]

- `AbortCombatSim` is record type 0x5B (its binder 0x56ED20 sets the table entry `+0x5B * 8`). It is posted by 0x488A80
  (a 0x60-byte record with an empty key header, flags 0: it blocks nothing) from 0x59C234 and from 0x5AE930 (called at
  0x5FC0C1); both posts follow a call to slot 166 (offset 0x298) of a game object with argument 1 `[I: its meaning was not
  read]`. The Unit system's listener 0x6B3DD0 defers like 0x488B30 (no hold) and calls 0x62DD90 on the combat system
  0x2D701A8, which ends every running simulation in the order of its +0x30 (slot 1, then the base destructor 0x4067C0,
  which drops only the hold whose id the simulation stored, then frees it).
- Nothing in that path, in the collection or in the move resets a counter or looks for a holder. **An outside hold
  survives `AbortCombatSim`**: the record stays held until the outside holder drops it. The proxy's watchdog (note 3.1:
  the limit plus 2 s) bounds it; for stock's behaviour the server should also end its fights when `AbortCombatSim`
  is dispatched `[I]`.
- The ten other callers of find-by-id hold records of other systems by their own stored ids `[I: not read one by one]`;
  none was seen to touch a 0x59 record's counter (D83 edit 2026-11-25, 0769: the scan's list of the ten call sites by
  address was replaced by this count).

#### 3.5.4 Thread and moment of the take and the drop [C, I where marked]

- **The take: inside the carried call.** `GameplayUnitCombat` 0x67DD90 posts 0x59 with 0x87E3D0, which enters the
  queue's critical section and returns with it held; the handler reads the event id from the payload and leaves the
  section (0x813D60), then does the same for 0x5A, and returns the 0x59 id `[C]`. On the product path that call is a
  synchronous DLL->engine call served by the proxy on the thread that waits in the outer call: the core channel's
  thread, which is the EXE's game-core thread inside `ICvGame1::Update` (`proxy-design.md` sync rules) `[C, design]`.
  The main thread pumps queue B only while no `Update` is pending (`game-core-thread.md` 6.3) `[C]`, so when the carried
  call returns the record is in the write half. The proxy finds it there by walking the write half **under the queue's
  own critical section** (queue + 0x34; the move holds it too) for type 0x59 with that id, and adds 1 with
  `InterlockedIncrement`. Find-by-id cannot be used for the take (3.5.1). The visualise byte (payload + 0x500) tells a
  fight the EXE would simulate from one it would skip (4.1); the hold is taken only when it is not 0.
- **The drop: on the EXE's main thread.** The main thread is the only one that collects and compacts the dispatch half
  (the pump), so a pointer into it is stable only there or under an `Update`. In the AI turns the frame stops asking for
  `Update` while queue B holds records (12.3), so a drop tied to `Update` would never come: the drop has to run on the
  main thread, from a per-frame point outside the move (the Lua bridge's `LocalMachineAppUpdate` drain in the prototype;
  the shell's frame-F insertion for route S's OwnerEnd). There the record is found in the dispatch half (find-by-id or the
  same walk) and the counter decremented; a record not moved yet (a drop before the first pump) is found in the write
  half under the critical section. The next pump's collection erases it and the waiting records move.
- **Means:** queue B's data and the EXE's own critical section only (reads, two interlocked writes,
  `EnterCriticalSection`/`LeaveCriticalSection`); no EXE function call and no code byte. Find-by-id may replace the
  drop's walk on the main thread (an EXE call, also approved). Build-table names (epic 0590, #7693 answered by #7700):
  `pac_qb_index` and `pac_qb_buf` (carried already), optionally find-by-id (a new code row). Offsets (stride 0x4C, flip +0x20, dispatched offset +0x2C, critical
  section +0x34; record type +4, counter +0x3C, flags +0x40, event id +0x44, visualise byte +0x544) are layout, the same
  in every record.

#### 3.5.5 What follows for route S [I]

H1 holds statically: the proxy's hold makes the EXE wait for the server's fight exactly where it waits for its own
simulation. With Combat off the simulation takes no hold (4.1) and 0x488B30's deferral bracket is balanced, so the
proxy's hold is the record's only one. The live check (3.5.6) tests the counter's value at the drop and what waits.

**For ticket 4773 (the hold in the proxy)** `[I]`, from the prototype in `proxy/evidence/0042/4796/`:
- `proxy/src/proxy_combat_hold.{h,cpp}` (new): the take (the eight method ids `kM_ICvEngineUtility{1..4}_Gameplay{Unit,City}Combat`
  of `vp_ids.gen.h`; the returned event id from the RETURN payload's first word; the write-half walk under the queue's
  critical section; only when the visualise byte is set), a table of held ids, the drop by id on the main thread, a watchdog
  (the 12 s / 20 s limit plus 2 s, note 3.1), all holds dropped at unload and at a game exit.
- `proxy/src/proxy_rt.cpp` `OnCall`: one call after `EngDispatchGenerated` returned handled and not failed
  (`hooks.patch` here). The server's DLL calls arrive there on the core channel's thread.
- The drop's trigger: the route S stream's OwnerEnd for the fight, executed at the insertion of its frame (note 0.1, ticket
  4795); until that exists, the Lua bridge's frame hook (`proxy_ui_bridge_rt.cpp` `Init`, chained before
  `ProxyUiDraw::OnFrame` as in `hooks.patch`) with the watchdog alone.
- Build table (epic 0590; lead-0590's answer #7700 on thread 0590, 2026-11-20): queue B's index and buffer array are
  already carried as **`pac_qb_index`** and **`pac_qb_buf`** (`exebuild/symbols/proxy.tsv`, data; read through
  `ProxyExe::Addr` / pacing's `Rvas().qbIndex` / `Rvas().qbBuf` once 0590's stage 2 is on main): use them, add no row.
  Find-by-id is not in the catalogue; the drop needs it only if it calls the EXE's walk instead of its own, then it is a new
  code row (kind code, anchor 0x797E70, with the cross-check of `exe-build-table.md` 8). The record offsets stay
  constants in the code; the ones written through (the hold counter +0x3C and the flip +0x20 above all) are guarded by an
  X pattern row at a carried instruction that uses them, as pacing does for the ordering byte. A failed guard is D118's
  report (note 3.6).
- Tests: `proxy/tools/rt_tests/` with a fake queue (two halves, the critical section, records of types 0x59/0x5A): the take
  finds the record in the write half, a hold survives a simulated collection, the drop decrements once, a drop of a
  missing id is counted, the watchdog fires.

**If H1 fails (the note's three-part fallback, sized `[I]`):** (1) the bridge raises `EndCombatSim` at the server's end
(4790's raise: a raise kind in `proxy_ui_bridge.cpp` with `RunCombatSim`'s 11 values kept by the proxy from the carried
call, 3-5 h); (2) `proxy/src/proxy_pacing.{h,cpp}`: the next AI `Update` request deferred while the server reports a
watched fight (a fight-active count from the server on the core channel, read by the pacing hook, 5-8 h with tests);
(3) the fighters' flags, damage text and death wait for the anchor table (server/src/units, 4-8 h). About 12-21 h against
the hold's 6-10 h.

#### 3.5.6 Live check: H1 holds [L, 2026-11-20 04:21-04:39 EST, one run]

- **Configuration.** Instance c5h-c (workstation, C:, Goldberg, EXE stamp 0x546CD0A8 = build 4390913), MC-B's grant #7716;
  layout = 4771's (0585's take: server.exe e85af4a5..., x64 DLL 78355a75... = pin batch 5) with the spike proxy
  34fd764a... (`proxy/evidence/0042/4796/proxy_combat_hold.cpp` + `hooks.patch` on w4-4796 f110257e8, built from the
  pin's source 1fd51353e; never merged); VP64Options.ini ServerRenderer 1, ServerUnits 0; config.ini the product's D33
  list plus **Combat = 1, Projectile = 1** (Particle on), for this session only, restored by SHA-256 (verify rc 0); save
  `C5H-4796-TINY_0018` (a copy of C5H-3000-TINY-T0018, Spain, turn 18), Quick Combat set OFF and read back.
  Driver `run_4796.py` / `chain_4796.sh`, timeline by `analyze_4796.py` (`proxy/evidence/0042/4796/live-r1.txt`).
  Pictures DIAGNOSTIC (D112), outside git: `C:/c5-64bit-work/0042/w4/pics/h5/`.
- **The take and the drop** (vpproxy.log, 4 of 4): every take found the record **in the write half** with the visualise
  byte 1 and made its counter 1; every drop found it **in the dispatch half** and took the counter **1 -> 0**: no other
  holder was left on it with Combat off (0x488B30's bracket balanced, 3.5.1). The human's three attacks were served on the
  **main thread** (thread 10556: the ui channel; the orders went in through `Game.SelectionListGameNetMessage` from the
  Lua channel), the barbarian's on the **game-core thread** (22760, inside the AI `Update`): the take works on both, under
  the queue's critical section. The drops ran in the bridge's frame hook on the main thread, 150 frames per 5 s in the
  human's turn and 526 in the AI turn (the turn wait lifts the frame cap).
- **The timeline** (Lua `os.clock()`, seconds from the first order; Run = `RunCombatSim`, End = `EndCombatSim`):

  | Scene | Run | End | End - Run | After the End |
  |---|---|---|---|---|
  | 1 archer kills a warrior (85 damage before) | 0.063 | 5.064 | **5.001** | `SetDamage` 100 and `SerialEventUnitDestroyed` at 5.097; the combat text "Your Archer bombarded an enemy Warrior and destroyed it!" first in the picture of 6.0 s, absent at 4.0 and 5.0 s |
  | 2 blitz warrior, first attack | 13.696 | 18.697 | **5.001** | both `SetDamage` at 18.730 |
  | 2 blitz warrior, second attack (ordered at 15.400, while the first was held) | **18.731** | 20.463 | 1.732 | both `SetDamage` at 20.496 |
  | 3 AI turn: a barbarian warrior on the human archer | 38.766 | 43.520 | 4.754 | `SetDamage` at 43.533, **`ActivePlayerTurnStart` at 43.697** |

  - The game state was final at once: scene 2's target had 32 damage 1.6 s after the first order, while the picture's
    first End was 3.3 s away (TARGET reads).
  - **The blitz unit's second fight waited** behind the first held record and was dispatched one frame (34 ms) after the
    first End. Its own hold showed only 1.7 s because the prototype times every hold from its take (both were taken
    1.8 s apart); route S's OwnerEnd comes at the end of the server's fight, so this artefact goes with the timer.
  - **The AI turn waited for the hold:** `ActivePlayerTurnEnd` at 37.732, the barbarian's fight 38.766-43.520, the human's
    next turn 0.18 s after its End (the turn 5.97 s in all). The hold was the fight's only one (Combat off), so the wait is
    the proxy's.
  - Flags: `EndCombatSim`, which the flag code answers, came at the hold's end in every fight; in scene 1's pictures the
    fighting pair had no flags at 3.0 and 4.5 s and the flags were back at 5.5 s (the camera moved to the selection
    meanwhile, so this is read from two crops only).
- **Not covered:** a city fight (`GameplayCityCombat`), air and nuclear fights, `AbortCombatSim` live, a drop before the
  record's first move (the write-half path of the drop), several fights in one AI turn, the hold together with D165
  options 2 and 4 (they were at the build's defaults), the human's own fights during the AI turn. The driver's turn wait
  passed the wrong turn and waited out its 900 s limit after the AI turn had ended (rc 6 for the arm; the data above are
  from before that).

#### 3.5.7 The hold as built (ticket 4773, 2026-11-20)

Written by w10-0042 (account B, branch `w10-4773`) from 3.5.1-3.5.6 and 4796's prototype (rewritten, no literal address).
Files: `proxy/src/proxy_combat_hold.{h,cpp}`, one call in `proxy/src/proxy_rt.cpp` `OnCall` (after `EngDispatchGenerated`
returned handled and not failed) and the frame hook chain in `proxy/src/proxy_ui_bridge_rt.cpp` `Init`
(`proxy/evidence/0042/4773/hooks.patch`); tests `proxy/tests/combat_hold/`.

- **Three parts.** The *core* (take, table of holds, drop, watchdog, checks) works on a `Target` of plain addresses and
  never includes the build table, so its tests build on any branch. The *binding* `TargetOf(base)` is the only reader of the
  build table: the build from the image header (`ProxyExe::BuildOfImage`), queue B's index and buffers as
  `ProxyExe::Addr(build, VPX_pac_qb_index / VPX_pac_qb_buf, base)` (epic 0590 stage 2; no new symbol). The *glue*
  (`AfterServed`, `Init`, the report) is the only part that needs the proxy runtime.
- *Added 2026-11-20 (ticket 4783, MC #8543):* the caller is route S's common state (`proxy/src/proxy_route_s.h`), which
  arms the hold LAST, after the sound executor and the effect player have attached, and disarms all three as one; the hold
  gained `ReleaseAll` (every held fight dropped at the next frame, counted `released`), a watchdog sink (each watchdog
  drop is passed to the common state, which disarms route S for the rest of the EXE process) and `SetReportSink` (its
  D118 reason goes into route S's one report). `unit-combat-server.md` 16.
- **Off unless route S arms it.** `SetServerFights(true)` (to be called by 4782/4795 when the server plays fights) arms it;
  until then nothing is read or written and `AfterServed` returns at its first test. TEST ONLY: `VP64_TEST_COMBAT_HOLD=<ms>`
  in the EXE's environment arms it at the proxy's boot with an automatic OwnerEnd `<ms>` after each take (0 = none, only the
  watchdog). The product today (Combat on, D147) must not arm it: the EXE's own simulation holds the record then, and an
  armed proxy hold without a server fight would keep every fight held until the watchdog.
- **The take** (the thread that served the carried call: the main thread for the human's orders, the core thread in the AI
  `Update`): the 8 methods `ICvEngineUtility{1..4}::Gameplay{Unit,City}Combat`; the event id = the RETURN payload's first word;
  the write half walked under the queue's critical section (q + 0x34); type 0x59 and that id; hold only when the visualise
  byte is 1; `InterlockedIncrement` of +0x3C. The record's key words 0-9 are kept with the hold.
- **The end and the drop.** `OwnerEnd(eventId, played)` (any thread) marks the hold; the drop runs on the main thread in
  the bridge's frame hook (after `ProxyUiDraw::OnFrame` when that is on): the dispatch half without lock (only the main
  thread's pump changes it), else the write half under the critical section; the record must still be type 0x59, size 0x560,
  with the same key words; the counter is lowered by compare-exchange, never below 0. `played = false` (the server could
  not play the fight) drops it all the same, with a LOUD line and a count. The form agreed with 4782 (lead-0042 #7862,
  `unit-combat-server.md` 4): the fight's OwnerEnd owner is `CombatRecord::returned`, the EXE's event id of the
  RunCombatSim record (= the DLL's parent id); `played` is true for a natural end or the fight limit, false for Refused,
  Gone or Reset; a server Reset arrives as one OwnerEnd per fight (no drop-all call); owner 0 (the server's "owner unknown")
  never names a hold (no hold is taken for event id 0), so only the watchdog drops such a fight's hold; per-clip OwnerEnds
  (4795's ClipEnded) are not the hold's.
- **The watchdog** (MC #7790 condition 3): a hold not ended by OwnerEnd is dropped at its take + 20 s + 2 s, LOUD. 20 s for
  every fight, not 12 s: the EXE takes 20 s when an air figure takes part, decided by the figures' weapon records (4.2,
  13.2), which the proxy does not read; a 14 s watchdog would drop an air fight's hold while the server still plays it.
  `SetLimitMs` takes a fight's own limit once the server's fight record names it. `[I]` for the choice.
- **The verified layout per build** (MC #7790 condition 2): `kLayoutVerified` in the .cpp lists build 4390913 only, with
  its evidence (this section's 3.5.1-3.5.6). On any other build arming fails: one LOUD line ("layout is verified on build
  4390913 only, not on Steam build 26577625 ..."), the D118 report (non-fatal stage `combat-hold`, the helper's popup, written
  on a thread of its own), and `SetServerFights` answers false. **How route S fails loud there (D118):** the server asks the
  proxy before it plays its first fight (4782/4795's handshake, through `SetServerFights(true)`); a false answer means the
  server plays no fight in that session and the EXE shows every fight as with Combat off (RunCombatSim and EndCombatSim one
  frame apart, damage and death at once), while the report and popup tell the player why. Adding build 26577625 needs its
  own reading and a live take and drop on c5h-new-a/b (a later step of 4773 or 4779), never by analogy.
- **Run-time checks of every offset written through** (lead-0590 #7786), each a refusal for the rest of the proxy load
  (LOUD, the report once, no new hold; holds already taken are still dropped): queue B's index 0..2; the control block
  readable; the critical section at +0x34 has an initialised shape before it is entered (debug info -1, or a readable
  debug record that points back at it; recursion count 0..0xFFFF; 0x34 + 0x18 = 0x4C, the stride); each half's begin and
  end in bounds and readable; every record size 0x48..0x40000 and the walk ending exactly at the half's end; the found
  record's size 0x560, visualise byte 0 or 1, counter 0 at the take; the counter 1 or more at the drop. A drop that finds no
  record of that id with the same keys (a load or a game exit during the fight) writes nothing and is counted (`gone`, LOUD).
- **Not built:** the drop at frame F's insertion (4795; the frame hook stands in), the server's call of `SetServerFights`
  (4782/4795), the 12 s limit per fight, a drop of every hold at the proxy's unload (the queue is gone with the game then).
- **Tests** (`proxy/tests/combat_hold/test_combat_hold.py`, offline, a fake queue B): see the ticket's log for the run;
  the live smoke on an old-build instance is in the ticket too.
- **Live smoke of the product hold [L, 2026-11-20 10:31-10:38 EST, one launch; W19 of team-0042].** c5h-g (F:, Goldberg,
  EXE stamp 0x546CD0A8 checked, F: read latency 1.5 ms), MC-B's grant; claimed 10:31, released 10:38. Proxy 4e61fe3f =
  the team branch with 4773 merged (built from the pin's source 1fd51353e, no spike code), with server e85af4a5, composite
  4a2ad069 and x64 DLL 78355a75 as in 3.5.6 (`F:/c5-64bit-work/0042/w19/layout4773`); 4796's arm h5 and scenes unchanged
  (`proxy/evidence/0042/4773/run_4773.py`, `chain_4773.sh` = 4796's with W19's folders; `prelaunch_4773.sh` the grant's
  checks), `VP64_TEST_COMBAT_HOLD=5000` exported by the chain and inherited by the EXE (`tools/harness/winapi.py`
  `build_env`). Timeline: `proxy/evidence/0042/4773/live-r1.txt` (`analyze_4796.py`).
  - The proxy armed itself from the variable ("LOUD: TEST ONLY: VP64_TEST_COMBAT_HOLD=5000 ... armed as route S would
    arm it"; "combat hold: on (Steam build 4390913 ...; layout verified: ticket 4796 ...)"): the binding resolved
    `pac_qb_index` / `pac_qb_buf` through main's build table and the build check passed.
  - **4 takes, 4 drops, all by OwnerEnd (TEST) after 5000 ms, every drop in the dispatch half with the counter 1 -> 0**;
    the human's three attacks taken on the main thread (11856), the barbarian's on the game-core thread (19724) inside
    the AI turn; drops on the main thread, 150 frames per 5 s in the human's turn, 563 in the AI turn. No refusal, no
    `gone`, no unknown OwnerEnd, no watchdog drop; the only LOUD line is the TEST ONLY one.
  - Lua timeline, End - Run: **5.003 s** (scene 1, the archer kills the warrior: `SetDamage` 100 and `UnitDestroyed`
    0.031 s after the End), **4.999 s** (scene 2, blitz, first attack), **1.701 s** (scene 2, the second attack, ordered
    while the first was held: dispatched 0.034 s after the first End, its own hold timed from its take, as in 3.5.6),
    **4.820 s** (scene 3, the AI turn: `ActivePlayerTurnStart` 0.086 s after the End). The same shape as 4796's
    prototype (5.001 / 5.001 / 1.732 / 4.754 s). Game state final at once (scene 2's target had 32 damage 1.6 s after the
    order).
  - No crash, no box (dialog marker clear after the stop), run rc 0, restore rc 0 (backup_restore_ok true, provision
    verify 0). Pictures DIAGNOSTIC (D112), outside git: `C:/c5-64bit-work/0042/w19/s4773/pics/h5/`.
  - So the product's take, drop and arming work live on build 4390913 as the prototype did. Not covered by this smoke:
    the server's own OwnerEnd (4795's stream; the TEST seam stood in), a city fight, an air fight, a load during a fight,
    the watchdog live, build 26577625 (arming refused there by design until its own reading).

#### 3.5.8 The safe form of a per-frame hook on the main thread (ticket 4720, 2026-11-20) [C for the code, L for the logs, I where marked]

Written by W12 of team-0042 (account B), 2026-11-20 06:25-07:00 EST, for 4773 (W10: the hold's drop) and 4795 (OwnerEnd at
the frame insertion). Read: `proxy/src/proxy_ui_bridge.cpp` (`Drain`, `SetFrameHook`, `SetFrameLuaHook`, the reset),
`proxy/src/proxy_ui_bridge_rt.cpp` `Init` (team branch `epic-0042-combat-movement` at 2211a7127), 4796's prototype
(`proxy/evidence/0042/4796/proxy_combat_hold.cpp` + `hooks.patch`), 4798's spike (branch `w6-4798`, commits 1f6aadf6f and
69714aa3b, never merged) and 4798's run folders r2-r4 (outside git). The crash itself is in `effects-formats.md` 8.1.1.

**The hook point as it is** [C].
- Two slots in `proxy_ui_bridge.cpp`, plain function pointers: `g_frameHook` (`void()`, ticket 0487's UI draw growth) and
  `g_frameLuaHook` (`void(lua_State*)`, ticket 0586's live reload). `proxy_ui_bridge_rt.cpp` `Init` sets them once (its
  `InterlockedCompareExchange` guard) before the first `OnLuaEntry`; the bridge's reset sets both to NULL. A set slot makes
  the bridge install its drain alone when the bridge is off (the product).
- `Drain` is the bridge's listener of the InGame context's `Events.LocalMachineAppUpdate`: a `lua_CFunction` called by the
  EXE's Lua event raise, once per in-game frame, on the main thread. It returns at once for an older generation
  (`Current(L)`) or another thread (counted as `wrongThread`); then it runs the queued raises, the plain hook, and the Lua
  hook with its own `L`, restoring the stack top after it. So a hook runs only between the drain's install into an InGame
  context and that context's end (none between a game's exit and the next load's install), at the frame's rate (30 per s
  at the human's turn cap, about 105 per s in the AI turn: 4796's 526 calls per 5 s, 3.5.6), always from inside the EXE's
  Lua call.

**4796 and 4798 compared** [C].

| | 4796 (no crash: one launch, 4 of 4 fights, 18 min) | 4798 (r2 and r3 crashed) |
|---|---|---|
| Slot and function | the plain slot: `ProxyCombatHold::OnFrame`, which calls the previous plain hook (ProxyUiDraw's or none) first through a pointer stored by `Chain` in `Init` | the Lua slot: `FrameLuaChain4798(L)` calls `ProxyLiveReload::OnFrame(L)` and then `ProxyFxSpike::OnFrame()` directly |
| Convention, previous hook | `void()` `__cdecl` in the proxy; the previous one called once | `void(lua_State*)` `__cdecl` in the proxy, the slot's type; the previous one called once |
| Lua | none | the live reload's own calls (`lua_cpcall`, unchanged); the spike none |
| EXE memory | queue B through pacing's names, base = `GetModuleHandle(NULL)`, `ProxyPacing::VerifyExe` first, every access under `__try/__except` | none until a request is read; then the 4 MB queue at `base + va - 0x400000` with `va` from the request file, no fault guard |
| Work per frame | counters, a critical section, a table walk | a counter, a log line every 1800 frames, and every 10th frame `Poll`: `GetFileAttributesExA` on `<proxy folder>\fxspike_req.txt`, and when the file changed `fopen`/`fread`, `std::string`/`std::vector` parsing, then the commands and an answer file |
| Files | none | the request and the answer in the proxy's folder, which is `<user>\MODS\(1) Community Patch` |

Excluded as the cause of r2/r3 [C]: a convention or type mismatch (none), a previous hook called twice or never (once,
directly), a returned-into freed thunk (every function is static code of the proxy, which stays loaded), a misused
`lua_State` (the spike never touches it), an unrebased EXE address (no spike code that uses one had run in r3; the
arithmetic in `effects-formats.md` 8.1.1), and a write under `<user>\MODS` (R-mods, 2026-11-20 07:39-07:46: a file
created there during play, no crash) [L]. Every spike launch (r2, r3, R-logs with the request under `<user>\Logs`)
crashed 0.25-0.35 s after its first request, before the spike's "request seen" log line, while the same code runs clean
offline with both CRTs; R-decide (08:37) put the crash in the spike's read of the request file inside the game (not
`g_log`), a path point 4 below forbids; the evidence is in `effects-formats.md` 8.1.1. (Corrected 08:30: the 07:00 text narrowed the
cause to the MODS write; R-mods and R-logs ruled that out.)

**The safe form** (for 4773 and 4795; each point names what 4798 did against it).
1. **Install once, in `Init`, as one function per slot.** `proxy_ui_bridge_rt.cpp` `Init` composes the slot's function
   in that file and sets it before the first `OnLuaEntry`, for example a plain `FrameHook()` that calls
   `ProxyUiDraw::OnFrame()` when that is on and then `ProxyCombatHold::OnFrame()`. Each part is called exactly once per
   drain in a fixed order. 4796's `Chain` (a stored previous plain hook, called first) is equivalent and acceptable; no
   module may keep a hook of another module that can be replaced later, and nothing sets a slot after `Init`. Use the plain
   slot unless the hook calls Lua; a Lua hook makes every Lua call protected (`lua_cpcall`/`lua_pcall`) and never keeps `L`.
2. **Main thread, not every frame.** The drain guarantees the thread. The hook must not rely on running in every frame or
   at all after a game's exit: a hold, a timer or a request that must end at a game exit or an unload is ended by the
   module's own unload path (4796's rule: all holds dropped at unload and at a game exit), not by the hook. Time is taken
   from a clock (`GetTickCount`/QPC), never from a frame count.
3. **EXE memory only by the build table, checked and guarded.** Addresses come from the build table (`ProxyExe::Addr`,
   pacing's `Rvas()`; D202), rebased from the running EXE's base: the old build's header sets DYNAMIC_BASE and c5h-g ran
   it at 0x00970000, so a VA at 0x400000 is never used as it stands. The EXE check (`ProxyPacing::VerifyExe` or the table's
   gate) runs before the first access; every read and write of EXE memory sits under `__try/__except`, counted and logged
   once. No EXE function call except a named row of the table (D136 means). 4798 took its addresses from a text file and
   wrote the queue without a guard.
4. **Bounded work, no allocation, no exception out.** Fixed tables, a critical section held for a table walk, a log line
   every N frames (both prototypes log the frame count and thread; keep that, it shows the hook alive). No file I/O, no
   wait or `Sleep`, no process or module enumeration in the hook. Nothing may leave the hook as a C++ exception (it
   would unwind through the EXE's Lua call): no `std::string`/`std::vector` growth on the frame path, or the body inside a
   `try { } catch (...) { }` that counts. 4798 polled a file and parsed it with `std::string`/`std::vector` in the hook.
5. **Nothing under `<user>\MODS` at run time, from the hook or from a test driver of it** (the rule of
   `renderer-in-server-design.md` 4.14; the EXE watches the whole MODS tree and reacts on its main thread). Test triggers
   and answers go to `<user>\Logs\VP64\` (a subfolder per ticket), the place the proxy's own run folder already uses. A
   single write there did not crash the game (R-mods), so this rule is about the main thread's stall (0148) and keeping
   test runs comparable, not the 4798 crash.
6. **Removal.** The bridge's reset clears both slots; the module's own state is dropped by its unload path (point 2). A
   module never calls its hook itself from another thread.

For 4773 this means: `ProxyCombatHold::OnFrame` in the plain slot, composed in `Init` with ProxyUiDraw's (or 4796's
`Chain`), the drop's EXE writes as 4796 did them (table names, `VerifyExe`, SEH), the watchdog on a clock, the holds dropped
at unload and at a game exit by the module, and its test driver's files outside MODS.

**The 4725 defect class, and the product's main-thread hooks against it (ticket 4725, W19 of team-0042, 2026-11-20 10:25
EST) [C for the census, the objects and the declarations named; L for the spike's crash].** The 4798 spike's crashes
were not the hook: a function of the spike's TU (`_SECURE_SCL=1`) called a `std::vector<std::string>::_Insert_n` that the
linker had taken from a `_SECURE_SCL=0` TU (one mangled name, two layouts and argument counts), so ESP came back one
word low and a pop-only epilogue returned into the stack (`effects-formats.md` 8.1.1, "The call named"). The class is a
call whose callee pops or reads other stack words than the caller's declaration assumes. The product today: (a) **STL
settings** - `proxy/evidence/0042/4725/scl_census.py` on the team branch with 4773 merged (w19-4773): 30 container TUs
at `_SECURE_SCL=0`, `proxy_combat_hold.cpp` (4773) among them; four TUs at `_SECURE_SCL=1` (`proxy_luaexec.cpp` with
vector/map/list/set/deque, `proxy_budget.cpp`, `proxy_farm_guard.cpp` and `proxy_shell.cpp` with `<string>` only), and
`comdat_overlap.py` over that build's objects (proxy 4e61fe3f) finds **no std:: function defined by TUs of both
settings**: no call of this kind exists in the product build today [C]. It is latent: any new container use in one of
those four TUs that matches another TU's instantiation recreates it. The one-line cure is a build-wide
`/D_SECURE_SCL=0 /D_HAS_ITERATOR_DEBUGGING=0` for the proxy's x86 TUs in `proxy/build_proxy.py` (not in 4725's touches;
for lead-0042 to file); until then a proxy TU that includes an STL header defines both macros to 0 before it (or
includes `proxy_rt_internal.h` / `proxy_rt_logic.h` first), and `scl_census.py` + `comdat_overlap.py` are the check.
**Current (2026-11-20, ticket 0609, merged into main 13:39):** the build-wide cure is in. `proxy/build_proxy.py` sets
one setting (`_SECURE_SCL=0`, `_HAS_ITERATOR_DEBUGGING=0`) for every x86 TU of the proxy DLL and of vp64helper.exe. It
does so through a forced include with `#pragma detect_mismatch`, so lld-link refuses a mixed pair, plus
`-Werror=macro-redefined` and an object check before each link. The per-TU workaround above is no longer needed. The
two scripts remain a cross-check. Stage 4 of epic 0042 was built once with that check (issue 0042's log). (b)
**Declared signatures of the hooks** [C, read 2026-11-20]: the bridge's drain is a `lua_CFunction` (`int __cdecl(lua_State*)`)
as the EXE's Lua calls it; the two frame slots are `void __cdecl()` and `void __cdecl(lua_State*)`, called only by the
proxy (4773's `ProxyCombatHold::FrameHook` is the plain slot's type); `proxy_queue_guard.cpp`'s `GuardedExchangeAdd`
is `LONG WINAPI(LONG volatile*, LONG)`, the import it replaces (`InterlockedExchangeAdd`, stdcall, 2 words);
`proxy_pacing.cpp`'s `SecsPerTurn` is `float __fastcall(void* self, void* edx)` for a thiscall method with no stack
arguments returning in ST0 (both pop nothing); the composite's farm guard `FgHook` is `void __fastcall(void* obj, void*
edx, void* ev)` behind a stub that ends `ret 4` (one stack word, as the walk's thiscall passes); the composite's D3D11/DXGI
detours are `STDMETHODCALLTYPE` with each method's own parameter list. None of them differs from its callee's or
caller's convention or stack-word count. 4722 (the server's emitter) runs in the x64 server, not on the EXE's thread.
**4723 (the sound executor) will call three EXE functions** (`exe-audio.md` 8: the 3D play, thiscall, 11 words, `ret
0x2C`; `FSound::Stop`, thiscall, none; the fade, thiscall, 3 words, `ret 0xC`): its typedefs must carry exactly those
word counts (as `__fastcall` with a dummy EDX, the pacing form), and its offline test checks each typedef's stack
effect against a stand-in callee written with the same `ret N` (ticket 4723).

#### 3.5.9 The layout on build 26577625 (ticket 0618, 2026-11-20) [C, L]

The 16 EXE functions 3.1-3.5 read the layout from (find-by-id, the move, the collection, the pump, the cursor, the
header copy, both posts, `GameplayUnitCombat`, the simulation's take, start and base destructor, the 0x59 listener, the
block test and its predicate, the section leave) are instruction for instruction the same on Firaxis' build 26577625,
every struct displacement and immediate included (`../02-exe-internals/new-exe-build-26577625.md` C.6.2, the method
in C.6.1). So stride 0x4C, flip +0x20, halves +4/+8, critical section +0x34, dispatched +0x2C, record size/type,
header words, counter +0x3C, flags +0x40, event id +0x44, visualise byte (payload +0x500) and RunCombatSim 0x59/0x560
hold there; `kLayoutVerified` lists build 26577625. Live on c5h-new-a (Goldberg, build 26577625), 4773's smoke with
`VP64_TEST_COMBAT_HOLD=5000` on 0606's flipped release layout with a proxy carrying the row: 4 takes, 4 drops by
OwnerEnd, no watchdog drop, Run->End 5.000, 5.001, 1.735 and 4.769 s (4390913's run: 5.003, 4.999, 1.701, 4.820);
C.6.5 has the run.

## 4. The simulation, start and end

### 4.1 Start conditions [C, 0x731FF0]

On a 0x59 message the simulation:
1. looks up the attacker and defender models from their keys (0x73BFA0 for a unit; 0x4E4E80 + 0x4812D0 for a city,
   by its plot);
2. returns without a simulation if **the app's DisableSystems mask (`[0xCC8E74] + 0x940`) lacks the Combat bit
   0x800**, or if **the visualise byte (payload byte 0x500, `GetVisualizeCombat`) is 0**;
3. returns if a model is missing or invalid (0x6D7E60, 0x5E9350);
4. otherwise allocates one simulation object of 0xC1618 bytes (`cvCombatSimSystem.cpp` lines 0x1A9, 0x1C1 or
   0x1DC, by case: unit against unit, unit against city, city against unit, plot target), copies the payload into it
   (+0x303FC * 4), sets participant 0 (attacker) and 1 (defender), and participant 2 (the interceptor) when
   present. It then adds every damage member as participant 3 onward, a unit through 0x6AE910 and a city through
   0x4024F0. A plot without a defender goes through 0x6BD1A0. Last, it takes the hold on the event (0x4BA9A0),
   starts the object (vtable 0xA05470 slot 0, 0x648590, which steps the 68 participant slots) and adds it to the
   active list (0x5C43E0).

So with Combat off (the product path, D33, `combat-on-the-product-path.md` section 3), no simulation exists and no
hold is taken. `EndCombatSim` is dispatched one frame after `RunCombatSim`, and the unit's later records pass at
once. 0316's observation fits this: no stall, and the flags not hidden for long `[I: the flags were not looked at
in 0316]`.

### 4.2 The object and its clock [C]

- 68 participant slots (0x44) of 0x2D60 bytes from +0x270. Each holds up to 32 figure records of 0x158 bytes from
  slot + 0x54, with their count at slot + 0x30, plus the participant's damage percent (slot + 0x3C), a "has a
  projectile weapon" byte (+0x2D41; it is the "side has an air figure" byte, 13.2, corrected 2026-11-17), the model handle and flags.
- Clock: elapsed time at +0x20, advanced by the frame step at +0x24 (`0x7165E0`). Time limit at +0x28: **12.0 s**
  by default (constructor 0x6A4A40, the float at 0xA018C0), **20.0 s** as soon as a figure of any participant has
  a weapon whose record + 0x148 is 1 (the float at 0x9F39EC, set in 0x6AE910). That kind is the air figure: the
  bombers' side has +0x2D41 set and its combats the 20 s limit, a battleship's ranged strike on a submarine has 12 s
  `[L]` 2026-11-17, 13.2 (corrected; was: "read here as the projectile weapon `[I]`"). The value 0 of the same field
  sets +0x2D42, the sea side `[L]`, 13.2. `0x6171F0` is "elapsed > limit". Past the
  limit every figure is forced to its end state (section 5.3).
- Per frame (vtable slot 2, 0x5FE650): if the attacker's or defender's model is gone, the simulation ends.
  Otherwise it runs its passes (0x6C8FD0, 0x5EFC50, 0x6902C0, 0x613940, 0x51F3B0, then the figure state machine
  0x7165E0). When the state machine reports nothing left to do, or the clock has run out, it ends: 0x5192A0 runs
  and bit 8 is set at +0xC1505. 0x5192A0 computes each participant's position and facing from its figures and
  hands them to the unit models `[I]`. The hold on the event is dropped in the base destructor (next bullet but
  one).
- Slots 3 and 4 (0x41EFC0, 0x465CE0) take a figure's animation events (the `.ftsxml` triggers). Slot 4 handles
  event kind 1, read as the Action trigger, the hit moment `[I]`: it applies the hit to the figure's target
  (0x5BA1C0, 0x6DE7F0, then 0x686F30 with the target at figure + 0x94). When the striking figure's member combat
  record has the byte at +0x4E set (`HasCollisionAttack`; corrected 2026-11-20, see 5.3), the striker itself is put into state 8 and reported dead (0x4A40A0), a one-shot weapon
  (section 5.3). Where the struck figure's death is decided was not read; 5.4 has the inputs.
- **The system's frame loop** (0x774FE0) `[C]`: for each simulation in the active list, 0x571170 adds the frame
  time to the clock (+0x20) and to the step (+0x24) and reports whether at least 0.001 s (0x9EC9B8) has
  accumulated. If so, slot 2 runs. When slot 2 reports done, slot 1 runs (0x629230: 0x5192A0 unless already done,
  then 0x5B2530, which hands each participant's figures back to its unit model). Then the base destructor 0x4067C0
  runs: it **drops the hold on the event** (looks up the id at sim + 0x34 and decrements the counter). The object
  is freed and taken off the list. So the hold ends in the frame the simulation ends, and the waiting records move
  in the next pump. The clock is the frame time passed in, so the limits are wall time `[I: the caller's argument
  was not traced to its source]`.

## 5. The choreography

### 5.1 Who takes part, who strikes [C structure, I meaning]

Per figure, 0x6AE910 records the figure's slot in the formation, its weapon (0x494DB0, from
`ArtDefine_UnitMemberCombatWeapons`), whether it is already dead in the model (flag 0x10: it takes no part),
whether the weapon is a projectile weapon (kind 1) or a melee one, and whether the figure can attack at all.
**A figure that has none of the actions 1100 attack, 1130 anti-air, 1140 charge attack, 1160 attack city and 1180
bombard enabled gets flag 0x2000000, does not attack and is not counted among the side's attackers.** It plays
"bombard defend" (1280, state 0xE) instead. These are the `EnableActions` of `ArtDefine_UnitMemberCombats`
(section 6).

Targets: a figure without a target asks for one (0x5C7100, stored at figure + 0x94). A figure ready to strike with
a weapon asks 0x61EAC0 for a target figure for that weapon (figure + 0x44). **A ranged figure strikes while its
opponent's planned deaths (+0x2B84) exceed the deaths played (+0x2B88) plus the figures under projectiles in flight
(+0x2D30), or while the opponent owes a hit (+0x2D48 >= 1); a `ContinuousFire` weapon always strikes** `[C]`. There is
no fixed wave size. 0x7635E0 is the phase map (fight type, phase -> next phase); a figure stopped by that test goes to
3 when a next phase exists, else 2 `[C]`. (Corrected 2026-11-22 by W50, ticket 4741, `unit-combat-server.md` 18.14,
which has the rule in full; replaced "How many of a side's figures may strike at once is limited by counts on the
participant ..., so figures attack in waves `[I]`. 0x7635E0 picks whether an idle figure goes to "approach" (3) or
waits (2)".)

### 5.2 Figure states and the action each requests [C, 0x512DE0; meaning I]

0x512DE0 sets a figure's state (figure + 0x9C) and, for most states, the action code it requests from the model's
state machine (figure + 0x70). The codes are the `ec` codes of `model-reader.md` 6.4 (a). Path 4 of the
`.fsmxml` state machine is the requested action.

| State | Requested action | Reading |
|---|---|---|
| 0, 1 | (keeps the current one) | idle, looking for a target |
| 2, 3, 6, 0x10 | 1000 idle, or 1600 combat ready when the figure is set up for it; 1400 run when flagged to move | waiting / approaching; 6 has a timer (+0x40) |
| 5 | 1600 combat ready, weapon target from the weapon record | ready to strike |
| 7 | 1000 idle (reface, 0x79BA70) | after combat |
| 8 | 1200 death | dying |
| 9, 0xB | none; 0xB first calls 0x7A7500 | finished (0xB: done, refaced) |
| 10 | attack: 1100 attack, 1130, 1140 charge attack (when the last action was 1120 run charge and 1120 is enabled), 1160 attack city or 1180 bombard. The choice depends on the weapon kinds of the figure's side and of the other side (+0x2D41, +0x2D42), the figure's flags (bits 8 and 11) and the weapon record (+0x48, +0x49, +0x4E). The branches are `[C]`; reading 1180 as the projectile side's attack and 1160 as the attack on a city, as the action names suggest, is `[I]` | striking |
| 0xC | 1120 run charge | charging |
| 0xD | none | (sub-state) |
| 0xE | 1280 bombard defend | under fire, or cannot attack |
| 0xF | 1450 shuffle | adjusting position |
| 0x11 | 1400 run | moving to a position |
| 0x12 | 1800 victory | won |

### 5.3 Transitions (0x7165E0) [C for the branches, I for the reasons]

- 0/1 -> 0xB when the clock is past the limit; -> 0xE when the figure cannot attack; else a ranged figure with a
  target passes 5.1's strike test (0x5C7100: -> 10 to strike, or 3 / 2 when the test stops it); a figure still
  without a target keeps looking while the opponent's needs are not covered (after a time in state 1 it goes to 2),
  else -> 3 when the fight has a next phase, otherwise 2 (0x7635E0). (Corrected 2026-11-22 by W50, 4741; was "without
  a target and with the wave count full, -> 2; with room in the wave, 3 or 2 (0x7635E0)".)
- 5 -> 10 when its animation is done (0x7556C0 false) and a weapon target is found. A projectile side with the
  right flags also starts its projectile here (0x5DCC50(2, 5, 10, 0)) `[I]`.
- 10 -> 8 (death) after its strike, for a figure whose member combat record (figure + 0x5C) has the byte at +0x4E
  set (`HasCollisionAttack` `[C]`, 3 rows in VP), with flag 0x40 and one strike left (+0x7C == 1). Slot 4 (4.2) also
  kills such a figure at its own hit: a collision (one-shot) attacker, a missile or a suicide attacker. (Corrected
  2026-11-20 by lead-0042 from 4776, `unit-combat-server.md` M6; replaced "whose weapon record has the byte at +0x4E
  set ... read as a one-shot weapon `[I]`".) Otherwise the figure goes back to 1 for the next strike,
  or re-targets (0x5C7100).
- 8: removed (0x4A40A0, which reports the figure dead to the model) when its 1200 clip has played or 3.0 s have
  passed (the float at 0xA0A144), or at the time limit.
- 0xF -> 3; 0x10 -> 0xB or 8 at the limit, else 8 when a melee figure has strikes left; 0x11 re-targets when its
  run is done.
- **End of a figure** (0x7556C0): when the clock has run out, or the figure is flagged to stop, it is refaced. A
  surviving figure goes to 0x12 victory, about three times in four (`random(100) > 25`), when its last action was
  1600 or 1000, it has 1800 enabled, it is not flagged 0x8000, and its **opponent's** planned deaths (+0x2B84) are at
  least the opponent's standing figures (+0x34): the opponent's unit dies `[C]`. All other figures go to 0xB.
  (Corrected 2026-11-22 by W50, 4741: the test reads the opponent's participant; was "its side's count at +0x34 is not
  above +0x2B84. Reading that last test as 'its side won' is `[I]`".) While the attacker or the defender has planned
  deaths or an owed hit open and the clock is within the limit, no figure is ended (13.4), and a figure under a
  projectile (0x200) is not ended while it waits for it.

### 5.4 Deaths: how many figures fall [C inputs, I rule]

The number of figures that die is not drawn at random. It follows the participant's damage percent (final damage x
100 / max hit points, section 2.2) against the figures standing at the start: a model shows fewer figures as its
unit loses hit points (`ArtDefine_UnitInfos.DamageStates`, the model's own damage states). `LastToDie` (one member in
VP's database) keeps a figure standing until last `[I: name only]`.

**How many figures one hit strikes** `[C]` (W33, 2026-11-20, read-only headless Ghidra 12.1.4 on a scratch copy of the
`civ5` project, old DX11 EXE stamp 0x546CD0A8; behaviour only, D83). The weapons' `VisKillStrengthMin` / `Max`
(`ArtDefine_UnitMemberCombatWeapons`; the database reader 0x6EDF90 stores them as floats, a value below 0 as 0; e.g.
10-20 for the archer's flaming arrow, 25-50 for artillery, battleship and catapult; NULL for most melee weapons and
for the machine gun) do not take a share of the damage away. They set **how many figures a single hit strikes**.
The count function 0x6AA060, called both when a projectile is launched (0x759110) and when a melee hit lands
(0x686F30), works like this:
- when `VisKillStrengthMax` is 0, the hit strikes one figure;
- otherwise it takes a fraction: `Min` when `Max - Min` is below 1.0 (the constant at 0x9F72F4), else `Min` plus a
  random value in `[0, Max - Min)`. The hit strikes `ceil(fraction x 0.01 x F)` figures, at least one, where F is the
  target participant's figure count (participant slot + 0x30, all of its figure records);
- the attacker's hit strikes all F figures in one case: a fight flag at simulation + 0xC14F4 is set. The flag was not
  named; it is read as the area or nuclear case `[I]`.

The figures struck (0x5E2BB0) are the aimed figure plus the nearest other figures of that participant, by squared
distance from the aimed figure. A candidate is skipped when it is dead (flag 0x10), dying (0x4), already the target
of a projectile in flight (0x200, which the chooser sets) or the protected survivor (0x4000000). Their number is capped
at the candidates there are, and at 32. Each struck figure is then resolved on its own (13.4). For a 14-figure
infantry unit, artillery (25-50) strikes 4 to 7 figures per shell. This explains why a stock kill by a two-gun
artillery unit ends in one or two volleys, while a core that strikes one figure per landing needs seven
(`unit-combat-server.md` 18.2). Replaces (2026-11-20) the reading "set how much of that percent one hit takes away
[I: the consumer was not read]" and the line "the last figure of a killed unit dies at the end" (13.4 now has the rule).

### 5.5 Ranged, melee, city, air and area [D for the data named, C where an address is given, I for the rest]

- **Melee:** both sides strike when the defender retaliates (`GetDefenderRetaliates`). Approach by run (1400) or
  run charge (1120), then charge attack (1140) for figures with `RushAttackFormation` / `ReformBeforeCombat` `[I]`.
  The winner's advance is the separate 0x4D `Moved` message after the combat.
  *2026-11-22 (W51, 4745, `unit-combat-server.md` 18.15 [C]): the melee fight runs by fight type and phase: a reform
  (phase 1, `ReformBeforeCombat` 1 attacker / 2 defender), a turn (phase 2, ships only), then the charge (type 2: an
  attacker without a `HasShortRangedAttack` member; the defender waits for the attacker's first landing) or both sides
  striking where they stand (types 1 and 4); the survivors walk home before `EndCombatSim`. The charge reading above
  holds for type 2 only.*
- **Ranged:** the attacker's figures play 1180 bombard. Their projectiles are flights handed to the Projectile
  system (`Transfer` triggers in `.ftsxml`, `ProjectileSpeed`, `HitEffect`, `HitRadius`, `MissTargetSlopRadius`).
  The defender plays 1280 bombard defend and does not strike back unless it retaliates. The time limit is 20 s.
  The hit moment is the projectile's arrival `[I]`.
- **City:** a city as target or attacker is a participant without figures (0x4024F0, by plot). Figures attacking
  it play 1160 attack city (melee) or bombard, with the `Vs_City` weapon (the flaming arrow, `Usage Vs_City
  ShortRange` in the templates). The city's own strike (`GameplayCityCombat` with the city as attacker) has the
  string `InitCityRangeStrike` (0xA37C9C) beside the combat event names `[I]`.
- **Air:** air units' templates enable `Idle Attack Bombard Death Run`. `AttackAltitude`, `TargetGround` and
  `IsDropped` (bombs) come from the weapon and combat rows. An interceptor is participant 2.
- **Nuclear and area:** the damage members become participants 3 to 67. The record is a barrier (section 3.3).

## 6. The data that drives it [D]

- `ArtDefine_UnitMemberCombats` (364 rows in VP's database): `EnableActions` takes 7 distinct values. The most
  common are `Idle Death BombardDefend Run` (112, the defenceless units) and `Idle Attack RunCharge AttackCity
  Bombard Death BombardDefend Run Fortify CombatReady Walk AttackCharge` (109, the soldiers). Over all rows, Idle,
  Death and Run are in 364, Attack and Bombard in 250, RunCharge, AttackCity, Fortify and CombatReady in 237,
  AttackCharge in 109, AttackSurfaceToAir in 52. Flags: `HasShortRangedAttack` 167, `HasLongRangedAttack` 63,
  `HasLeftRightAttack` 48, `HasStationaryMelee` 74, `HasRefaceAfterCombat` 288 (of 364), `ReformBeforeCombat`
  1 in 121 rows and 2 in 15, `HasIndependentWeaponFacing` 57, `OnlyTurnInMovementActions` 40,
  `RushAttackFormation` `DefaultCavalry` 14 and `ChariotElephant` 1, `LastToDie` 1. Rates: `ShortMoveRadius` 12 or
  24, `ShortMoveRate` 0.35, `TurnRate*`, `TurnFacingRate*` 15-20, `TargetHeight` 8 / 12 / 65.
- `ArtDefine_UnitMemberCombatWeapons` (377 rows): `Index` 0-2 per member, `WeaponTypeTag` (FLAMING_ARROW 90,
  EXPLOSIVE 79, METAL 60, BULLET 51, ...), `VisKillStrengthMin/Max`, `ProjectileSpeed` (46 rows), `HitEffect`
  (`ART_DEF_VEFFECT_*_IMPACT_$(TERRAIN)`), `HitRadius`, `ContinuousFire` 5, `WaitForEffectCompletion` 2,
  `TargetGround` 13, `IsDropped` 10, `AreaDamageDelay` 0.5 (2), `MissTargetSlopRadius` 10 (113).
- The templates in `Assets/Units/Civ5ArtDefines_UnitMemberTemplates.xml` explain the rows. `ART_DEF_TEMPLATE_BASE`
  enables `Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify CombatReady` with
  `bHasRefaceAfterCombat`. The soldier adds `Walk AttackCharge Victory`, `fDeathRotationAmount 45`,
  `fTargetHeight 8`. The archer adds `bReformBeforeCombat`, short and long ranged attacks, an ARROW weapon and a
  `Vs_City` FLAMING_ARROW one. `fDeathRotationAmount` is read by 0x790F70 and `fUnitLoopCombatHeartbeatLength` (the
  combat audio loop) by 0x907620 / 0x9135A0 `[C for the readers]`.
- **Formations:** `Assets/Units/UnitFormations.xml` (loose in the install; the EXE names
  `Assets\Units\UnitFormations.xml` at 0xA21344) holds 24 `<Formation>` entries (corrected 2026-11-15 by 4153; was: "25 entries", which counted the root `<Formations>`). Each has `SpacingX`, `SpacingY`,
  `SpacingMultiplier`, `PerRowInc`, `YOffsetInc`, `YOffsetCurved`, `Stagger`, `UnitRandomness`, `BlockRandomness`,
  `TotalWidth`, `X/YBlockSize`, `X/YBlockSpacingCount` and `CenterWidth`. The names: DefaultMelee, DefaultCavalry,
  Warrior, Archer, Scout, Phalanx, Pikeman, HonorableGunpowder, ThreeBigGuns, TwoBigGuns, Vehicle, ChariotElephant,
  Samurai, UnFormed, Barbarian, LooseCivilian, Melee Front (one name; was listed as "Melee, Front"), BomberWing, FighterWing, EarlyGreatArtist,
  EarlyGreatMerchant, OffsetPikeman, ThreeInLine, TheeInLineStag. `ArtDefine_UnitInfos.Formation` uses 23 of them (corrected 2026-11-15 by 4153; was: "uses 24 of them"); "Melee Front" is unused, and the 64 rows with an empty (43) or NULL (21) formation get the EXE's built-in "Default" record, so the sheet's 24 formations are 23 from the file plus Default (`unit-formations.md` sections 0-2)
  (VP: DefaultCavalry 42, UnFormed 32, TwoBigGuns 24, HonorableGunpowder 16, Archer 12, ...). This is the data the
  Architect's premise K5 asked for (item N4): it is found, and the placement rule is described since 2026-11-15 in `unit-formations.md` (4153).
  `CIV5MultiUnitFormations.xml` (Expansion) is gameplay (armies of operations), not this.
- **Hit and trigger moments:** the `.ftsxml` Action triggers (`model-reader.md` 6.4 (d), 1,278 of them, e.g. the
  warrior's 1100 at 0.4889 s and 2100 at 0.4741 s) are the strike moments that reach the simulation's slot 4.
  `Transfer` triggers (603) hand a projectile to its effect. Sound and Effect triggers are for audio and particles.

## 7. Where Quick Combat changes it [C]

- **The DLL:** with `CvPreGame::quickCombat()` true it makes no `lookAt`, no `GameplayUnitCombat` /
  `GameplayCityCombat` call and no `setCombatUnit`, and the result is `ATTACK_COMPLETED`. `GetPostCombatDelay()` is
  110 instead of 1, so the attacker's mission timer runs longer after a quick combat. The same `ResolveCombat`
  runs either way (0316).
- **The EXE:** no 0x59/0x5A records, no simulation and no hold. The `SetDamage`, `DestroyedInCombat`, `Moved` and
  `Destroyed` records are dispatched in the next frame, and the unit models show them by themselves: the damage
  state, the death of the whole unit, the move.
- **Visualise without Quick Combat:** with Quick Combat off but the target plot not visible to the active player,
  the DLL still calls `GameplayUnitCombat` with `GetVisualizeCombat` false. The EXE posts both records, but the
  simulation refuses to start (section 4.1, the visualise byte), so nothing is held. 0316's third
  `GameplayUnitCombat` call in the AI turn may be such a case `[I]`.
- **The options:** the in-game options set it with `Network.SendGameOptions` (0316 section 1). The EXE keeps
  `SinglePlayerQuickCombatEnabled` / `MultiplayerQuickCombatEnabled` as user options (strings 0xA1A40C, 0xA1A4BC,
  "Combats resolve without animation.").

## 8. What the server has to reproduce (for C2 and C3)

These are consequences of the above, for the later code ticket `[I]`:
1. A visual queue per unit key, fed in the DLL's call order, in which a combat holds its units until its
   simulation ends. Messages about other units pass. A barrier mode covers the human's view of the AI turns and
   nuclear attacks.
2. Damage, death, removal and advance of a unit wait for that unit's combats to finish, as `SetDamage`,
   `DestroyedInCombat`, `Destroyed` and `Moved` do in the EXE.
3. The time limit per combat (12 s, 20 s when an air unit takes part; corrected 2026-11-17, 13.2) is the EXE's catch-up rule. A tighter rule
   would be a departure from stock.
4. The flags' hide and show follow `RunCombatSim` and `EndCombatSim`. If the server takes the combat over while
   the EXE's Combat system stays off, `EndCombatSim` reaches Lua one frame after `RunCombatSim` today, not at the
   end of the server's animation. Either the server raises the end itself or the flags show during the server's
   combat (the bridge of 0178 can raise it with `Dispatch`).
5. The figure state machine and its action codes (5.2, 5.3) on top of the playback layers L2-L4 of the sheet. The
   death count follows the damage percent, and the victory pose is chosen with a 75 % chance.

## 9. The product path today, for reference

*Since 2026-11-15 (issue 0368, the owner's decision D147) the product keeps Combat, Projectile and Particle ON and the EXE plays
its own combat simulation over the server's picture (`research/02-exe-internals/disable-systems-and-masks.md` 5, "Combat,
Particle and Projectile on in the product"); the paragraph below describes the product before 0368 and the state 0042 and
0328 aim to return to when the server draws combat.*
`proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2` turned Combat, Projectile and Particle off until 0368. With them off,
`RunCombatSim` is dispatched (the Lua flag code hides the flags), no simulation starts, `EndCombatSim` follows one
frame later (the flags show again), and the unit records pass at once. The units the EXE still draws show only
the damage states, and a unit killed in the fight is removed at once with no death clip, as with Quick Combat on
(corrected 2026-11-20 by 4799, 9.1 `[L]`; was "the damage states and the death of the whole unit"). The camera still
moves to the target (0316). The proxy carries the combat call (0316 section 4).

### 9.1 The EXE's own figure with Combat off, and its timed triggers (ticket 4799, spike S-C, 2026-11-20)

Route S (D210) turns Combat and Projectile off again. This section answers the Architect's route S note
(`research/07-architecture-decisions/architect-2026-11-20-route-s-combat.md` 0.5 (b), 1.6, premises S3 and S4): what
the EXE's figures still play when the damage, the death and the advance reach them, whether their timed triggers can be
taken away by a data write, and who owns the death's sound and effect. Written by w3-0042 (account B, branch `w3-4799`).
Static part read 2026-11-20 03:50-04:15 EST on the old build's analysis copy (`reverse-engineering-setup.md`, PE time
stamp 0x546CD0A8 checked by the scripts; llvm-objdump 19 listing); addresses only, no code (D83). Starts from 4771's S1
(`unit-combat-server.md` 0).

**How a model's timed triggers fire (S4)** `[C]`:

- Each `.ftsxml` is loaded once into a shared trigger system object (`FGrannyTimedTriggerSystem`, made by the generic
  XML object loader for the `trigger_system` element). It keeps its triggers sorted in one list: the list pointer at
  **+0x78** and the count at **+0x7C**.
- For a clip being played, an instance (`FGrannyTimedTriggerSystemInstance`, 0x20 bytes, vtable at 0xA9B3D4 in
  `.rdata`; its slot 3 is the type test on that class name) is built by a factory taken from a GUID-keyed table of
  creators (a pointer at 0x9FBEC8 in `.rdata`). Its constructor (0x8F3780) asks the shared system for the clip's range
  of triggers (0x8FBD00: no triggers when the shared count at +0x7C is 0, else a search by the clip's key) and keeps
  that range's pointer at **+0x18** and count at **+0x1C**; nothing else in the class writes them after construction.
- Every frame the model's animation update (0x8E8F80, called by the unit model's override 0x5D1EE0 after it informs an
  object at its +0xD8) finds the clip's attached instance through a lookup table at the model's +4 (the attached object
  at entry +0x10), asks it for the instance class (slot 3) and calls its advance (0x8F36A0). The advance **returns at
  once when the list pointer at +0x18 is 0**, resets when the clip time went backwards (slot 8), and for each trigger
  whose time window lies between the last and the current clip time calls the instance's fire slot 10 (0x8F3660). That
  builds an event and calls the trigger object's own slot 7 (+0x1C): `FTimedTriggerSound`, `...Effect`, `...Action`,
  `...Transfer` each fire there.

**What a data write can do (S4)** `[C for the fields; I for the effect, not run]`:

1. **The shared system's count at +0x7C = 0** (one write per loaded `.ftsxml`, after it is loaded): every clip instance
   built afterwards gets no triggers, so that model plays no Sound, Effect, Action or Transfer trigger. Instances
   already built keep theirs until their clip ends. This is the field asked of lead-0590 as build-table entry
   `ftts_system_trigger_count` (thread 0590, #7690); the class's vtable is the anchor to find it on build 26577625.
2. **An instance's count at +0x1C = 0** (or its list at +0x18 = 0): the same for one clip instance only. Instances are
   built per clip, so a write at a unit's creation does not cover its later clips.
3. **Kind by kind:** a trigger object's vtable pointer swapped for a copy whose slot 7 is a no-op (a heap vtable write,
   an approved means) silences Sound triggers and keeps Action triggers.
4. **Not found:** the path from a unit (or its model) to its shared trigger systems. The systems are created by the
   generic loader, and the model keeps its clip instances in the lookup table at +4. Until that path is known, item 1
   needs the systems found some other way, for example by their vtable in a heap walk after the load `[I]`.
5. **Muting all four kinds is not free** `[I]`: Action triggers (1,046 in the reached unit files, `effects-formats.md` 6)
   may drive what the EXE's figure does mid-clip. Under `ServerUnits = 1` the EXE's figures are not drawn, so a lost
   action is unseen, but this has not been checked.

**What the EXE's figure plays with Combat and Projectile off (S3), live** `[L, 2026-11-20 04:14-04:38 EST]`:

- **Configuration:** c5h-b, old build, the 4771 layout (`unit-combat-server.md` 2.1), arm s0 (`ServerUnits = 0`, so the
  EXE draws the units and its figures can be seen; config.ini `Combat = 1`, `Projectile = 1`, Particle on). Quick
  Combat off. Copy `C5H-4799-TINY_0018` of `C5H-3000-TINY-T0018`. No Lua reveal: every defender stood on a plot that
  the human already saw at the load, so the server's fog clouds did not cover it (4771's problem). Pictures were
  PrintWindow bursts every 0.3 s (some frames flat grey or without units, E9), and the EXE's audio session peak was read
  every 0.1 s. Driver `proxy/evidence/0042/4799/run_4799.py`, chain `chain_4799.sh`. Launches: r1 04:14-04:29 (s0, then
  u0, the shipped product with Combat on, as the reference; its scenes 6-7 found no plots and did not run) and r2
  04:30-04:38 (s0 again, all 7 scenes, plots allowed side by side). Each launch was restored by SHA-256 (provision
  verify 0), with no box. Evidence in `proxy/evidence/0042/4799/r1/` and `r2/` (per-scene Lua events, the audio peaks
  per 0.5 s); the pictures are under `C:/c5-64bit-work/0042/w3/4799/` (DIAGNOSTIC, not in git); contact sheets come
  from `sheet_4799.py`.
- **Damage (ranged, the warrior from 35 to 60):** in the frame after the order, the figures above the new damage state
  fade out where they stand (2 of the unit's figures went within 0.3-0.7 s; no fall, no death clip). The damage number
  ("-25", red) rises over the unit from +1.0 s to +2.2 s. No sound but the selection click. No hit particle was seen.
- **Death (ranged kill from 70 and from 75, melee kill from 85; two launches r1 and r2, 8 kills with Quick Combat off and
  on):** `SerialEventUnitDestroyed` reaches Lua 0.23-0.26 s after the order. The defender's figures are gone in the
  next picture (+0.43-0.44 s), shadows too: **no death animation, no death sound, no effect**. The meter shows the
  selection click (0.14-0.17 at +0.5 s) and then no peak above 0.05, against 0.38-0.52 for the same fights with Combat
  on. No damage number over a killed unit.
- **Advance (melee kill):** the attacker stands on the defender's plot one second after the order (Lua: 9,25 -> 8,25
  with Quick Combat off, 9,20 -> 9,21 with it on). Its figures stand there in the picture at +0.43-0.74 s: shown at
  once, with no walk seen in the 0.3 s frames. The attacker's own damage number ("-3", blue) and its XP text rise over
  it. A low sound (peaks 0.02-0.05) is in the meter from about +3 s to +7 s after both melee kills. The peak meter
  cannot separate that from the coast's ambient sound at that camera; it may be the advancing figures' walk sound.
  Only the Miles log of the note's 1.2 would tell.
- **The reference with Combat on** (arm u0, the same scenes) `[L]`: the defenders bombard-defend, are hit at about +3 s
  (dust), fall in their death clips at +3.7 s and are removed at `EndCombatSim` + one frame (+4.0-4.6 s), with the
  fight's sounds (peaks 0.38-0.52) until then.
- So, **with Combat off, a figure killed in a fight is removed, not killed on screen** `[L]`. This corrects the reading
  of this section's paragraph above and of the route S note's 0.5 (b), which expected "the death of the whole unit" with
  its sounds and effects (`[C]` for the call, `[I]` for what it shows). The note's collision (1.6, S3) does not occur.

**Who owns the death's sound and effect (the note's 1.6)** `[L for the EXE's part; I for the rule]`: the server, alone.
With Combat off the EXE's figure plays no death clip, sound or effect for a unit killed in a fight, so the server's
choreography sends the death clips' Sound and Effect triggers itself and nothing sounds twice. The EXE keeps the parts
that are not clips: the damage numbers (interface text, shown at once at the damage event, so under the hold of the
note's 3.1 they appear at the server's end of the fight), the selection click, and the camera's move to the target.
Item 2 of the note's 1.6 (taking the EXE's triggers away) is not needed for deaths. It stays an option for the walks
(1.8), with the field above.

## 10. Open premises and their tests

| # | Premise | Status | Test |
|---|---|---|---|
| Q1 | The queue's ordering byte (+0x30, at 0x2A10E38 + 0x4C * context) is set in a normal game, so records really wait | `[C]` for the only two writers: 0x4031F7 clears it and 0x403492 sets it to 1, both in the state switch at 0x4031DB (12 states at [esi+0x10]); `[I]` that the set is the "game running" state, since it follows `GetActivePlayer` | cdb on a stock game copy: break on 0x60EB10 during a visualised attack and read `[ecx+0x30]`; or the Lua test of Q3 |
| Q2 | `Moved` (0x4D) and `DestroyedInCombat` (0x47) name their unit in words 2/3, as `SetDamage` does | `[C]` for 0x59 and 0x48 (3.3); `[I]` for 0x47 and 0x4D | cdb: dump the 13 header words of the records at 0x87E640 and 0x87DEF0 for one attack |
| Q3 | Per unit the picture runs a combat behind: `EndCombatSim` and `SerialEventUnitSetDamage` reach Lua only when the animation ends | `[L]` 2026-11-20 (ticket 4771, `unit-combat-server.md` 0-1; c5h-b, old build, Tiny, Combat on, Quick Combat off): Run -> End 4.033-4.866 s for an archer's volley (16 fights), 4.534-4.767 s for a melee (4); `SerialEventUnitSetDamage` one frame after End; of two attacks in a row on one defender the second Run comes one frame after the first End. With Combat off Run -> End is one frame; with Quick Combat on there is no Run / End; was `[I]` | stock configuration (Combat on), Quick Combat off: a Lua listener stamps `os.clock()` on `RunCombatSim`, `EndCombatSim`, `SerialEventUnitSetDamage` and `SerialEventUnitDestroyedInCombat`. Attack once, then twice in a row with a blitz unit. Expected: End - Run = the animation length (seconds), and the second Run after the first End |
| Q4 | During the AI turns visible combats are strictly serial and other units' moves wait (the barrier) | `[L]` 2026-11-17 (12.3): two AI strikes ran one after the other, later Lua events waited, and the game update itself stopped until the queue drained; was `[I]` | the same listener over one AI turn with several visible combats; also time the turn against Quick Combat on |
| Q5 | The 12 s / 20 s limits are the only catch-up | `[I]`; the 20 s limit `[L]` 2026-11-17 (12.2: 10 air strikes on a submarine ended at 20.03-20.07 s) | a long ranged combat (several archers against a city) with the listener; End - Run must stay at or under 20 s |
| Q6 | The death count follows the damage percent (and `VisKillStrength`) | `[I]`; 2026-11-20 ticket 4771 tried it from pictures (3 damage levels, c5h-b) and could not count: the defenders stood under the server's fog clouds and PrintWindow frames at that zoom are not countable (`unit-combat-server.md` 0); still open | the pose tap (sheet 3.3) or RenderDoc on the standing figures before and after a combat at 3-4 damage levels; compare with `final damage / max HP` x figures |
| Q7 | Victory is played by about 75 % of the eligible survivors | `[C]` for the branch | count 1800 poses over about 20 combats with the pose tap |
| Q8 | What sets 0x488B30's deferral bit (+0x128) | unknown | read the writers of +0x128 on the Unit system object |
| Q9 | On the product path the flags hide for one frame only | `[I]` | 0316's driver with a Lua listener on both events |
| Q10 | The flag word of 0x59 depends on the active player's human, alive and turn state as described | `[C]` for the slots, `[I]` that the object is `ICvPlayer1` | cdb: break at 0x67DED1 / 0x67DEDF during the human's turn and during an AI turn |

## 11. Not covered

- The per-pass work of the simulation (0x6C8FD0, 0x5EFC50, 0x6902C0, 0x613940, 0x51F3B0): movement of figures to
  their positions, facing and turning (`TurnRate*`, `MoveRate`), and reforming.
- The consumer of `VisKillStrengthMin`/`Max` and the exact death-count rule; `LastToDie`; `HasLeftRightAttack`,
  `HasOpponentTracking`, `HasCollisionAttack`.
- Formation placement from `UnitFormations.xml` (item N4).
- The Projectile and Particle systems (effects, item N8), the city's ranged strike, air sweeps and paradrops.
- The full name table of event types 0x46-0x4D (only 0x59 and 0x5A were checked).
- Nothing was run live. Every timing claim above is from constants in the code. Section 10 lists the runs.
  *(2026-11-17: section 12 has the first live runs. They confirm the 20 s limit, and Q4's barrier in the AI turns,
  live.)*

## 12. The bomber loop and the AI-turn freeze, reproduced on stock (ticket 0456, 2026-11-17)

The owner's report (D169, `research/07-architecture-decisions/architect-2026-11-16-increment-4-extras-epic.md` 4.5): bombers
[reworded for privacy] fly away off the map and the game freezes while the faulty animation repeats. Ticket 0456
asked for a reproduction on stock and for where the turn waits. No fix was made here.
Work 2026-11-17 00:43-02:05 EST, implementer on account B. Marks: `[L]` seen live, `[C]` read in the EXE's machine code (the
workstation's analysis copy, `reverse-engineering-setup.md`, and the live process through cdb; addresses only, D83), `[I]`
inferred.

### 12.1 Configuration and method

- **Where.** oldpc, through the testbed (`tools/testbed/RECIPE-queue-place.md`). The machine's own Steam EXE ran in
  instance tb2 (job `20261117-005112-17aa`) and then tb1 (job `20261117-012523-0375`). DLL: the pin `i0ship-26234371-ia`
  (`3c2f47da...a06c`, `quiet-testbed.md` 24), with the modpack mods cp, vp and eui. Window 1920x1080, frame cap 30.
  The run used the live-session job (`live_session.py`, `live.py`).
- **Scene.** Save `REL548-TINY_0008` (Tiny, turn 8, SHA-256 `b2ceb663...`). The human is Polynesia, capital at 51,14.
  Quick Combat and Quick Movement were switched off with the options menu's call
  (`Network.SendGameOptions`) and read back false. Everything else was set up through the Lua channel:
  - two human air units in the capital. A city holds two; with six there, `CanRangeStrikeAt` was false for all of them,
    because `canRangeStrike` requires `canEndTurnAtPlot` `[L]`;
  - barbarian targets around the capital (infantry on land, a destroyer, two submarines, an embarked infantry);
  - the area made visible to the human (`Plot:ChangeVisibilityCount(team, 1, -1, true, true)`).
  - For the AI turns: war with Carthage (player 1, capital 36,18), two AI `UNIT_BOMBER` in its capital, and three human
    submarines, or for the control three human infantry, 2-7 tiles away. The AI team also saw invisible units there.
  The scene is deterministic: the second session gave the same unit ids and plots.
- **Measures.**
  - A Lua listener in the InGame state stamped `os.clock()` (wall seconds) on `RunCombatSim`, `EndCombatSim`,
    `SerialEventUnitSetDamage`, the AI-processing events and `ActivePlayerTurnStart/End`, as Q3 proposed.
  - The Lua channel itself served as a probe of the game update. It is polled at the top of `CvGame::update`, so
    "no answer within 15 s" means `Update` is not being called.
  - cdb thread stacks (non-invasive attach).
  - Picture series from the new `burst` command of `live_session.py`: a Lua order, then PrintWindow frames in the
    job's process, 0.5 s or 1 s apart.
  The scripts and logs are in `server/evidence/0456/`. The frames are on F: and in Visual Checks (issue 0456,
  DIAGNOSTIC).

### 12.2 The loop: an air strike on a submarine always runs to the 20 s limit [L]

`RunCombatSim` to `EndCombatSim`, as Lua received them:

| Attacker | Target | Turn | Run -> End (s) | Runs |
|---|---|---|---|---|
| Heavy Bomber (`UNIT_BOMBER`) | submarine | human | 20.03, 20.07, 20.07 | 3 |
| Stealth Bomber | submarine | human | 20.03 | 1 |
| AI Heavy Bomber | human submarine | AI | 20.04, 20.07, 20.05, 20.07, 20.04, 20.07 | 6 (3 AI turns, 2 strikes each) |
| Heavy Bomber | infantry on land | human | 6.77 | 1 |
| Heavy Bomber | embarked infantry | human | 6.30 | 1 |
| Heavy Bomber | destroyer | human | 2.20 | 1 |
| Battleship (ranged, naval) | submarine | human | 3.37 | 1 |
| AI Heavy Bomber | human infantry | AI | 5.16, 5.10 | 2 |

So the loop is **an air unit's strike on a submarine**. All ten such strikes ended at the simulation's 20 s limit
(section 4.2: 20.0 s once an air figure takes part, 13.2), never by themselves. Other targets, and a ship firing on the
submarine, end in 2-7 s. The embarked unit did not loop, although Civilization: Beyond Earth players reported that case
(forum, 2014).

**What it looks like** (series b1, 50 frames 0.5 s apart; series b2, 70 frames 1 s apart; DIAGNOSTIC). The DLL's
`lookAt` centres the camera on the target. The two bomber figures of the BomberWing formation then fly passes over the
submarine's hex. A bomb explosion and its water splash appear on the hex about every 1-2 s, from +3 s to +19.9 s: about
a dozen passes, the repetition [reworded for privacy] the owner reported. The submarine's figure is not visible during the strike. At
+20.4 s the bombers are gone at once, with no flight home, and the damage number (-31) and the flag appear. In the AI
turn the corner shows PLEASE WAIT through both loops (b2, +2 s to +45 s).

**Not reproduced:** a bomber flying away "into the atmosphere". In about 12 strikes at the default camera height, the
bombers stayed around the target hex. Candidates not tried:
- another camera height, or the strategic view;
- a bomber intercepted by a fighter or anti-air (participant 2);
- a strike from a carrier;
- a target across the map's wrap (the map wraps in X);
- a bomber killed in the strike.

**Why the submarine** `[I]`. In the database (VP, read through `GameInfo` in the game), the submarine's member
`ART_DEF_UNIT_MEMBER_SUBMARINE` has no `TargetHeight`. Its `EnableActions` include `BombardDefend` and
`AttackSurfaceToAir`. The bombers' members (`ART_DEF_UNIT_MEMBER_BOMBER`, `..._STEALTHBOMBER`) enable `Idle Attack
Bombard Death Run`, with `AttackRadius` 64. The destroyer's member also lacks `TargetHeight` but does not loop, so
`TargetHeight` alone does not explain it. **Corrected 2026-11-17 by 0477 (section 13):** the bombs do hit the
submarine (9 hits registered on its figure in one strike `[L]`); the strike waits for the submarine's return fire at
the bombers, which its model cannot play. Replaced reading `[I]`: "the bombs explode at the water surface and never
register as a hit on the submerged figure".

### 12.3 Where the turn waits: the EXE stops calling `Update` until its event queue is empty [L, C]

- **In the human's own turn nothing waits.** During each 20 s loop the Lua channel answered, for example at +9.2 s
  into run s5 `[L]`. Only the units involved wait, as 3.4 says.
- **In the AI turn the whole game waits for the loops.** After the human ended the turn, the first channel request got
  no answer for its whole 15 s, in both runs that probed it (a1, a2). The next answer came 2 s after the second
  strike's `EndCombatSim`. The other AI players (2, 3, the city states, the barbarians) then finished within 3 s, and
  the human's next turn started about 5 s after the last loop ended. For example, run a2: end turn 1439.5, strikes
  1440.16-1460.21 and 1460.25-1480.31, the channel answered at 1482.28, `ActivePlayerTurnStart` at 1485.5 `[L]`.
  - Series b2: the AI turn took 45.9 s, 40.1 s of it the two loops.
  - Control a3 (AI bombers on land targets): the AI turn took 16.7 s, and the channel also stayed silent through
    both 5 s strikes (the first answer came at 750.88, 1.8 s after the second strike's end).
  So **every visible AI combat stops the game's update for its whole animation**, and the submarine bug makes each
  strike cost the full 20 s. Ten AI bombers striking submarines in a late game are 200 s of a frozen turn `[I]`.
- **Not the DLL** `[L]`. cdb, taken during the first loop of a2:
  - The game-core thread (start `CivilizationV_DX11+0x42A360`) was in `ZwSuspendThread`, called from its thread
    procedure at RVA 0x42A3F4 (VA 0x82A3F4), as between two updates (`research/02-exe-internals/game-core-thread.md`).
  - None of the 35 threads had a frame in `CvGameCore_Expansion2`.
  - The main thread was running frames.
  So the game-core thread was idle because the EXE did not ask it for an `Update`; it was not stuck inside one.
  VP's DLL does not wait for a combat either: it resolves at once (`research/03-dll-engine-boundary/combat-on-the-product-path.md`
  1). `hasBusyUnit` stayed false.
- **The gate in the EXE's frame function** `[C for the code paths; I for the meaning]`. The frame function holds the
  update request 0x53AF10 at VA 0x7258E5, the call that `game-core-thread.md` 0 names. Its call is skipped when a
  local flag ([esp+0x11]) is set. Two places set that flag, and both do so only when the predicate **0x4FB9F0** is true
  for the current context's event queue B (`0x2A10E08 + 0x4C * [0x2A10200]`, the combat queue of section 3).
  - **0x4FB9F0 is "the queue is not empty"** `[C]`: the dispatch buffer still holds records, or, under the queue's lock
    (+0x34), the write buffer does. A held `RunCombatSim` record (3.2) keeps it non-empty for the whole simulation.
  - **First place, 0x7256E6.** It is reached when the per-player-slot scan (64 slots; two virtual calls per slot,
    +0x54 and +0x58) finds a slot different from the one stored at `[obj+0x8CC]`, read as "the player whose turn it
    is has changed" `[I]`. If the queue is not empty, the update is skipped and the new slot is not stored, so the
    check repeats every frame until the queue drains.
  - **Second place, 0x725762.** It is reached when a computed "may update" state differs from the byte at 0x2DE0447
    (read by 0x5426A0) and that state is true.
  The data fit the first place: the AI player owning the bombers finished, the DLL started the next player, and then
  nothing ran until the queue was empty.
  - **`CanAdvanceTurn` is not this gate** `[C]`. The DLL asks `ICvEngineUtility1::CanAdvanceTurn` (slot 105, thunk
    0x466500 -> 0x76EF20) only at the turn's roll-over (`CvGame::update`, `getNumGameTurnActive() == 0`). It checks the
    per-player-slot table at 0x2DDFE40 (0x6DE690, 0x6A0880: network or slot readiness), not the event queue.
- **For increment 3** (the caution of the Architect's 4.5). The wait is in the EXE: its frame function and event queue,
  on the main thread, with the game-core thread left suspended. Under D144 the EXE's unit and combat simulation keeps
  running unseen, so drawing a correct, short animation in the server would hide the bomber but not shorten the
  freeze. The freeze ends only when the EXE's combat simulation ends. Product path today (D147: Combat on): the same
  freeze as stock `[I: not run on the product path]`.

### 12.4 Next step: the fix ticket (core code, no Extras flag, D169)

Options, for the MC to choose and number (not done here):
1. **DLL: no visualised combat for an air strike on a submarine** (recommended first). In `CvUnitCombat::AttackAir`
   (and the air paths of 2.1), when the defender is a submarine (`getInvisibleType()` is the submarine type, or the
   art member has the submarine's combat class), call `setVisualizeCombat(false)`. The EXE then posts both records,
   the simulation refuses to start (4.1, the visualise byte), nothing is held, and the damage and flags show at once.
   The camera still moves (`lookAt`). It is small, in the DLL, and goes upstream as a plain bug fix. It removes the
   animation of that one case. The other combats keep stopping the AI turn for their 2-7 s.
2. **Find why the bombs never hit the submarine's figure**, and give the figure a hit point (art data: `TargetHeight` or
   the submarine member's combat row). This is the faithful fix (D66). It needs the hit code of 5.3 and 11 and an
   offline harness run, and changing the art defines is a mod data change, not core code.
3. **Shorten the 20 s limit** (the float at 0x9F39EC). That is an EXE data write. Whether CEG covers that `.rdata`
   constant is unknown, and the write would shorten every projectile combat. Not recommended.
4. **The gate itself**: let the update run while the queue holds combats in the AI turn. That breaks the picture's
   order against the game state (3.4), touches EXE behaviour, and belongs to increment 3's C2 (the visual queue per
   unit), not to a bug fix.

Option 1's test is this ticket's scene: the AI turn of run b2 with the fix must take about the control's 16.7 s, not
46 s, and the channel must answer during it.

### 12.5 Incident on the way (not the bomber)

At 01:40:04 EST a fatal "Render Error: FGXRenderer11::Present failed hr=DXGI_ERROR_DEVICE_REMOVED. Driver failure" box
appeared in session 2 (tb1, pid 22080). It was the human's turn 11, idle; no combat had run since 01:38:13. The System
log on oldpc (read-only job `20261117-014757-bfd1`) shows the cause: **Windows Update installed "NVIDIA Display Driver
Update (32.0.16.1088)"**. Started 01:38:51 (event 43), `nvlddmkm` service re-added 01:39:57 (UserPnp 20003),
installed 01:40:03 (event 19). There was no display event 4101 and no TDR. No other testbed job on oldpc carries a
DEVICE_REMOVED [L].
- The box was closed by COMMON 29's fatal path (repair job `20261117-014354-e350`, `stop --after-fatal` rc 0), and
  tb1 was restored (`20261117-014630-34ef`, restore ok, `provision.py verify` 0).
- **oldpc's GPU driver is now 32.0.16.1088.** It was 31.0.15.4592 in 21.4 of `research/06-tooling-and-workflows/quiet-testbed.md`.
  Timing sets from before and after 2026-11-17 01:40 EST are not comparable. The MC was told on 2026-11-17.

## 13. Why the strike on a submarine never ends, and the fix (ticket 0477, 2026-11-17)

Ticket 0477 (decision D176: make the bombs hit; fallback after 48 h). Work 2026-11-17 from 02:37 EST, implementer on
account B. Marks as in 12: `[L]` seen live, `[C]` read in the EXE's machine code (read-only headless Ghidra 12.1.4 on a
scratch copy of the `civ5` project, D83: addresses and behaviour only) or in the DLL's source, `[D]` read in the
install's files or the game database. Scripts: `server/evidence/0477/scripts/`; logs: `server/evidence/0477/logs/`.

### 13.1 In short

1. **The bombs hit the submarine.** In a live strike the submarine's participant had 4 projectile hits launched at it
   by +5.8 s and 9 by +10.8 s, and its figure carried the "has taken a hit" flag `[L]`. 12.2's reading (the bombs never
   register) was wrong.
2. **The strike waits for the submarine's return fire.** When the air unit takes damage in the combat, the EXE has the
   defender fire back: the defending figures are set ready to strike in the strike's opening phase (13.4), and with the
   DLL's `DefenderRetaliates = true` (sent for every bombing run by `CvUnitCombat::GenerateAirCombatInfo`) the attacker
   is also owed one visible hit. A figure on a sea side answers an air attacker with the anti-air action **1130** (13.5).
3. **The submarine models cannot play 1130.** Every ship model's state machine takes a request of 1130 (ships without
   an anti-air clip play an attack instead); the three submarine members (`Submarine.fxsxml`, `NuclearSubmarine.fxsxml`,
   VP's `akula.fxsxml`) use the land units' `1_OMNI_STATE_01.fsmxml`, which has no transition for it `[D]`. The
   submarine's figure stays in combat-ready (1600) with 1130 requested and never strikes; its pending attack and the
   bomber's owed hit hold the simulation until its 20 s limit `[L]`.
4. **Fix (DLL, no flag, no data, no EXE change):** in `CvUnitCombat::AttackAir` the EXE gets a copy of the combat
   information in which, for a defender of combat class `UNITCOMBAT_SUBMARINE` (in VP exactly the three units with
   those models `[D]`), the defender deals no damage in this combat and does not retaliate; the game result still uses
   the real values (13.6). **A first fix that only cleared `DefenderRetaliates` did not work** (10 of 10 strikes still
   20.03-20.07 s `[L]`, 13.6). Commits on the DLL fork, local branches (not pushed, MC #3619):
   `fix-air-strike-submarine-retaliation` **cb85112f1** on i0-ship-batch2 26234371e (the 32-bit stock route) and
   `fix-air-strike-submarine-retaliation-x64` **23bb8cbca** on the x64 pin 04b1211b8 (the product path, a cherry-pick).
   They replace the first fix's c97804c1e / 594ac5aa2.
5. **Known, unchanged stock behaviour:** a submarine deals damage to its air attacker in VP, and that damage is never
   shown as return fire, on stock or after the fix (13.7).

### 13.2 Configuration and method

- **Where.** The workstation, pool instance `c5h-a` (C:, the instance's copy of the DX11 EXE, cloaked by the harness),
  DLL `i0ship-26234371-ia` (SHA-256 `3c2f47da...a06c`, the build 0456 used), mods cp, vp, eui. Save: a copy of
  `REL548-TINY_0008` (`b2ceb663...`) named `C5H-0477-TINY_0008`. Scene: 0456's (`setup.lua`; Quick Combat and Quick
  Movement off, read back false); it gave the same unit ids as on oldpc (bombers 1039/1040, submarines 1043 at 49,11
  and 1045 at 54,14). oldpc's headed place was held (no logged-on session); the MC agreed to the workstation (#3605,
  #3614).
- **Reading the simulation live.** `cdb -pv` (non-invasive) with `scripts/sim.cdb`. The combat simulation system is a
  static object at VA **0x2D701A8** (`0x488B30` calls `0x731FF0` on it); its active simulations are a vector at +8
  (begin) / +0xC (end) `[C]`; each element is a simulation object (vtable 0xA05470, written by 0x5BEBA0 after the base
  constructor 0x6A4A40) `[C]`. The game ran at runtime image base **0x9C0000** (runtime = VA + 0x5C0000), and the
  simulation objects lay above 2 GB `[L]`. Each attach pauses the game 3-14 s, so a strike with a dump is not timed.
- **Times** from 0456's Lua event logger (`RunCombatSim` -> `EndCombatSim`, `os.clock`), the AI turn from
  `ActivePlayerTurnEnd` -> `ActivePlayerTurnStart`. Times measured on the workstation are indications (COMMON 23).
- **The time limit.** A bomber's combat had the 20.0 s limit and its side the byte +0x2D41; a battleship's ranged
  strike on a submarine had 12.0 s `[L]`. So 20 s applies when an air figure takes part, not to every projectile
  weapon (4.2 corrected).

### 13.3 The fields read live [L for the values, C for where they are written, I for the names]

Participant (0x2D60 bytes from simulation + 0x270; 0 attacker, 1 defender, 2 interceptor):

| Offset | Content |
|---|---|
| +0 / +4 | owner, unit id (the bomber 0 / 0x410, the submarine 63 / 0x415) |
| +0x30 / +0x34 / +0x38 / +0x3C | figures, figures standing, figures that cannot attack (flag 0x2000000), damage percent |
| +0x2B84 / +0x2B88 | deaths planned / deaths done |
| +0x2D30 / +0x2D34 | hits in flight (launched, not landed) / hits launched in all |
| +0x2D38 | the participant's own index |
| +0x2D3C.. bytes | +0x2D3E "this side can answer" (a ranged member, or an air side), +0x2D41 air side, +0x2D42 sea side |
| +0x2D48 | **hits still owed to this participant** (13.4) |

Simulation + 0xC14EC.. holds the message payload's flag bytes (payload 0x500 = simulation + 0xC14EC): 0x500 visualise,
0x502 **DefenderRetaliates**, 0x503 ranged, 0x504 bombing mission; read by comparing a bomber's strike (0x502 = 1,
0x504 = 1) with a battleship's ranged strike (0x502 = 0, 0x503 = 1) `[L]`.

Figure record (0x158 bytes from participant + 0x54): +0x10 position (x, y, z; the bombers at z 90, the submarine at
z 10.1), +0x5C member combat record, +0x64 weapon index, +0x74 the action last requested, +0x78 the action the model
plays, +0x94 target (participant << 16 | figure; -1 none), +0x98 flags, +0x9C state (5.2), +0xA0 targeting kind.
Flags: 0x2 strike done, 0x10 takes no part, 0x80 is targeted, 0x100 ranged, 0x200 marked for a projectile hit,
0x8000 air figure, 0x10000 sea figure, 0x200000 has taken a hit, 0x2000000 cannot attack, 0x4000000 protected survivor.

### 13.4 Deaths and owed hits (0x6A9450, called from the frame function 0x5FE650) [C]

Per participant: deaths = figures x damage percent / 100, rounded and clipped; minus figures already out; a unit that
is not killed keeps at least one figure. When figures survive, one of them (a `LastToDie` member first) gets
0x4000000, and every target chooser skips it. Then the owed hits (+0x2D48) are reset to 0 and set to 1:
- for the **defender** when there is no interceptor, or the attacker keeps figures standing: it must take one hit;
- for the **attacker** when it has no planned deaths, the defender's side can answer (+0x2D3E), the combat's
  `DefenderRetaliates` byte is set and the attacker's weapon is not one-shot: it must take one hit from the defender.

A landing hit (0x658890) resolves each figure it strikes (5.4: one or several) through 0x5B4D00 `[C, W33 2026-11-20,
read-only headless decompile of the old DX11 EXE in a scratch project copy; behaviour only]`:
- The struck figure dies when its participant still has a planned death (0x6C5830: "done" is deaths played >=
  planned and owed < 1; the death itself in 0x6C2430 checks played < planned). One exception: the participant's last
  standing figure (standing - played = 1) is spared while the **other** side (0x520230: participant 0 <-> 1, 2 and up
  -> 0) still has planned deaths unplayed or an owed hit open. The spared figure is only hit.
- **Every struck figure pays one owed hit of its participant (decrement while above 0), whether it dies or is only
  hit.** At a death, a participant whose planned deaths equal its standing figures (the whole unit dies) also has its
  owed hit cleared. So an owed hit never outlasts a side's first death, and a killed side's owed hit is never left for
  a figure that no longer stands.
- A struck figure that is not killed loses its "targeted by a projectile" flag (0x200), so it can be aimed at again.

Replaces (2026-11-20) "counts a planned death when one is left, else pays an owed hit", the reading the server's 4775
core was written from; the old reading made a lone striker's kill hold to the limit (`unit-combat-server.md` 18.2).
**The end test
of a figure (0x7556C0) holds every figure in the fight while the attacker or the defender has planned deaths not done
or a hit owed, until the clock passes the limit.**

With `DefenderRetaliates` false, 0x5C7100 puts a figure of participant 1 or 2 into state 0xE (1280 bombard defend)
when it next looks for a target `[C]`. **But the air strike's opening phase decides first** (0x493300, simulation
phase 3, when the attacker's side is air and the defender's is not): when the attacker's damage in this combat (the
message's attacker record, damage field, payload byte 0xE) is 0, the defender's figures go to 0xE and the attacker's
owed hit is cleared; otherwise they are set to state 5, ready to strike, whatever the retaliate byte says `[C]`. A
figure in state 5 strikes as soon as a target is in reach (7165E0, case 5). With only `DefenderRetaliates` false the
submarine still requested 1130 and ended in state 0x10 with an attack pending (+0x80 = 1100, never played), and 0x7556C0
keeps such a figure in the fight until the clock runs out `[L, C]` (dumps of 2026-11-17 03:52, `cdb-f2-*`).

### 13.5 Dropped bombs, and the answer to an air attacker [C; L where marked]

- **The bombs.** A bomber's figure (air, ranged) takes a target from the air chooser 0x6A1040 (the nearest figure of
  the other side, skipping flags 0x4000014). On its `Transfer` trigger 0x759110 launches a projectile: the figures it
  will strike are chosen at launch from the striker's target (0x6AA060, 0x5E2BB0) and listed on the target's
  participant; for an `IsDropped` weapon (weapon + 0x37) the impact point is the striker's own position carried along
  its heading, at ground height (0x530A10), plus the target's `TargetHeight` unless the weapon has `TargetGround`
  (+0x36). On arrival 0x658890 strikes the listed figures. Live, the bomber targeted the submarine's figure (+0x94 =
  0x00010000) and the submarine's participant counted 4, then 9 hits launched `[L]`.
- **The answer.** In state 10 (5.2) 0x512DE0 chooses the action by the sides: against an **air** side a land figure
  requests 1180 (bombard), a **sea** figure **1130** (anti-air). `EnableActions` does not enter that choice. Live, the
  submarine's figure sat in state 10 with the bomber as target, 1130 requested (+0x74 = 0x46A) and 1600 playing
  (+0x78 = 0x640) from +5.8 s to +10.8 s, while the bomber's participant had +0x2D48 = 1 `[L]`. A frigate in the same
  place requested 1130, played 1100 (+0x78 = 0x44C), delivered its hit, and its strike ended in 3.4 s `[L]`.
- **The state machines** `[D]` (`Resource/Common/UnitModels.fpk`, read with `evidence/9012/fpk.py`):
  `ships_state_01.fsmxml` (5 conditions on 1130) and `modern_ship_aim_state_01.fsmxml` (6) take a request of 1130;
  `1_omni_state_01.fsmxml` has no 1130. A census of the 54 sea-domain unit members in VP's database (models from the
  install's archives and VP's mod folder; `scripts/seamodels.py`): 3 use `1_omni_state_01` (the submarine, nuclear
  submarine and attack submarine members, none with a 1130 clip); 13 use a modern-ship machine with a 1130 clip; 35 use
  `ships_state_01` without the clip; 3 civilians (great admirals, work boats) have no attack. So the submarines are the
  only combat ships that cannot answer an air attack.

Bomber strikes on the workstation, stock DLL: destroyer 2.83 s, frigate 3.37 s, ironclad 2.87 s, cruiser 3.23 s
(`logs/series-naval1.json`), submarine 20.03 and 20.07 s (`logs/series-stock-sub.json`) `[L]`.

### 13.6 The fix

`CvUnitCombat::AttackAir` (`CvGameCoreDLL_Expansion2/CvUnitCombat.cpp`, the only file touched), the visualised branch
of a strike on a unit: the engine's `GameplayUnitCombat` gets a copy of the combat information (`CvCombatInfo`
assignment); when the defender's `getUnitCombatType()` is `GC.getInfoTypeForString("UNITCOMBAT_SUBMARINE", true)`, the
copy has `setDamageInflicted(BATTLE_UNIT_DEFENDER, 0)` and `setDefenderRetaliates(false)`. `ResolveCombat` keeps the
original, so the damage both units take is unchanged; the copy only reaches the EXE (and, on the product path, the
proxy). No effect when the class does not exist (a game without VP). The criterion is the MC's (#3619): the submarine
class only, because every other ship answers correctly.

What the EXE sees then: the attacker's damage in this combat 0 (so the opening phase sends the submarine's figures to
bombard defend, 13.4), its final damage unchanged (the health bar, `SetDamage`). Side effect: the Lua events
`RunCombatSim` / `EndCombatSim` carry the attacker's per-combat damage as 0 for these strikes (MC told, #3632 thread).

First fix, replaced (negative result, 2026-11-17 03:44-03:52 `[L]`): `GenerateAirCombatInfo` passing
`DefenderRetaliates = false` for the submarine class (commits c97804c1e / 594ac5aa2, DLL `193BC675...`). It cleared the
bomber's owed hit (+0x2D48 = 0 in the dump) but every strike still ran 20.03-20.07 s, 10 of 10
(`logs/series-fixv1-sub10.json`), because of the opening-phase rule of 13.4.

Why not data: the state machine is named inside the model asset (`Submarine.fxsxml` names `1_OMNI_STATE_01.fsmxml`),
so a data fix means shipping a changed game file (D83); the art-define tables can only point the member at another
model. The MC closed the data route (#3629).

### 13.7 The submarine's damage to its bomber (unchanged stock behaviour)

`GenerateAirCombatInfo` gives the attacker the defender's `CvUnit::GetAirStrikeDefenseDamage`: in VP
(`MOD_BALANCE_AIR_UNIT_CHANGES`) the unit's `BaseLandAirDefense` plus `LandAirDefenseValue`, scaled by the attacker's
interception-defence and damage-taken modifiers, plus a random 0-4 `[C, DLL source]`. Live a submarine took 8 and 5 HP
off a Heavy Bomber `[L]` (`RunCombatSim` attacker damage). On stock that damage is never shown as return fire (the
submarine's figure never fires); the bomber's health bar drops when the combat ends (`SerialEventUnitSetDamage` after
`EndCombatSim`). After the fix the same: the damage is applied and the bar drops at the end; only the 20 s wait goes.
Kept as it is (MC #3629).

### 13.8 Results

Stock route on the workstation (`c5h-a`, 2026-11-17, 0456's scene, Quick Combat off) `[L]`. Times are Lua wall clock
(`RunCombatSim` -> `EndCombatSim`); workstation numbers are indications (COMMON 23).

| Run | DLL | Result |
|---|---|---|
| bomber on submarine, 2 strikes | stock pin `3c2f47da` | 20.03, 20.07 s (`logs/series-stock-sub.json`) |
| bomber on destroyer, frigate, ironclad, cruiser | stock pin | 2.83, 3.37, 2.87, 3.23 s (`logs/series-naval1.json`) |
| AI turn: 2 AI Heavy Bombers vs 3 human submarines | stock pin | **54.9 s**; the two strikes 20.05, 20.07 s; the Lua channel silent through them (`logs/aiturn-stock-ai1.json`) |
| bomber on submarine, 10 strikes | first fix `193BC675` (c97804c1e) | 20.03-20.07 s, 10 of 10: no change (`logs/series-fixv1-sub10.json`) |
| bomber on submarine, 10 strikes | **fix `1F7AFEA0` (cb85112f1)** | **1.87-3.87 s, 10 of 10** (`logs/series-fixv2-sub10.json`) |
| bomber on destroyer, infantry, embarked infantry | fix | 3.07, 6.60, 6.07 s: unchanged (0456 on oldpc: 2.20, 6.77, 6.30) (`logs/series-fixv2-controls.json`) |
| AI turn: 2 AI Heavy Bombers vs 3 human submarines, two turns | fix | **9.45 s and 11.93 s**; strikes 2.00, 2.00 and 2.97, 3.53 s; the channel answered throughout (`logs/aiturn-fixv2-ai1.json`, `-ai2.json`) |
| proxy path: bomber on submarine, 10 strikes | x64 fix 23bb8cbca (server DLL `76625DE3`) through the proxy | **1.87-4.20 s, 10 of 10** (`logs/series-proxy-sub10.json`) |
| proxy path: destroyer, infantry, embarked infantry | x64 fix | 3.10, 5.43, 6.10 s |
| proxy path: AI turn, 2 AI bombers vs 3 human submarines | x64 fix | **9.38 s**; strikes 2.00, 2.00 s (`logs/aiturn-proxy-ai1.json`) |

The proxy-path run is proxy + `server.exe` (`set-proxy`, `mode=on`, the EXE's Combat system on as in D147), not the full
D33 product configuration (no composite, no server renderer); 0 LOUD lines, build ids in `logs/proxy-run-identity.txt`.
The full product check comes with the release candidate of the DLL pin batch that carries this fix (batch 4, MC #3685)
and its human-input test phase. The AI-turn pair on oldpc that turns the workstation indication into a measured number
is ticket 0485, next paragraph. (Corrected 2026-11-17 by 0485; replaced "Not run: an AI-turn pair on oldpc ...
(`live_session.py` cannot ship a DLL yet)": 0485's job ships both DLLs itself.)

**Measured on oldpc (ticket 0485, 2026-11-17 07:30-08:03 EST) [L]; numbers filled in by the MC from
`server/evidence/0485/scripts/analyze.py` on the fetched job.**
Quiet headed testbed job `20261117-073035-7a91` on `oldpc` (`tb1`, the machine's own Steam DX11 EXE, the owner's logged-on
session, offscreen mode), GPU NVIDIA GeForce RTX 2080 driver 32.0.16.1088 (the job's `info.gpu`; D175),
mods cp, vp, eui, save `TB-TINY_0008` (= REL548-TINY_0008, `b2ceb663...`, shipped with the job), 0477's scene (Quick
Combat and Quick Movement off, read back; 0456's `setup.lua` and `ai_setup.lua` AI=1 NB=2 AIRTYPE=UNIT_BOMBER KIND=sub
NT=3: two AI Heavy Bombers against three human submarines), one AI turn per launch, each run from a fresh launch and
load. Arms: **A** the stock pin `i0ship-26234371-ia` (`3C2F47DA...`), **B** the fix (cb85112f1, `1F7AFEA0...`). Order: a
warm-up of A (not counted), then A,B,B,A,A,B,B,A,A,B,B,A (6 per arm). AI turn = `ActivePlayerTurnEnd` ->
`ActivePlayerTurnStart`, strike = `RunCombatSim` -> `EndCombatSim` (the logger's `os.clock` stamps). Job:
`server/evidence/0485/scripts/aiturn_ab.py`; results `server/evidence/0485/logs/`.

| Arm | AI turn: median (min-max), mean +- sd, 95 % CI of mean | Strikes on submarines by the AI bombers | At the 20 s limit |
|---|---|---|---|
| A stock pin | 45.45 s (45.27-45.60), 45.45 +- 0.13, [45.31, 45.58] | 12 strikes, 20.03-20.07 s | 12 of 12 |
| B fix | 9.25 s (9.14-9.42), 9.26 +- 0.10, [9.15, 9.36] | 12 strikes, 1.91-2.00 s | 0 of 12 |

B - A: means -36.19 s (95 % CI [-36.34, -36.04], Welch, df 9.4), medians (Hodges-Lehmann) -36.19 s ([-36.33, -36.06],
bootstrap); B/A of the medians 0.204; detectable difference 0.21 s at 80 % power; the strikes alone account for 36.16 s
(median per-run sum on submarines, A - B: 40.10 - 3.94), the rest 0.03 s. Pass check of the ticket (`analyze.py`
RESULT): PASS. Machine: the job ran alone (`quiet_summary` alone true, undisturbed false, foreign_cores 0.425: another
process used under half a core; the arms are interleaved, so it cannot favour one). The workstation indication of 0477
(54.9 s -> 9.4-11.9 s, another scene layout) agrees in direction; this is the measured number.

- Game results are the same as on stock: in the same scene the first strikes took 8 and 5 HP off the bombers and 31 and
  39 off the submarines, with the stock and the fixed DLL (`RunCombatSim` final damages, `SerialEventUnitSetDamage`).
  With the fix the events carry the attacker's per-combat damage as 0 (13.6).
- Picture series `v2burst` (Visual Checks, issue 0477, DIAGNOSTIC): at +0.1 s a bomber over the capital; at +2.8 s the
  submarine at 49,11 under a bomb splash, the bombers back over the city, the combat text "Your Heavy Bomber bombarded
  an enemy Submarine! (31 damage)"; the strike had ended at +1.9 s. The submarine's figure is visible, unlike in 0456's
  frames of the stuck strike.
- Lua that reads the attacker's per-combat damage (MC #3662; grep of every `*.lua` of the install and the MODS folder
  with VP 5.4.8, cp, vp and the VP-EUI files): the unit flag managers and city banner managers only hide and show flags
  and garrison rings; the EUI unit panel's listener that prints the damages is commented out (`--[[ ]]`); only the
  tutorial's `TutorialChecks.lua` compares the two damages for its advisor hints, and it runs only in the tutorial. No
  combat log, notification or statistic uses the value.
