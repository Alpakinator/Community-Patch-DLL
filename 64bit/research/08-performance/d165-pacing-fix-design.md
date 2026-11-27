# D165: design of the client pacing fix, options 2 and 3 (epic 0437)

lead-0437 (third lead), 2026-11-17 02:00-02:40 EST. A design for the MC's decision; **nothing here is implemented** (*2026-11-17 04:25, impl-4662: option 3 is now implemented, ticket 4662; `turn-time-study.md` "Option 3 built"*;
*2026-11-17 09:20, impl-4665: option 2 too, ticket 4665; "Option 2 built"*),
and no proxy change starts before the MC answers (MC #3462). Built on `../02-exe-internals/game-core-thread.md`
section 6 (what paces the update requests: 4656 [C], 4661 [L]) and `turn-time-study.md` "What paces an AI turn in the
client (D165)". Addresses are VAs of the Steam DX11 EXE (SHA-256 `b2ce96ae...`, TimeDateStamp 0x546CD0A8) at ImageBase
0x400000; the proxy rebases each by the EXE's module base. Marks: `[C]` read in the EXE or our source, `[L]` seen live,
`[I]` inferred.

## 1. What each option removes

Per AI player the windowed client spends **P = U + D_B + T_tick** in series (4661, workstation, S2i on M, one run,
grade C: U 5.28 s, D_B 5.92 s, T_tick 1.20 s of a 12.39 s turn) [L].

| | Option 2: mark the player change as seen | Option 3: a shorter tick in AI turns |
|---|---|---|
| Removes | D_B from the critical path (the wait for queue B to drain after each AI `Update`) | most of T_tick (from 0-0.1 s to at most one frame) |
| Means (D165) | plain heap data: the field `GameViewState+0x8CC` | a vtable pointer on a heap object: the engine utility object's vptr, pointed at a copy of vftable 0x9EA718 |
| EXE bytes | none (`.text`, `.rdata` untouched) | none; the pooled 0.1 at 0x9E402C (`.rdata`) is not written |
| Upper bound on M (workstation numbers) [I] | about 5.9 s of 12.4 s, less the drain work the main thread still does in the gaps | about 1.0 s of 12.4 s |
| What the player sees | the AI's moves play further behind the game; the backlog is shown after the AI turn ends (*corrected 2026-11-17 by impl-4665 [L]: at the end of the AI phase, before the turn change, under the "Processing Turn For Barbarians" banner; nothing is left at the human turn's start*) | nothing different in AI turns but speed; human turns unchanged |

The two combine (3 then shrinks the gaps that 2 opens). Option 3 alone is the safer first step: it changes only when a
request is made, never what gates it.

## 2. Option 2 in detail

**What it writes.** After each AI player's `ICvGame1::Update` returns to the proxy, and before the EXE's step ends,
the proxy computes what the frame's gate computes, the first player in slots 0-63 with `IsEverAlive` and
`IsTurnActive` (ICvPlayer1 slots 21 and 22), and stores it in `[[[base+0x8C8E74]]+0xC0]+0x8CC` (g_Civ5App -> GVS ->
the accepted player) with one aligned 4-byte interlocked write. The frame's gate then finds no change and does not wait
for queue B; the next request comes on the next tick after the step ends.

**Why the race is harmless [I].** The frame writes the same field with the same value (its own scan of the same server
state), and an aligned 4-byte store is atomic on x86. The proxy's scan runs on the game-core thread after the
server's `Update` has returned, so it reads the state the frame will read.

**Only in AI turns.** The proxy writes only when the AI-turn byte `[base+0x29E0447]` (VA 0x2DE0447) is 1 at the
`Update`'s start, the hold `[0x2DE044C]` is 0 and the scanned player is not the active human (`ICvGame1::GetActivePlayer`).
*Added 2026-11-23 (ticket 0718, D246): and only while the DLL's Quick Combat and Quick Movement are both on, read after
each AI `Update` that reaches the decision. With animations on, the deferred records of several AI players would be
dispatched together, so they would play at the same time. Then the stock wait runs (`turn-time-study.md` "Options 2
and 4 follow Quick Combat and Quick Movement").*
In the human's turn nothing is written. *Added 2026-11-17 by impl-4665 (ticket 4665, built; `turn-time-study.md`
"Option 2 built"): the third skip (the AI-turn byte against the frame's flag) needs no write of its own, because in
stock it fires only after skip 2 has cleared the flag [C, L]; with +0x8CC written the byte stays 1 through the whole AI
turn instead of dropping and rising once per player, so the frame's per-player byte transitions (a 0x60-byte record into
queue B, queue B's flag 0x2A10F00, the counter 0x2DE0448, InterfaceBuddy 0x401C80) happen once per AI turn [C]. When
no player is turn-active (the turn's end) nothing is written, so the backlog drains before the turn change.*

**What it costs and moves.** The scan is 64 x 2 read-only calls, both answered by the proxy's own players mirror
(`proxy/model/policy-table.tsv`: `ICvPlayer1::IsEverAlive` and `IsTurnActive`, policy `mirror:players` [C]), so no round
trip to the server. *Corrected 2026-11-17 by impl-4665: the mirror is the epoch mirror, and right after an `Update`
(an impure call) its values are stale, so the scan's getters do cross to the server once (they then fill the mirror
for the frame's own scan); measured on the workstation about 68 DLL calls and 0.15 ms per scan, at most 1.2 ms [L].* Queue B is still pumped only while no `Update` is
pending (0x4B3FF4 [C]), so its drain is not removed but deferred into the gaps: queue B's buffers are growable vectors
(`../02-exe-internals/message-queue-and-fixed-buffers.md`), so the backlog costs memory and lag, not an overflow [I].
A timing claim must report the backlog at the human turn's start (queue B bytes, time until empty, with 4661's poller)
so that a deferral is not sold as a saving (PERFORMANCE.md: explain why the numbers moved).

**The 4660 lesson.** 4660 deadlocked because the server ran several `Update`s in one request and UI Lua on the main
thread needs the server's lock in the gaps. Option 2 keeps one `Update` per request and the main thread both runs UI
Lua and makes the requests, in that order within its frame, so a request is never made while a UI Lua call of the main
thread is in flight [C for the frame's order, I for "no UI Lua on another thread"; check with `proxy-lua-calls.tsv`].
The gaps get shorter, not removed: UI Lua latency in AI turns is measured as part of the acceptance (the proxy's
per-call timers).

## 3. Option 3 in detail

**What it swaps.** The engine utility object (`[[base+0x8C8E74]+0x1460]`, the same pointer the EXE gives the DLL and the
proxy keeps as `g_engineUtility`, `proxy_rt.cpp` SetEngineUtility) has vptr 0x9EA718 [C]. The proxy builds a copy of
that vftable (its RTTI entry at index -1 and its 264 entries: 260 interface slots to 0x9EAB24 plus the 4 that follow,
`../02-exe-internals/addresses.md` row 0x9EA718), each entry checked to lie in the EXE's `.text`, replaces slot 102
(`getSecsPerTurn`, original 0x704B60) with the proxy's function, and writes the copy's address into the object's vptr
(heap). Every other slot stays the EXE's own function.

**The replacement.** A `__thiscall`-compatible function returning a float: when its return address is the frame's call
site (`base+0x3258C1`, VA 0x7258C1: *corrected 2026-11-17 by impl-4662; this line said 0x7258BF, which is the
address of the frame's 2-byte `call edx`, not its return address [C, L]*), the caller is the main thread and the AI-turn byte 0x2DE0447 is 1, it returns the
option's tick (default for the option on: 0, a request on the first frame the gates allow); otherwise it calls the
original 0x704B60 through the saved entry (so the DLL's surrender-mission timer and every other caller keep 0.1).

**Why not human turns.** Human units' mission timers count `Update` calls (`CvUnitMission::CalculateMissionTimer`), so
a faster tick in the human's turn would speed up their moves on screen and change how many `Update`s a human move takes.

**Lifetime.** The proxy can boot more than once in one EXE process (issue 3308) and is unloaded with FreeLibrary: the
original vptr is written back at `DetachOnUnload` and before each re-install; the copy is checked again (the vptr still
ours) at each `Update`. If anything else replaced the vptr, the proxy stops the option and reports (section 5).

## 4. Proxy paths (each asked of the MC; small, epic 2600 also edits `proxy_rt.cpp`)

| Path | Change |
|---|---|
| `proxy/src/proxy_pacing.cpp`, `proxy/src/proxy_pacing.h` (new) | the EXE checks, option 2's scan and write, option 3's vtable copy, swap, replacement and restore, counters, the report lines |
| `proxy/src/proxy_update.cpp` | `BeforeUpdate`: the per-`Update` re-check (GVS and vptr) and the AI-turn sample; a new `AfterUpdate` calling option 2 |
| `proxy/src/proxy_rt.cpp` | `End()`: for the outermost `ICvGame1::Update` (role `kRoleUpdate`, as `Begin` does), call `ProxyRtInt::AfterUpdate()` (about 4 lines); `DetachOnUnload`: the restore (1 line) |
| `proxy/src/proxy_rt_internal.h` | `AfterUpdate` declaration |
| `options/include/vp64_options.h`, `options/src/vp64_options.cpp`, `options/tests/test_options.cpp` | two keys in a new section `[Performance]`: `AiSkipVisualDrain` 0/1 (default 0) and `AiUpdateTickMs` -1 = stock (default) or 0..100 (default 0 since ticket 0566, 2026-11-19, D191) |
| the proxy's build (`proxy/README.md`; no build file lists `proxy_update.cpp` by name, so the sources appear to be taken from `proxy/src` as a whole [I]) | the new source file, if the build lists files |
| `proxy/tools/rt_tests/` (new `pacing_unit.cpp`) | offline tests on a fake EXE image: checks refuse a wrong stamp, a wrong vptr, an entry outside `.text`; the swap and restore; off = no write, no swap |

No server, DLL, shell or harness change. The headless hosts (S1, S3) have no EXE and are unaffected.

## 5. Fails loud (D118, D125)

- **At boot, an option on and a check fails** (TimeDateStamp 0x546CD0A8, `SizeOfImage`, g_Civ5App readable, GVS vptr
  0xA20908, the engine utility pointer equal to `g_engineUtility` with vptr 0x9EA718, slot 102 = 0x704B60, every copied
  entry in `.text`): the proxy writes the D118 report naming the option and the failed check and raises the helper's
  popup, through the existing `ReportFailure` path, and does not start the game with the option silently off.
- **During play, a check fails** (GVS replaced by an object of another class, the vptr no longer ours, an access fault
  in the write, reported by the proxy's guarded read): the proxy restores the original vptr and stops writing, logs one
  `pacing:` line in vpproxy.log, and raises the D118 report. Proposed: the game then continues with stock pacing, which
  is the identical game (not a "32-bit game with a broken world", D118's concern); the MC decides whether this case is
  fatal instead.
- `server-report.txt`/vpproxy.log carry counters: writes made, ticks shortened, checks run, the option's state.

## 6. Off by default

**Corrected 2026-11-19 (ticket 0566, D191): option 3 is on by default.** `AiUpdateTickMs` defaults to 0 in the options
table (`options/src/vp64_options.cpp`) since branch `task-0566-ai-tick-default`; an explicit `AiUpdateTickMs = -1` still
gives stock pacing (nothing installed, one `pacing: off` line). `AiSkipVisualDrain` and `AiDrainUnordered` stay 0. With
the default on, option 3's boot checks run for every player without the key, so on an EXE other than Steam build
4390913 (Firaxis' 26577625 included, D200) a failed check writes the D118 report and raises the popup: wanted (MC #7259,
D118/D125; ticket 0588). Upgrade behaviour and the live smoke: `turn-time-study.md`, "Option 3 on by default (ticket
0566)". Replaced text: "Both keys default to stock (0 and -1)."

Before 0566: both keys defaulted to stock (0 and -1). With both off the proxy installs nothing: no read of the EXE's
fields beyond what it reads today, no write, no swap (the offline test asserts it; the parity pair below covers the
product). A default-on change goes to the MC and the owner (MC #3325 (a)); the owner made option 3's in D191. Increment 4's `[Extras]` is for visual additions and bugfixes
(D169); this is neither, hence `[Performance]` (the MC may rule otherwise).

## 7. Game identity (D164) and acceptance

- **Identity pair, windowed against windowed**: the product path (S4) on the M save (TS0438-M-T150) by hash, option
  on against off, the same client, statscmp over at least 10 AI turns with `--expect-turns`; option 3 alone, option 2
  alone, both. Any difference stops the work and goes to the MC before any timing (MC #3325 (b)).
- **Situations**: a turn with an AI leader message (the hold 0x2DE044C), a human turn with a unit move (option 3 must
  not act; mission timers unchanged), a load and a second load in one process (re-install), an exit to the menu (restore).
- **Timing**: on the laptop (quiet, driver named), S4 option off/on alternated (the runner's Williams order), with
  4661's poller in a separate instrument run (never in a timed series): U, D_B, T_tick per turn, queue B's backlog at
  the human turn's start and the time to drain it, UI Lua call latency in AI turns. Graded per PERFORMANCE.md.
- **Where it runs**: product-path runs go to the laptop or oldpc; on the workstation only once ticket 0473 (the focus
  guard covers vp64helper's popup) has merged.

## 8. Open questions for the MC

1. Grant the paths of section 4 (option 3 first, then option 2), or name other ones.
2. A mid-play check failure: report and continue on stock pacing (proposed), or fatal.
3. The section and keys `[Performance] AiUpdateTickMs`, `AiSkipVisualDrain`.

## 9. Not verified

- How fast the 64-slot scan from the players mirror is, and that the mirror is current right after `Update`.
- That no UI Lua runs on a thread other than the main thread during AI turns.
- The vftable's exact length (264 entries plus RTTI) on a live object; the copy's checks guard it. *Corrected
  2026-11-17 by impl-4662: in the EXE file the 264 entries are confirmed (slots 0-263 in `.text`, the entry after them
  not a pointer) and the "RTTI entry" at index -1 is 0 (no locator) [C]; on the live object see `turn-time-study.md`
  "Option 3 built".*
- Whether queue B's deferred backlog under option 2 delays the human's first input (it is pumped every frame once no
  `Update` is pending). *Answered 2026-11-17 by impl-4665 [L, workstation, one run each]: no; the backlog (about 475 KB
  on M) drains at the end of the AI phase, where nothing is written and the frame waits as in stock (about 5.6 s per
  turn), and queue B holds the same about 80 KB after the turn-change `Update` as in stock.*
- *Answered 2026-11-17 by impl-4665: the scan's speed (above, at most 1.2 ms) and that the epoch mirror is NOT current
  right after an `Update` (the getters cross).*
- Every gain figure: from one grade-C workstation run.

## 10. After options 2 and 3: what the drain is made of, and the next remedies (ticket 4672, 2026-11-17)

impl-4672, 2026-11-17 11:20-11:40 EST. Numbers and method: `turn-time-study.md` "What the drain is made of: queue B
by record type". Every gain below is a **model figure** (`perf/turnstudy/pacing/qbsim.py`: queue B's move rule run on
the 2026-11-17 census records; it matches the frames seen within 10 %) times the workstation's own drain frame
(25-27 ms, loaded: 9 heavy jobs of other owners registered); an indication, not a result (D106). No remedy is built.

**What is left with options 2+3** (per M turn, workstation, one run): 8.35 s = U 3.86 + the end-of-phase drain 4.14 +
T_tick 0.36. The drain is about 180 frames: the dispatch loop 0.56 s, the move 0.15 s, the rest ordinary frames that
each let one keyed record per unit through; about 145 of those frames come from the 24 embarks a turn
(`GameplayUnitEmbark` posts six records with flags 0x5, which pass one per frame across all units).

**Ranked by expected gain against risk** [I]:

| Rank | Remedy | Where the cut is made | Gain per M turn on top of options 2+3 (stock pacing in brackets) | What the player sees | With Quick Movement off | Identity check |
|---|---|---|---|---|---|---|
| 1 | **Clear queue B's ordering byte (`q+0x30`, `.data`) for the end-of-phase drain** (no turn-active player, option 2 on), when nothing in queue B is held or has flag bit 1, the hold 0x2DE044C is 0 and Quick Combat is on; set it back before the turn-change request | proxy (`proxy_pacing.cpp`, beside options 2 and 3; a `.data` byte, D165) | up to -3.6 s (179 of 180 frames; the loop's 0.56 s and the move stay) [-5.0 s if done for every AI player's drain] | the whole backlog in one frame of about 0.7 s instead of 4 s of frames: one unit's records (an embark's model swap, its teleports) in the same frame. Order kept (the move walks in posting order) | moves are queued `GameplayUnitMoved` records whose animations may hold records: the guard (no held record) keeps the byte set then; gain shrinks | EXE-side only: statscmp windowed vs windowed (D164) must be IDENTICAL; DIAGNOSTIC pictures of an embark and of a unit with a long move in the burst |
| 2 | **No `GameplayUnitEmbark` for a unit the active team cannot see; send its current state when it becomes visible** | DLL (`CvUnit::embark/disembark`, the visibility paths; behind a define on a fork branch, D104: helps the 32-bit game too) or the proxy (defer the call; flush before a `GameplayUnitVisibility(true)` of that unit) | up to -4.1 s if every embark is unseen; -3.9 s with "AI units only" as a stand-in (the visible share is not measured) [-3.5 s] | nothing for units out of sight; a unit coming into view shows the right model | the same | DLL variant: statscmp IDENTICAL expected (a void engine call); pictures of a unit that embarked out of sight and comes into view |
| 3 | **Quick Movement: one teleport per move segment, dim-flag/visibility/fortify records last-wins per unit** | DLL (`CvUnit::move` calls `SetPosition` per plot step; queue the plot and publish at the points `PublishQueuedVisualizationMoves` already uses: embark, combat, mission end) | -1.1 s alone; with rank 2 -4.4 s together [-2.1 s; -4.9 s] (*corrected 2026-11-17 by 4677: -0.13 / -0.41 s for steps A / B with 2+3 [-0.83 / -1.36 s], about 0 with option 4; see the status below the table*) | fewer intermediate jumps (already instant under Quick Movement) | not used (moves are animated `GameplayUnitMoved` then) | statscmp IDENTICAL expected; pictures |
| 4 | Dispatch-side work (FOW changes, plot state, city dirty 0x6F) | proxy or DLL | under 0.1 s each; 0x6F never posted saves no frame | - | - | - |
| 5 | Option 1 (several AI players per request) | server | at most -0.36 s (the remaining T_tick); not the end drain; deadlocks as built (4660) | - | - | - |

*Status of rank 3, 2026-11-17 13:45 (impl-4677, ticket 4677):* measured and designed, **not built**. The MC's view
(#4342): do not build it now under D178; park steps A and A+B as a ready idea for stock pacing and the 32-bit game;
revisit it if the owner keeps option 2 off by default. Step A's code exists unbuilt on fork branch `x64-proto-0437-qmove`. Details are in
`turn-time-study.md` "Rank 3: Quick Movement coalescing in the DLL". Within what the DLL can reach (one `Update`, per
unit, a segment ended by any other engine call about the unit), the steps remove this many records per M turn:

- Step A (one teleport per segment): 283.
- Step B (A, plus dim flag, visibility and fortify last-wins): 1,150.

The modelled gain per M turn:

| Pacing | Step A | Step B |
|---|---|---|
| Stock (also the 32-bit game) | -0.83 s | -1.36 s |
| Options 2+3 | -0.13 s | -0.41 s |
| Options 2+3+4 | <= 0.09 s, probably about 0 | <= 0.12 s, probably about 0 |

With options 2+3+4 the drain is 2 pumps, and the dispatch loop's fit gives these record types 0 ms. The row's earlier
figures (-1.1 s alone, [-2.1 s]) kept one record per unit across the whole drain, which the DLL cannot reach. They are
corrected by the figures above. The proposal to lead-0437 (#4338) was to stop at the design.

*Quick Movement off, 2026-11-17 14:45 (impl-4678, ticket 4678):* measured, the remedy proposed and **not built**. The
MC's answer (#4403, 2026-11-17): not now. Rank 6 is a recorded candidate, revisited only if options 2+3 do not
become defaults. Details
are in `turn-time-study.md` "Quick Movement off: the animated moves of units player 0 cannot see".

- The DLL already animates a step only when a watching human sees its old or new plot [C]. The study's Quick
  Movement off arms animate everything because autoplay's observer sees every plot.
- With the observer following player 0, the animated moves per M turn fall from 278 to 83. The turn falls from 29.9 s
  to 12.2 s with options 2+3, and from 73.3 s to 34.0 s at stock pacing. These are single workstation runs (an
  indication); the game is IDENTICAL.
- What stays animated but unseen is mostly trade units (`CvGameTrade::StepUnit` animates every step).

| Rank | Remedy | Where | Gain per M turn (Quick Movement off, player 0's view) | What the player sees | Identity check |
|---|---|---|---|---|---|
| 6 | **A trade unit's step follows `CvUnit::move`'s rule: a teleport when no watching human sees its old or new plot (the queued visible steps published first)** | DLL (`CvTradeClasses.cpp` 2187, define `VP64_PROTO_QMOFF_HIDDEN_TRADE` on a fork branch; D104: helps the 32-bit game) | stock pacing about -5.5 s of 34.0 s (16 of 120 AI drains in 5 turns had only hidden moves, median 1.97 s against 0.28 s); options 2+3 about 0 (visible moves set the drain's end); no effect with Quick Movement on | nothing for trade units out of view; a visible trade unit walks as stock | statscmp IDENTICAL expected (visual call only); census: hidden trade 0x4D gone; pictures of a trade unit coming into view |

The remaining U (3.86 s) is the DLL's own work 2.14 s, GameEvents Lua in the EXE about 0.9 s (90,000 calls a turn),
the turn change's autosave 0.56 s and transport: outside this note's options (a GameEvents short-cut on the server is a
separate study).

*Status 2026-11-17 (impl-4675): rank 1 is built as option 4, ticket 4675, on the MC's yes with conditions (#4203: off
by default, only with option 2, a Quick Movement guard unless pictures clear it, options id 5 only; *ids corrected 2026-11-18 by lead-0437: after
main's merge into the epic branch (234c0a950) AiUpdateTickMs is 8, AiSkipVisualDrain 9, AiDrainUnordered 10, or 9-11
if epic 0504's GameEventsOneCrossing reaches main first; the INI keys do not change*); what it does, its
tests and live results: `turn-time-study.md` "Option 4 built". The open point of 4672 ("is `q+0x30` rewritten per
frame?") is answered: no, only LoadGameState writes it [C].* The proposal as it was:

**Prototype proposed (not built; the lead asks the MC, MC #4144):** rank 1 as option 4 of the proxy's pacing,
`[Performance] AiDrainUnordered` (0 = stock, the default), only with option 2 on; paths `proxy/src/proxy_pacing.{h,cpp}`,
`options/` and `proxy/tools/rt_tests/pacing_unit.cpp`; the install checks of option 2 plus the queue's address and the
byte's present value (1); fail loud as options 2 and 3. Tests: identity pair S4s on M 151-160 windowed against windowed
(D164), DIAGNOSTIC pictures of the burst, then a testbed pair on the laptop (option 4 off/on with 2+3 on, Williams order)
for the timing. If the pictures show a model glitch, rank 2 instead (a fork branch for the DLL variant).

*Status of rank 2, 2026-11-17 13:50 (impl-4676, ticket 4676; details `turn-time-study.md` "Embarks the human cannot
see"):* measured, not built. (1) In every autoplay arm the active player is an observer that sees every plot
(`CvGame::setAIAutoPlay`, `SetAllPlotsVisible` [C]; live [L]), so a cut keyed on the active team never acts there; the
row's "active team" is read as "a watching human" (`CvPlot::isVisibleToWatchingHuman`), and an arm needs the observer
to follow player 0 (`Game.SetObserverUIOverridePlayer(0)`). (2) From player 0's view 78 % of the AI phase's embark sets
are hidden (90 % of the AI units'), worth -134 of 180 frames per M turn: **-3.9 s on top of options 2+3 at the laptop's
frame** (model), close to the row's -4.1 s upper bound. (3) Those are the frames option 4 already removes: on top of
2+3+4 only their dispatch is left, about -0.03 s per turn; in stock pacing (the 32-bit game's case) -3.1 s per M turn.
(4) Proposed (#4346): the DLL variant behind `VP64_PROTO_HIDDEN_EMBARK` on `x64-proto-0437-embark`, not the proxy
defer. *Answered (MC #4348, lead #4350): yes, with arms for Quick Movement on and off.*

*Status of rank 2, 2026-11-17 15:25 (impl-4676):* **built as a prototype**, fork `x64-proto-0437-embark` 3fd5255a1
(pushed; DLL A3CB17A9, build 2), arm S4e with the variants `he_*` (the observer follows player 0). Game identity:
IDENTICAL headless (S3, both references) and windowed off/on [L]. Workstation, Quick Movement on, options 2+3 (one
loaded run per arm, an indication): 188 of 239 embark calls deferred, 0 missed reveals; the end drain's frames 179 -> 52
per M turn, as modelled, but its seconds 3.2 -> 2.2 s, not -3.8 s: the frames removed are the drain's cheap tail
frames, so the "frames x mean frame" figures of this section overstate rank 2's (and option 4's) seconds; the laptop
pairs (prepared, not submitted) decide. Pictures: a unit coming into view has the right model at the right plot; its
formation can face another way than in stock (named difference).

## 11. Option 2 hybrids: drain after each AI player, not only at the AI phase's end (ticket 0568, D191)

impl-0568 (account B), 2026-11-18 19:05-19:25 EST. The owner (D191, 2026-11-18) [reworded for privacy]: asked whether a hybrid is possible in which the buffer is drained after each
AI turn instead of at the end of all AI turns, still buffering some but not locking up the UI for
a long stretch. **A design for the MC; nothing is built.** Every number below is a **model figure** (`perf/evidence/0568/hybridsim.py`
on 4672's census run `ws-S4s-pacing_skip1_tick0`, M 151-160, the move rule of `qbsim.py`) converted to laptop seconds
with constants fitted to the laptop's D178 results. It is an indication, not a result (D106). Marks: `[C]` read in our
source or the knowledge base's EXE readings, `[L]` seen live, `[I]` inferred or modelled.

### 11.1 What the hybrid has to work with

- **[L] With options 2+3 nothing of queue B is dispatched between two AI players.** 4672's census
  (`perf/evidence/0437/4672/qb/ws-S4s-pacing_skip1_tick0-turns.tsv`) has `pumps` = `end_drain_pumps` in all 10 turns:
  all 112-211 pumps of a turn fall after the last AI `Update`. The frame pumps queue B before it calls
  `GameViewState::Update` (the pump call at 0x4B4029 and the call at 0x4B4087 in the app frame [C, 4656]; that the pump runs first is read from their order in the function [I]). So the frame after an `Update` skips the
  pump (an `Update` was pending at its start), and its `GameViewState::Update` then makes the next request at once (tick 0,
  +0x8CC already written). *Corrected 2026-11-19 by impl-0571 (ticket 0571; replaced: "Why that frame's pump test
  always sees the `Update` pending was not read. The likely reason is that the main thread waits in UI Lua for the
  server's lock until the `Update` ends [I]"):* the cause is not a pending `Update`. **Queue B's write half never
  shrinks while the AI-turn byte 0x2DE0447 is 1, even with no `Update` pending** [L, oldpc job 20261119-175726-ef97, M
  151-160, pin batch 5, 4661's poller at about 1 ms: 0 shrinks of the write half over 3,018 (options 2+3), 3,934
  (2+3+4) and 41,793 (value 2 as first built: 8 frames held at each gap) samples with no `Update` pending, the byte 1
  and records waiting; every shrink (3,465 / 74 / 73) came with the byte 0]. With option 2's +0x8CC write the byte
  stays 1 through the AI turn (4665), so nothing of queue B moves until the byte drops at the phase's end. Which of the
  byte's effects gates the pump or its move (the byte itself, or queue B's flag 0x2A10F00 that 0x50A170 holds while
  the byte is 1) was not read [I]. Consequence: a hold of the next request (A1, A2, F1, B*, L* below) drains nothing;
  only a variant that lets the byte drop (the stock gate: A3, B2-B6) does.
- **[L] The backlog posted by each AI player**: the census assigns each record to the `Update` that posted it. Per M turn
  that is 23.9 AI gaps and 4,229 records. The first gap (player 1's `Update`) also carries the records of the
  turn change and of player 0: 895-1,369 records. Players 2-7 post 120-660 records each, the city states 3-137 each,
  and the barbarians 101-242 (turns 151-160).
- **[C] Two ways to make the frame wait at a gap.** (1) The stock gate: option 2 does not write +0x8CC after that
  player, so the frame waits until queue B is empty. This brings back that player's AI-turn byte round trip, its 0x98
  record and the per-player UI refresh (4665: three times the UI Lua calls). (2) A hold by the proxy: option 2 writes
  +0x8CC as now, and the slot-102 replacement of option 3 holds the next request. It answers a threshold no
  accumulator reaches while queue B has undispatched records. The frame calls that slot about once per frame at
  0x7258C1 (4662's counters: 8,181 frame-site calls in 245 s [L]). The accumulator keeps adding dt, so the first
  frame after the release asks for the next `Update`. Hold (2) needs no EXE write beyond those of options 2 and 3.
- **[C] Option 4 can apply to each per-player drain.** Its guards (Quick Combat and Quick Movement on, the hold
  0x2DE044C clear, no held record, no flag-bit-1 record in the write half) are about queue B's contents, not about the
  phase's end. At an AI gap after a full drain nothing is dispatched-and-held, as at the end [I]. The clear is made in
  `AfterUpdate` while the `Update` still counts as pending, so no pump runs concurrently, and the byte is set back at the
  next `Update`'s top, both as built (`turn-time-study.md` "Option 4 built"). With the byte cleared one pump moves and
  dispatches the whole backlog.

### 11.2 The model and its calibration

`hybridsim.py` replays each turn's records gap by gap under a drain schedule. With the ordering byte set it runs one
pass per frame; unordered it uses one pump per drain. Seconds per M turn = U + R + frames x F0 + records x C + stock-gate
waits x G. U is 3.66 s (laptop poll 41f1). R is a residue (0 or 0.3 s: the poll's T_tick of 0.89 s with options 2+3 is
partly drain). F0 and C are solved from the laptop's timed options 2+3 (9.16 s, 180 ordered end-drain frames) and 2+3+4
(4.75 s, 1 unordered frame). G is solved from option 3 alone (13.00 s, 23 stock-gate waits per turn).

| Constant | Value | What it is |
|---|---|---|
| F0 | 24.6 ms | one drain frame on the laptop |
| C | 0.18-0.25 ms | one record's move and dispatch (by R) |
| G | 61 ms | per stock-gate wait: the byte's round trip, the GC frame, the per-player UI refresh and 0x6D |

**Every variant dispatches the same 4,229 records**, so R and C cancel in the turn time. Only the lag depends on them.

**Plausibility of G [I]:** 23 x 61 ms = 1.40 s per turn. Against it: 0x6D's dispatch (4672: about 81 ms each, 7 per turn)
gives 0.57 s, and the UI Lua calls of 4665 give about 0.93 s (215,000 extra calls per turn x 4.3 us). Together that is
1.50 s.

**Limits [I]:** F0 is one cost per frame. 4676 found that the tail frames a remedy removes are cheaper than the mean.
An ordered variant that removes fewer frames (L*) may therefore gain less or more than shown. The census is one loaded
workstation run, and the laptop constants come from other jobs (cross-series). Quick Movement is on in M; nothing here
models Quick Movement or Quick Combat off.

### 11.3 The variants, per M turn (laptop model)

Output `perf/evidence/0568/hybridsim-ws-S4s-pacing_skip1_tick0.{txt,json}`. "Longest drain" is the longest single wait
the player sees (the next AI player held), with its frames and records. "Lag" runs from the end of the `Update` that
posted a record to its dispatch, at R = 0. "Behind" is how many AI players later a record is shown.

| Id | What happens at an AI gap | s/turn | Longest mid-phase drain | End-of-phase drain | Lag p50 / p95 / max | Behind (max) |
|---|---|---|---|---|---|---|
| C1 | nothing (options 2+3 as built) | 9.16 (timed) | - | 5.50 s (180 frames) | 2.02 / 4.94 / 6.75 s | 23 |
| C2 | nothing, end unordered (2+3+4 as built) | 4.75 (timed) | - | 1.09 s (1 frame) | 1.66 / 2.39 / 2.95 s | 23 |
| C3 | stock gate to empty, ordered (option 3 alone) | 12.88 (13.00 timed) | 2.11 s (74 frames) | 0.49 s | 0.10 / 0.85 / 2.11 s | 0 |
| A1 | hold to empty, ordered | 11.49 | 2.11 s | 0.49 s | 0.10 / 0.85 / 2.11 s | 0 |
| **A2** | **hold, unordered (option 4 at every gap)** | **5.31** | **0.37 s (1 frame, 1,369 records)** | 0.07 s | 0.07 / 0.25 / 0.37 s | 0 |
| A3 | stock gate, unordered (option 4 at every gap, option 2 never writes) | 6.70 | 0.37 s | 0.07 s | 0.07 / 0.25 / 0.37 s | 0 |
| **F1** | **as A2, but only when more than 100 records wait** | **5.04** | 0.37 s | 0.07 s | 0.08 / 0.25 / 0.37 s | 4 |
| F2 | as A2, more than 300 records | 4.94 | 0.37 s | 0.10 s | 0.11 / 0.37 / 0.68 s | 10 |
| B2u / B4u | hold, unordered, every 2nd / 4th player | 5.02 / 4.87 | 0.51 / 0.74 s | 0.08 / 0.11 s | max 0.86 / 1.64 s | 1 / 3 |
| B2 / B4 / B6 | stock gate to empty, ordered, every 2nd / 4th / 6th player | 11.14 / 10.05 / 9.78 | 2.96 / 3.72 / 5.39 s | 0.5-0.6 s | max 3.0 / 3.9 / 5.4 s | 1 / 3 / 5 |
| B4h | as B4, held by the proxy | 9.75 | 3.72 s | 0.55 s | max 3.89 s | 3 |
| L1 / L2 / L3 | hold, ordered, at most 2 / 4 / 8 frames per gap | 9.16 / 9.16 / 9.46 | 0.23 / 0.34 / 0.47 s | 3.52 / 2.33 / 0.62 s | p95 4.25 / 3.94 / 3.40 s | 23 / 22 / 17 |
| L4 / L5 | hold, ordered, down to 500 / 1000 records, at most 8 frames | 9.16 / 9.16 | 0.38 / 0.24 s | 2.57 / 4.37 s | p95 4.0 / 4.5 s | 22 / 23 |
| L6 | L2, end unordered | 7.00 | 0.34 s | 0.17 s | p95 3.73 s | 22 |

What the model says:

1. **With the ordering byte set, no hybrid is both fast and current.** The embark sets and long move chains pass one
   keyed record per frame. Draining them per player costs the serial frames that option 2 overlaps. A1 needs 275
   frames against C1's 180 (+2.3 s). A cap per gap (L1-L3) moves frames from the end drain into the gaps at no gain:
   L3 brings the end drain from 5.5 s to 0.6 s for +0.3 s per turn. The ordered chains still show up to 17 players
   late.
2. **With option 4 at each drain, the hybrid costs about 0.3-0.6 s over 2+3+4 and keeps the map current.** A2 shows
   each player's records one frame after its `Update`. The longest wait is a major civ's 0.37 s frame, against
   C1's 5.5 s and C2's 1.1 s end drain. F1 skips the hold for small backlogs (most city states) and gets 5.04 s.
   The only lag is a few city-state gaps of under 100 records each (at most 4 players, 0.37 s).
3. **The stock gate instead of a hold (A3) costs about 1.4 s per turn (G).** In exchange it keeps stock's per-player
   UI refresh: banners and panels update player by player as in stock. It needs no change to the slot-102
   replacement: option 4's clear is simply made at every gap, and option 2 never writes.
4. **Every k-th player** (B*) is dominated: B4u is 0.17 s faster than F1 but shows up to 3 players late. The ordered
   B* keep multi-second waits.

### 11.4 Proposed: `AiSkipVisualDrain` value 2, "per-player drain" (F1 with an ordered fallback)

No new key: one more value of option 2, and option 4's key keeps its meaning ("drain unordered wherever option 2
drains"). An option id is needed only if the MC wants the floor or the cap as keys (asked below; K4).

- **At each AI gap** (`AfterUpdate`, the scan found the next turn-active AI player, the hold 0x2DE044C clear): option 2
  writes +0x8CC as with value 1. When queue B holds more than **100 undispatched records** (a constant, 11.3 F1), the
  proxy arms a hold. When `AiDrainUnordered` is 1 and option 4's guards pass at that gap, it also clears the ordering
  byte (set back at the next `Update`'s top, as now).
- **The hold** (main thread, in the slot-102 replacement, frame call site only, AI-turn byte 1): while armed, it
  returns a threshold no accumulator reaches. It releases on the first frame where the write half is empty and
  `q+0x2C` equals the dispatch half's size (everything dispatched; the GC of the next pump is not waited for). It also
  releases after **8 frames** (the cap; with the byte cleared the release comes after 1 frame). It never holds while an
  `Update` is pending, in the human's turn, with the hold 0x2DE044C set, or after a failure.
- **Ordered fallback** (option 4 off or its guard refusing at that gap: Quick Movement or Quick Combat off, a held
  record): the same hold with the byte set. That is at most 8 passes, about 0.2 s, per gap (L3: end drain 0.6 s,
  turn about +0.3 s over 2+3). With Quick Movement off the next AI player starts while the earlier one's animations
  still play, as value 1 already does today.
- **Option 3's swap is needed for the hold.** Proposed: value 2 installs the slot-102 copy even when `AiUpdateTickMs`
  is -1. The replacement then returns the original's value whenever it is not holding. The alternative is to refuse
  value 2 without option 3 (D191 makes option 3 the default anyway). The MC chooses.
- **The phase's end** is unchanged: no turn-active player, nothing written, the frame waits, and option 4 clears at
  the end as built.
- **Fail loud** as options 2-4: install checks are option 2's and 3's (and 4's for the clear). Queue B's control block
  is read from the main thread with the guarded reads; a failure stops the holds and writes in that proxy load, raises
  the D118 report and leaves stock pacing.
- **Counters**: gaps, holds armed, released by empty / by the cap, frames held (max), byte clears per gap and refusals
  by reason, the longest hold in ms.
- **Expected** (model, laptop, M): about **5.0-5.3 s per turn** with options 3+4 on. That compares with 9.16 s (2+3) and
  4.75 s (2+3+4). It is about 5.9 s if each gap costs one more frame than modelled [I]. The longest wait the player
  sees is about 0.4 s, against 5.5 s (C1) and 1.1 s (C2). With option 4 off it is about 9.5 s, with an end drain of
  0.6 s instead of 5.5 s.

**What the player sees** [I, to be shown by DIAGNOSTIC pictures or a video in step 2]:

- **During the AI phase**: each major civ's moves appear in one frame right after its turn, as in stock but without
  the per-move frames. The "Processing Turn For" banner steps through the players about as fast as with value 1.
  Units, cities and fog are current to within one or a few city states. There is one long frame (0.1-0.4 s, a short
  stutter) per major civ, where C1 streams everything during the last 5 s. Panels and city banners still refresh about
  once per AI turn (+0x8CC written: 4665's lower UI Lua counts), unlike stock; A3 would keep the per-player refresh at
  +1.4 s.
- **At the end of the AI phase**: no burst under the "Barbarians" banner, only the barbarians' records (0.07 s).

### 11.5 Risks [I]

- **Held records and combats**: with Quick Combat off a combat holds records. Option 4's guard then refuses, the
  ordered hold runs, and the cap releases the next player after 8 frames while the combat plays (value 1 does the same
  with no cap at all).
- **Quick Movement off**: MC #4203 (a)'s guard stays. The fallback is ordered and capped, so per player at most about
  0.2 s, and the animations overlap the next player as with value 1. At stock pacing (no cap) a Quick Movement off
  observer turn waits 30 s for its drains (4675); this variant never does.
- **A hold that never releases** (a record that never dispatches, the queue pointers moved): the cap releases it.
  A failed read stops the holds (fail loud).
- **Threads**: the hold reads queue B on the main thread, which is the only thread that pumps it. No `Update` is
  pending during a hold, so the DLL posts nothing then. The 4660 lesson holds: one `Update` per request, and UI Lua
  still runs in every gap.
- **Option 4 at every gap** multiplies its clears and set-backs by about 13 per turn. Each walk is at most 0.5 ms
  (4675: 484 us for 4,890 records). The set-back's "byte must still be 0" check runs 13 times as often, and a
  LoadGameState in between is reported as today.
- **Memory**: queue B stays below about 1,400 records instead of about 4,900. This may lower option 4's unexplained
  +2.2 MB/turn growth (4675's laptop pair). That is to be measured, not claimed.
- **Identity**: pacing only. statscmp windowed vs windowed must be IDENTICAL (D164), as for options 2-4.

### 11.6 Step 2, once the MC says yes (not started)

Code in `proxy/src/proxy_pacing.{h,cpp}` (the value, the arming in `AfterUpdate`, the hold in the slot-102
replacement, the per-gap clear), `options/` (range 0-2), `docs/OPTIONS.md`, offline tests in
`proxy/tools/rt_tests/pacing_unit.cpp`, with a negative for each guard:

- off;
- value 1 unchanged;
- no hold in the human's turn, with the hold set, or while an `Update` is pending;
- release by empty and by the cap;
- an ordered fallback per guard;
- a failure in the hold's read.

Then the arms in `perf/turnstudy/runner/arms.json`, and a functional pair on a test machine with DIAGNOSTIC
pictures or a short video of the end of the AI phase. The laptop pair (D178: M 151-160, 3 runs x 10 turns, Williams,
quiet and alone, GPU drivers named, D175) compares options 2+3 against value 2 + option 3. A third arm with option 4
on is proposed (about +30 min). Each arm reports its longest single wait (the new counter), and findings go into
`turn-time-study.md` and here.

### 11.7 For the MC

1. Variant: **F1 as value 2** (proposed), or A2 (a drain after every player, about +0.3 s), or A3 (the stock gate
   with option 4 at every gap: no hold, per-player UI refresh, about +1.4 s).
2. The floor (100 records) and the cap (8 frames): constants (proposed), or keys (option ids from the MC).
3. Value 2 without option 3: install the slot-102 copy anyway (proposed), or refuse.
4. Grant the step-2 paths of 11.6 once their holders allow, and the laptop pair's arms (two or three).

*Answered 2026-11-18 19:35 by the MC (relayed by dispatcher-B):*

1. F1 becomes `AiSkipVisualDrain` value 2. A2 and A3 stay here as alternatives.
2. The floor (100) and the cap (8) are named constants in `proxy_pacing`, documented in `docs/OPTIONS.md`. There are
   no new keys.
3. Value 2 with `AiUpdateTickMs` -1 installs the slot-102 copy anyway and keeps the stock tick. One log line says so,
   and it fails loud (D118) if the copy cannot be installed.
4. Step 2 is its own ticket, 0571. It waits until 0504 and 0561 release `options/` and `docs/OPTIONS.md`, and 0570
   releases `turn-time-study.md`. The laptop pair has 3 arms: options 2+3, options 2+3+4, and value 2 with options 3
   and 4. Value 2 requires `AiDrainUnordered` 1, or it logs that it gains nothing.

*Status 2026-11-19 (impl-0571, ticket 0571): F1 was **built** as answered (the hold in slot 102, floor 100, cap 8;
commit cd62e2f06) and **does not drain**: every one of 258 holds in 10 M turns ended at the 8-frame cap with queue B
untouched (the correction in 11.1), so it only added about 3.2 s per turn (oldpc, one run). On the MC's yes (#7167,
2026-11-19 18:15) value 2 is now **F1 with the stock gate** (A3 with the floor): at a gap with more than 100 undispatched
records option 2 does not write +0x8CC, the frame's own gate drops the byte and drains queue B, and option 4 clears the
ordering byte at that gap so the drain is one pump; smaller gaps are written as value 1; no hold, no slot-102 copy. A
prototype, off by default; its default and any release follow a second laptop set (PERFORMANCE.md) and come back to the
MC. Details and numbers: `turn-time-study.md` "Option 2 value 2: drain after each AI player".*

### 11.8 Not verified

- Every second figure: a model on one loaded workstation census run with laptop constants from other jobs. The F1
  floor and the 8-frame cap are model choices, not measured optima.
- Why the frame after an `Update` never pumps with options 2+3. The fact is [L]; the UI Lua wait is [I].
- That the slot-102 replacement is called on every frame of an AI gap (4662's counters are per run, not per frame), and
  that a large threshold holds the request with nothing else changing.
- Quick Movement or Quick Combat off, scenario H, a human watching the AI phase, a leader message during a hold.
