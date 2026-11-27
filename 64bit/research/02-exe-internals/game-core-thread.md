# The game-core thread: what lets it call `ICvGame1::Update`, and the update hold

Issue 0335, 2026-11-15, implementer impl-B-0335 (account B), branch `task-0335-stall-dump-read`. Read from the three
dumps of issue 0317's AI-turn stall (`research/05-graphics-composition/unit-state-ii.md` 5 and 9) with cdb 10.0.26100
(Windows SDK, official) and from the EXE file of the Goldberg instance c5h-e (SHA-256 `04cfdc71...8814c3`,
TimeDateStamp 0x546CD0A8; its `.text` is byte-identical to the Steam EXE's, `addresses.md` conventions). No live game.
Marks: `[C]` read in the disassembly of the dump or of the file, `[L]` seen in a run's logs or counters, `[I]`
inferred. Addresses are VAs at ImageBase 0x400000. This file describes behaviour only; no disassembly or decompiler
output is committed (D83). The rule was recorded nowhere before (increment 3 sheet, item N12).

## 0. In short

1. **The game-core thread runs one `ICvGame1::Update` per request and then suspends itself** `[C]`. Its loop
   (thread procedure 0x82A360) checks a "suspend requested" byte under a critical section and, when it is set, calls
   `SuspendThread` on its own handle. The step it runs after each wake-up (0x4D8BF0) calls `Update` once, then sets
   the "suspend requested" byte again. So between two updates the thread sits in `SuspendThread`, which is where both
   stall dumps show it.
2. **The main thread wakes it** `[C]`. The main thread's frame function (around 0x7258E5) calls the update request
   0x53AF10 when its frame-time accumulator is due. In the threaded mode (the one in use: the thread object exists),
   the request returns at once while an update is still pending (counter `+0x40` > 0); otherwise it sets the pending
   counter to 1 and resumes the thread (0x82A320: clears the byte and calls `ResumeThread`). When the request finds
   no thread object yet it creates the object and starts the thread.
3. **The update hold** `[C]`. A global counter at **0x2DE044C** stops every `Update`: the thread's step (0x4D8BF0),
   the synchronous path of the request (0x53B021, used when threading is off or fewer than two worker threads are
   reported) and a third `Update` call site at 0x6C8DD0 (not traced; the job mode's job is the likely one `[I]`)
   test it through 0x74E510 and skip the call while it is non-zero; the frame function (0x72552D) and the hold's own
   decrement (0x5975A9) are the other two callers of the test. One function
   changes it: 0x769FB0 (`InterlockedIncrement` for true, `InterlockedDecrement` for false); a reset (0x51AFD0)
   sets it to 0. No other `.text` instruction names the address.
4. **Who holds and who releases** `[C]`. The only increment is in the EXE's handler of
   `ICvEngineUtility1::GameplayDiplomacyAILeaderMessage` (engine utility vftable 0x9EA718 slot 184 -> thunk 0x605CB0
   -> 0x51B1E0), on the branch that shows the leader screen: an AI leader's message to the human **holds the game
   update until the leader screen is left**. The only decrement is in a method of the leader-head scene class
   (vftable 0xA1DAE0 slot 2, 0x597590; the class's strings are "Leaderheads are disabled!" and "THERE IS NO SCENE
   LOADED FOR THIS LEADER!!"), which drops the hold only if it is set. Lua's `UI.RequestLeaveLeader` (the call EUI's
   `LeaderHeadRoot` `OnClose()` makes, `run-and-drive-the-game.md` 5) goes to a method (0x5DB0E0) of the same
   diplomacy manager that took the message `[C]`; that this path reaches the decrement is `[I]`.
5. So in stock the rule is: **the game-core thread calls `Update` whenever the main thread's frame asks for it and
   no leader screen holds the game** `[C]`. An AI turn with a message to the human pauses at that message until the
   human (or the harness's diplomacy answerer) closes the screen. A hold that nothing releases is a permanent stop of
   `Update`, with the frame loop, the UI's per-frame DLL reads and the renderer still running, which is what 0317 saw.

## 1. The thread object [C]

One object of 0x44 bytes, pointer at **0x2DD202C** (created and started by the request 0x53AF10 on first use,
stopped and freed by 0x474120 at game exit). The fields used here:

| Offset | Meaning |
|---|---|
| +0x00 | vftable 0xA55BA0 (slot 1 = the step 0x4D8BF0) |
| +0x04 | started |
| +0x07 | stop requested |
| +0x08 | running (the loop runs while set) |
| +0x0C | critical section guarding +0x34 |
| +0x30 | thread id |
| +0x34 | suspend requested: set by 0x82A250 (the step calls it at its end), cleared by 0x82A320 (with `ResumeThread`) |
| +0x38 | thread handle (the one the loop suspends) |
| +0x3C | semaphore handle: the step waits on it (infinite) before `Update` and releases it after |
| +0x40 | update pending: set to 1 by the request, decremented by the step after its `Update` (or after skipping it) |

The thread procedure 0x82A360 runs `while (running) { lock; s = suspend requested; unlock; if (s) SuspendThread(self);
if (!stop requested) step(); }`. The step: wait on the semaphore; take the game (`ICvGameContext1::GetGame`, the first
call the thread makes, `load-sequence.md` 6); if the hold counter is 0, call `ICvGame1::Update` (vtable +0x120, the
call whose return address 0x4D8C4A appears in 9036's dump, `proxy-design.md` 18.1); decrement +0x40; release the
game; request the suspend; release the semaphore.

The request 0x53AF10 has three modes, chosen by two bytes of its object ([[0xCC8E74]+0x1460]+0x398/+0x399) and a
worker-thread count (0x81D870 returns 0x6196D54, the count the Granny pose pool is sized by, `addresses.md`; synchronous when it is below 2): synchronous on the main thread (the hold test, then `Update` directly), a job on the job
set at 0x2DD2030 (counter 0xC936EC), and the thread above. Which mode a run uses was not read from its object;
the thread mode is the one in use in 0317's runs, since its object exists and its thread is the one in the dumps
`[C]`.

## 2. The hold in detail [C]

- **Increment** (0x51B1E0, `GameplayDiplomacyAILeaderMessage(player, diploUIState, text, animation, data)`, 5 stack
  arguments): it returns at once on two game-state checks (virtual calls on the game; not identified); formats the
  text into a 1 KB buffer; when no leader is current (`[manager+0x1E4] == -1`) it makes the caller's player current;
  then, if a virtual query on a UI object returns false, it marks an entry of the popup state table at 0x2A10E08
  (state 3, flag 2) and **increments the hold**; otherwise it marks an entry at 0x25FF080 and does not hold. A
  message for a player that is not the current leader is queued (list at `[manager+0x1E8]`). The manager is
  `[[0xCC8E74]+0xD0]` (the thunk 0x605CB0 loads it).
- **Decrement** (0x597590, leader-head scene class slot 2): if the hold is set, decrement it; then reset the leader
  scene and call a method of a member at +0x5A4.
- **Reset** (0x51AFD0): zeroes the hold and the fields at 0x2DE0440-0x2DE0448 and re-initialises 64 entries of 0x18
  bytes at 0x2DDFE40 (a per-player-slot table, indexed by slot 0-63 and stepped by the per-frame slot poll 0x6A1DC0;
  corrected 2026-11-15 by epic 0334 / 4257, was "a per-leader table"; `research/03-dll-engine-boundary/player-limits.md`
  3.1 lists its 14 other referencing functions) `[C]`.
- **The leader-head class's per-frame method** (slot 3, 0x6A36A0) shows "Leaderheads are disabled!" when bit 9
  (LeaderHead, 0x200) of the mask it keeps at +0x718 is clear, and "THERE IS NO SCENE LOADED FOR THIS LEADER!!" when
  the leader-scene pointer **0xDA8F8C** is 0; bit 12 (Particle) gates a further part of it.

## 3. Addresses

| VA | What | Mark |
|---|---|---|
| 0x82A360 | game-core thread procedure (the Win32 start address `CivilizationV_DX11.exe+0x42A360`) | C |
| 0x82A250 | thread: request suspend (sets +0x34) | C |
| 0x82A320 | thread: resume (clears +0x34, `ResumeThread`) | C |
| 0x82A1F0 | thread: exit path (called at the loop's end) | C |
| 0x4D8BF0 | the step: semaphore, `Update` unless held, pending--, request suspend | C |
| 0x53AF10 | update request from the main thread's frame (three modes) | C |
| 0x53B021 | its synchronous path (`Update` on the caller's thread unless held) | C |
| 0x7258E5 | the frame function's call of the request (behind a frame-time accumulator) | C |
| 0x474120 | stop and free the thread object (callers 0x449E5B, 0x47B842) | C |
| 0x2DD202C | pointer to the thread object | C |
| 0x2DE044C | the update hold counter | C |
| 0x74E510 | "is the update held" (callers 0x4D8C36, 0x53B026, 0x5975A9, 0x6C8DD0, 0x72552D) | C |
| 0x769FB0 | hold(bool): increment or decrement | C |
| 0x51B1E0 | `GameplayDiplomacyAILeaderMessage` handler (via thunk 0x605CB0, engine utility slot 184) | C |
| 0x597590 | leader-head class slot 2: drops the hold | C |
| 0x6A36A0 | leader-head class slot 3: per-frame, the two debug texts | C |
| 0xA1DAE0 | leader-head scene class vftable | C |
| 0xDA8F8C | current leader scene (0 = none loaded) | C |
| 0x5DB290 | `UI.RequestLeaveLeader` C function (calls manager method 0x5DB0E0) | C |

## 4. Corrections to other files

- **The game-core thread's start address is RVA 0x42A360, VA 0x82A360** (2026-11-15). `load-sequence.md` 6 and
  `proxy-design.md` 15.3 write it as `CivilizationV_DX11.exe+0x42A360`, which is right (module-relative, like the
  main thread's `+0x527035`); `disable-systems-and-masks.md` ("Why the models are dark red in D33") and the increment 3 sheet (4.2) write `0x42A360` as
  if it were a VA. In the dump the EXE is at 0xFF0000 and the thread's start is 0x0141A360.
- `proxy-design.md` 18.1 puts the `Update` call from 0x4D8C4A on the "main thread": 0x4D8C4A is the return address
  inside the game-core thread's step (0x4D8BF0), so that stack was the game-core thread's `[C]`.

## 5. Not verified

- Which UI query decides between the holding and the non-holding branch of 0x51B1E0, and what the two early checks
  are.
- That `UI.RequestLeaveLeader` (or `Events.LeavingLeaderViewMode`) is what reaches the decrement 0x597590; and
  where the EXE raises the Lua event `AILeaderMessage` (its two name strings, 0xA377E0 and 0xA58190, are only
  referenced from registration tables).
- ~~Whether the step thread keeps being resumed (and skips `Update`) each frame while the hold is set~~ answered by
  4656 (2026-11-16, section 6): it is resumed once per update tick, not per frame, and skips `Update` [C].
- The job mode and the synchronous mode were not seen in use.

## 6. What paces the update requests (tickets 4656 and 4661, D165)

4656 (impl-4656, 2026-11-16 23:25-23:55 EST, workstation, static reading only) read the Steam EXE `CivilizationV_DX11.exe`
(SHA-256 `b2ce96ae...0c88e`, TimeDateStamp 0x546CD0A8, **ImageBase 0x400000**; every address here is a VA at that
base) with cdb 10.0.26100, one 0317 stall dump for a heap object's vtable, and the DLL fork at 04b1211b. 4661
(impl-4661, 2026-11-17 00:18-00:56 EST) checked it live with a read-only poller (`perf/turnstudy/pacing/poller.py`,
ReadProcessMemory about every 1 ms, no write, no hook) on the workstation: c5h-a, window cloaked, S2i on the M save
(TS0438-M-T150) turns 151-153, statistics logging on, shared machine, one run, grade C. Behaviour only, nothing from the
EXE (D83). The working notes are `perf/evidence/0437/4656/update-pacing.md` (static) and
`../08-performance/turn-time-study.md` "The pacing poll" (live); what the pacing costs a turn and the amelioration
options are in that file's section "What paces an AI turn in the client (D165)".

### 6.1 The tick [C, L]

- `GameViewState::Update` 0x7253E0 (vtable slot 3, called from the app frame at 0x4B4087) adds the frame's `dt` to the
  float **0xCCF91C** (`.data`) each frame. When it exceeds the threshold, the return of `ICvEngineUtility1::getSecsPerTurn`
  (slot 102 of the engine utility object `[[0xCC8E74]+0x1460]`, vftable 0x9EA718; function 0x704B60, which returns the
  `.rdata` float 0x9E402C = **0.1**; the frame's call returns to 0x7258C1 [C, L by 4662's caller tally]), the frame calls the update request 0x53AF10 (at 0x7258E5) unless a skip
  condition (6.2) holds, and sets the accumulator to **0** in both cases [C]. So a request can come only on a tick,
  every N frames with N the first n where float32(n x dt) > 0.1: 7 frames at 60 fps (0.117 s), 3 at 30 fps, 13 at 120
  fps; at most about 8.6 requests a second at 60 fps; a skipped tick is lost [C].
- 0x9E402C is a compiler-pooled constant named by 50 `.text` instructions and lies in a read-only section: it is not
  a place to change the interval [C].
- Live: every request came within 3 ms of an accumulator reset, 75 of 75; 3.7 ticks per request period on average,
  2.7 of them lost; frame rate 43.8 fps [L].
- No accumulation at all that frame when `GameViewState+0x1C0` is clear, the byte 0x2DD2F5E is non-zero,
  `ICvGameAsynch1::IsFinalInitialized` is false or `ICvGameContext1::IsGraphicsInitialized` is false (load and exit
  states [I]) [C].

### 6.2 The skips [C, L]

1. **An update is pending** (`ICvEngineUtility1::IsGameCoreExecuting`, slot 233, 0x603580: the thread object's `+0x40`
   > 0, section 1): every tick while an `Update` runs is lost [C].
2. **The first turn-active player changed and queue B is not empty** (AI turns only): the frame scans player slots
   0-63 for the first alive, turn-active player; when that differs from the player kept in the heap field
   **`GameViewState+0x8CC`** (GVS = `[[0xCC8E74]+0xC0]`, vtable 0xA20908) and queue B (`0x2A10E08 + 0x4C x [0x2A10200]`,
   test 0x4FB9F0) holds records, the request is skipped and +0x8CC kept; otherwise +0x8CC takes the new player [C].
   Live: queue B emptied 0-5 ms (median about 1 ms) before the frame accepted the next player in all 57 periods
   where the poll saw it empty [L].
3. **The AI-turn flag turns true while queue B is not empty**: a per-frame flag (true unless the active player is a
   human whose turn is active, or the game is paused or over, or the hold 0x2DE044C is set) is compared with the byte
   **0x2DE0447** (set through 0x50A170); the flag is also the request's argument [C]. Live: 0x2DE0447 is 1 from the
   frame's acceptance of the next player to the end of that player's `Update`, 0 from there to the next acceptance,
   and 0 in the human's turn [L]. *Refined 2026-11-17 by 4665 (`perf/evidence/0437/4665/notes.md`):* skip 3 does not
   wait on its own. In stock the byte drops in the first frame after an AI `Update` because skip 2 fires there and
   clears the per-frame flag; the byte's 1 -> 0 transition (0x50A170: queue B's flag 0x2A10F00 released, a 0x60-byte
   record posted to queue B) follows from that, and the 0 -> 1 transition at the acceptance (the flag taken, the
   counter 0x2DE0448 +1, InterfaceBuddy 0x401C80(0)). While an `Update` is pending the frame neither scans nor
   transitions (its flag := the byte) [C]. In 4662's samples the byte dropped a median 8 ms after the `Update`'s end
   in 299 of 300 cases [L]. So when +0x8CC already holds the next player, neither skip fires and the byte stays 1
   through the AI turn (D165 option 2, `../08-performance/turn-time-study.md` "Option 2 built").
4. **Once per game turn** a start-of-turn step (the turn kept at 0x2DE0440 against the word at 0xC98DE4) may send a net
   message through `[0x2DE0454]` and skip that tick [C path, I meaning]. Live after a load at turn 150: 0x2DE0440 was
   -1, then 150 from that turn's change; 0xC98DE4 was 150, then -1, so neither is a plain game turn [L].
5. **The hold** (section 2): with 0x2DE044C set the frame still sends the request once per tick (argument 0) when no
   update is pending and the step skips `Update`; this answers section 5's open question [C].

### 6.3 Queue B waits for the `Update`, so an AI turn is serial per player [C, L]

- The app frame pumps queue B (0x785AF0 at 0x4B4029) **only while no `Update` is pending** (the test at 0x4B3FF4);
  queues A and C and the 4 MB queue are pumped every frame [C]. Records the DLL posts during an `Update` (moves,
  combat, damage, deaths) start to be dispatched only after it ends.
- The DLL plays one AI player per `Update` (`CvGame::update` -> `updateMoves` takes only the first active AI player;
  AI mission timers are 0) [C, DLL source], so an M turn is 25 `Update` calls (8 civs, 16 city-states, barbarians) [L].
- Per AI player: **P = U + D_B + T_tick** in series: U the `Update`, D_B the drain of that player's queue-B records
  (only after U), T_tick the wait for the next tick [C]. Live per M turn (mean of 3, 12.39 s): U 5.28 s (43 %),
  D_B 5.92 s (48 %), T_tick 1.20 s (10 %) [L, grade C, one run on a loaded workstation].
- *Added 2026-11-17 by 4672:* D_B is frames, not handler work: the pump's dispatch loop is 0.56-1.2 s of 4.1-6.9 s per M
  turn; the move rule lets one keyed record per unit through per frame and holds every flag-bit-2 record behind a
  blocked one, and `GameplayUnitEmbark`'s six records (flags 0x5) serialise the queue. Layout of a half and a record,
  the pump's steps and the record types: `../08-performance/turn-time-study.md` "What the drain is made of" [C, L].
- *Added 2026-11-17 by 4675:* the move applies that rule only while queue B's ordering byte `q+0x30` is 1; with 0 every
  record of the write half moves in one pass, in posting order. The byte is written only by LoadGameState::Update
  0x403170 (0 at the load's start, 1 at a later state), never by the pump, the move or the frame [C]. D165 option 4
  clears it for the AI turn's end-of-phase drain (`../08-performance/turn-time-study.md` "Option 4 built").

- *Added 2026-11-19 by 0571:* the pump test is not the only gate: queue B's write half never shrinks while the AI-turn byte
  0x2DE0447 is 1, even with no `Update` pending [L, oldpc, pin batch 5, about 1 ms poller]; so holding the next AI request
  cannot drain it. The working form (D165 option 2 value 2, off by default) leaves +0x8CC unwritten at a gap with more than
  100 undispatched records and clears the ordering byte (option 4): `../08-performance/turn-time-study.md` "Option 2 value 2".

### 6.4 What a fix may and may not touch (D165, D83, CEG)

The tick constant 0x9E402C (`.rdata`) and the skip tests (instruction bytes) are out of bounds. The heap field
`GameViewState+0x8CC` (option 2) and a copy of the heap object's vtable 0x9EA718 with a different slot 102 (option 3)
are heap data and a heap vtable pointer, the means D165 allows; neither is implemented (design:
`../08-performance/turn-time-study.md`, D165 section). *(Corrected 2026-11-17 by 4675: options 3, 2 and 4 are
implemented, tickets 4662, 4665, 4675; option 4 writes queue B's ordering byte `q+0x30`, plain `.data`.)* The "several AI players per request" option deadlocks the
windowed client (4660, not merged): UI Lua on the main thread needs the gaps between `Update` calls.

## Sources

Dumps (not committed, D83, D141): `F:/c5-64bit-work/0317/dumps/unitnomp-stall/c5h-e-x86-43908-20261115-011444.dmp`,
`.../unitnomp-stall-rerun/c5h-c-x86-19056-20261115-014359.dmp`, `.../unitnomp-stall/c5h-e-x64-48724-20261115-011517.dmp`.
Commands: `cdb -z <dump> -y <pdb folders> -cf <script>` (x86 cdb for the EXE dumps, x64 for the server's), scripts
using `~* k`, `!handle`, `dd`, `dds`, `u` and `s -d`; small Python scanners over the EXE file for `call`/`jmp` targets,
absolute-address operands and string references. The scripts lived in `C:/c5-64bit-work/0335/` (scratch, removed at
the hand-back). Engine utility slot numbers: `proxy/model/policy-table.tsv` (`slot_x86`), vftable 0x9EA718
(`proxy-design.md` 17.4).
